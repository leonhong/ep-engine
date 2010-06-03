/* -*- Mode: C++; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
#include "config.h"
#include <assert.h>
#include <poll.h>
#include <fcntl.h>

#include <memcached/engine.h>
#include <memcached/protocol_binary.h>

#include "ep_engine.h"


class BinaryMessage {
public:
    BinaryMessage() : size(0) {
        data.rawBytes = NULL;
    }

    BinaryMessage(const protocol_binary_request_header &h) throw (std::runtime_error)
        : size(ntohl(h.request.bodylen) + sizeof(h.bytes))
    {
        // verify the internal
        if (h.request.magic != PROTOCOL_BINARY_REQ &&
            h.request.magic != PROTOCOL_BINARY_RES) {
            throw std::runtime_error("Invalid package detected on the wire");
        }
        data.rawBytes = new char[size];
        memcpy(data.rawBytes, reinterpret_cast<const char*>(&h),
               sizeof(h.bytes));
    }

    virtual ~BinaryMessage() {
        delete []data.rawBytes;
    }

    size_t size;
    union {
        protocol_binary_request_header *req;
        protocol_binary_request_tap_no_extras *tap;
        protocol_binary_request_tap_connect *tap_connect;
        protocol_binary_request_tap_mutation *mutation;
        protocol_binary_request_tap_delete *remove;
        protocol_binary_request_tap_flush *flush;
        protocol_binary_request_tap_opaque *opaque;
        protocol_binary_request_tap_vbucket_set *vs;
        char *rawBytes;
    } data;
};

class TapConnectBinaryMessage : public BinaryMessage {
public:
    TapConnectBinaryMessage(const std::string &peer, uint32_t flags,
                            uint64_t backfill) {
        size = sizeof(data.tap_connect->bytes) + peer.length();
        if (flags & TAP_CONNECT_FLAG_BACKFILL) {
            size += sizeof(backfill);
        }
        data.rawBytes = new char[size];
        data.req->request.magic = PROTOCOL_BINARY_REQ;
        data.req->request.opcode = PROTOCOL_BINARY_CMD_TAP_CONNECT;
        data.req->request.keylen = ntohs(static_cast<uint16_t>(peer.length()));
        data.req->request.extlen = 4;
        data.req->request.datatype = PROTOCOL_BINARY_RAW_BYTES;
        data.req->request.vbucket = 0;
        uint32_t bodylen = size - sizeof(data.req->bytes);
        data.req->request.bodylen = htonl(bodylen);;
        data.req->request.opaque = 0xcafecafe;
        data.req->request.cas = 0;
        data.tap_connect->message.body.flags = htonl(flags);
        char *ptr = data.rawBytes + sizeof(data.tap_connect->bytes);
        memcpy(ptr, peer.c_str(), peer.length());
        ptr += peer.length();

        if (flags & TAP_CONNECT_FLAG_BACKFILL) {
            backfill = htonll(backfill);
            memcpy(ptr, &backfill, sizeof(backfill));
        }
    }
};

void TapClientConnection::resolve(void) throw (std::runtime_error) {
    if (ai == NULL) {
        struct addrinfo hints;
        std::memset(&hints, 0, sizeof(hints));
        hints.ai_flags = AI_PASSIVE;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_family = AF_UNSPEC;

        std::string host = peer;
        std::string port = "11211";
        ssize_t s = host.find(":");
        if (s != -1) {
            port = host.substr(s + 1);
            host = host.substr(0, s);
        }

        int error = getaddrinfo(host.c_str(), port.c_str(), &hints, &ai);
        if (error != 0) {
            std::stringstream ss;
            ss << "getaddrinfo(): "
               << (error != EAI_SYSTEM) ? gai_strerror(error) : strerror(error);
            throw std::runtime_error(ss.str());
        }
    }
}

void TapClientConnection::createSocket() throw (std::runtime_error) {
    for (struct addrinfo *next= ai; next; next= next->ai_next) {
        if ((sock = socket(ai->ai_family, SOCK_STREAM,
                           ai->ai_protocol)) != -1) {
            break;
        }
    }

    if (sock == -1) {
        throw std::runtime_error("Failed to create socket");
    }

    int fl;
    if ((fl = fcntl(sock, F_GETFL, 0)) < 0) {
        std::stringstream msg;
        msg << "Failed to get current flags: " << strerror(errno);
        throw std::runtime_error(msg.str());
    }

    if ((fl & O_NONBLOCK) != O_NONBLOCK) {
        if (fcntl(sock, F_SETFL, fl | O_NONBLOCK) < 0) {
            std::stringstream msg;
            msg << "Failed to enable O_NONBLOCK: " << strerror(errno);
            throw std::runtime_error(msg.str());
        }
    }
}

bool TapClientConnection::connect() throw (std::runtime_error) {
    // create socket and make it nonblocking
    if (!connected) {
        resolve();
        if (sock == -1) {
            createSocket();
        }

        while (::connect(sock, ai->ai_addr, ai->ai_addrlen) == -1) {
            if (errno == EINPROGRESS || errno == EALREADY) {
                return false;
            } else if (errno == EISCONN) {
                TapConnectBinaryMessage msg(peer, flags, backfillage);
                connected = true;
                if (send(sock, msg.data.rawBytes, msg.size, 0) !=
                    static_cast<ssize_t>(msg.size)) {
                    // It's unlikely that we're not able to send that few
                    // bytes, so just retry if it happens..
                    setFailed();
                    return false;
                }
                return true;
            } else if (errno != EINTR) {
                /* A real problem occured */
                std::stringstream msg;
                msg << "Failed to connect: " << strerror(errno);
                throw std::runtime_error(msg.str());
            }
        }
    }

    return true;
}

