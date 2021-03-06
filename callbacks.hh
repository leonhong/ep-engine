/* -*- Mode: C++; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
#ifndef CALLBACKS_H
#define CALLBACKS_H 1

#include <cassert>

#include "locks.hh"

class Item;

/**
 * Value for callback for GET operations.
 */
class GetValue {
public:
    GetValue() : value(NULL), status(ENGINE_KEY_ENOENT) { }

    explicit GetValue(Item *v, ENGINE_ERROR_CODE s=ENGINE_SUCCESS) :
        value(v), status(s) { }

    /**
     * The value retrieved for the key.
     */
    Item* getValue() { return value; }

    /**
     * Engine code describing what happened.
     */
    ENGINE_ERROR_CODE getStatus() const { return status; }

private:

    Item* value;
    ENGINE_ERROR_CODE status;
};

/**
 * Interface for callbacks from storage APIs.
 */
template <typename RV>
class Callback {
public:

    virtual ~Callback() {}

    /**
     * Method called on callback.
     */
    virtual void callback(RV &value) = 0;

    virtual void setStatus(int status) {
        myStatus = status;
    }

    virtual int getStatus() {
        return myStatus;
    }

private:

    int myStatus;
};

/**
 * Threadsafe callback implementation that just captures the value.
 */
template <typename T>
class RememberingCallback : public Callback<T> {
public:

    /**
     * Construct a remembering callback.
     */
    RememberingCallback() : fired(false), so() { }

    /**
     * Clean up (including lock resources).
     */
    ~RememberingCallback() {
    }

    /**
     * The callback implementation -- just store a value.
     */
    void callback(T &value) {
        LockHolder lh(so);
        val = value;
        fired = true;
        so.notify();
    }

    /**
     * Wait for a value to be available.
     *
     * This method will return immediately if a value is currently
     * available, otherwise it will wait indefinitely for a value
     * to arrive.
     */
    void waitForValue() {
        LockHolder lh(so);
        if (!fired) {
            so.wait();
        }
        assert(fired);
    }

    /**
     * The value that was captured from the callback.
     */
    T    val;
    /**
     * True if the callback has fired.
     */
    bool fired;

private:
    SyncObject so;

    DISALLOW_COPY_AND_ASSIGN(RememberingCallback);
};

#endif /* CALLBACKS_H */