void TapClientConnection::run() {
    LockHolder lh(mutex);
    while (!terminate) {
        lh.unlock();
        try {
            if (retry_interval == 0) {
                if (connect()) {
                    consume();
                }
            } else {
                --retry_interval;
            }

            if (connected) {
                wait(POLLIN);
            } else if (sock == -1) {
                wait(0);
            } else {
                if (!wait(POLLOUT)) {
                    --connect_timeout;
                    if (connect_timeout == 0) {
                        setFailed();
                        if (!shouldRetry()) {
                            terminate = true;
                        }
                    }
                }
            }
        } catch (std::runtime_error &e) {
            std::stringstream msg;
            msg << "An exception occured in the tap stream: " << e.what()
                << std::endl;
            getLogger()->log(EXTENSION_LOG_DEBUG, NULL, msg.str().c_str());
            setFailed();
        }
        lh.lock();
    }
    running = false;
    zombie = true;
}

bool TapClientConnection::wait(short mask) throw (std::runtime_error) {
    struct pollfd fds[1];
    fds[0].fd = sock;
    fds[0].events = mask;
    int error = poll(fds, 1, 1000);

    if (error != 1 || fds[0].revents & POLLERR) {
        if (fds[0].revents & POLLERR) {
            int err;
            socklen_t len = sizeof(err);
            (void)getsockopt(sock, SOL_SOCKET, SO_ERROR, &err, &len);
            std::stringstream msg;
            msg << "Error on TAP client stream: ";
            if (err == 0) {
                msg << errno;
            } else {
                msg << err;
            }
            (void)close(sock);
            sock = -1;
            throw std::runtime_error(msg.str());
        }

        return false;
    }

    return true;
}

void TapClientConnection::apply() {
    /* Call the tap notify function!! */
    protocol_binary_request_tap_no_extras *tap = message->data.tap;
    uint16_t nengine = ntohs(tap->message.body.tap.enginespecific_length);
    uint16_t tap_flags = ntohs(tap->message.body.tap.flags);
    uint32_t seqno = ntohl(tap->message.header.request.opaque);
    uint8_t ttl = tap->message.body.tap.ttl;
    char *engine_specific = message->data.rawBytes + sizeof(tap->bytes);
    char *key = engine_specific + nengine;
    uint16_t nkey = ntohs(tap->message.header.request.keylen);
    char *data = key + nkey;
    uint32_t fl = 0;
    uint32_t exptime = 0;
    uint32_t ndata = ntohl(tap->message.header.request.bodylen) - nengine - nkey - 8;
    uint16_t vbucket = ntohs(tap->message.header.request.vbucket);
    uint64_t cas = ntohll(tap->message.header.request.cas);
    tap_event_t event = TAP_OPAQUE;
    bool ignore = false;

    switch (tap->message.header.request.opcode) {
    case PROTOCOL_BINARY_CMD_TAP_MUTATION:
        event = TAP_MUTATION;
        break;
    case PROTOCOL_BINARY_CMD_TAP_DELETE:
        event = TAP_DELETION;
        break;
    case PROTOCOL_BINARY_CMD_TAP_FLUSH:
        event = TAP_FLUSH;
        break;
    case PROTOCOL_BINARY_CMD_TAP_OPAQUE:
        event = TAP_OPAQUE;
        break;
    case PROTOCOL_BINARY_CMD_TAP_VBUCKET_SET:
        event = TAP_VBUCKET_SET;
        break;
    default:
        ignore = true;
    }

    if (ignore) {
        getLogger()->log(EXTENSION_LOG_DEBUG, NULL,
                         "Ignoring unknown packet on tap stream: 0x%02x\n",
                         static_cast<unsigned int>(tap->message.header.request.opcode));
    } else {
        if (event == TAP_MUTATION) {
            protocol_binary_request_tap_mutation *mutation;
            mutation = message->data.mutation;
            fl = ntohl(mutation->message.body.item.flags);
            exptime = ntohl(mutation->message.body.item.expiration);
            key += 8;
            data += 8;
            ndata -= 8;
        }

        ENGINE_ERROR_CODE ret;
        ret = engine->tapNotify(NULL,
                                engine_specific, nengine,
                                ttl - 1, tap_flags, event, seqno,
                                key, nkey, fl, exptime,
                                cas, data, ndata, vbucket);

        switch (ret) {
        case ENGINE_SUCCESS:
            break;
        case ENGINE_DISCONNECT:
            terminate = true;
            // FALLTHROUGH
        default:
            getLogger()->log(EXTENSION_LOG_DEBUG, NULL,
                             "tapNotify returned: %d\n", static_cast<int>(ret));
        }

        if (ret == ENGINE_DISCONNECT) {
            terminate = true;
        }
    }
}

void TapClientConnection::consume() {
    LockHolder lh(mutex);
    do {
        lh.unlock();
        char *dst;
        size_t nbytes;
        ssize_t nr;

        if (message == NULL) {
            dst = reinterpret_cast<char*>(&header) + offset;
            nbytes = sizeof(header.bytes) - offset;
        } else {
            dst = message->data.rawBytes + sizeof(header.bytes) + offset;
            nbytes = ntohl(header.request.bodylen) - offset;
        }

        if ((nr = recv(sock, dst, nbytes, 0)) == -1) {
            switch (errno) {
            case EINTR:
                break;
            case EWOULDBLOCK:
                return;
            default:
                std::stringstream msg;
                msg << "Failed to read from socket: " << strerror(errno);
                throw std::runtime_error(msg.str());
            }
        } else if (nr == 0) {
            throw std::runtime_error("stream closed");
        }

        offset += nr;
        if (nr == static_cast<ssize_t>(nbytes)) {
            // we got everything..
            offset = 0;
            if (message == NULL) {
                message = new BinaryMessage(header);
            } else {
                /* Call the tap notify function!! */
                apply();
                delete message;
                message = NULL;
            }
        }

        lh.lock();
    } while (!terminate);
}

void* tapClientConnectionMain(void *arg)
{
    TapClientConnection *conn = reinterpret_cast<TapClientConnection*>(arg);
    conn->run();
    return NULL;
}
