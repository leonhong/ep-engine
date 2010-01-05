/* -*- Mode: C++; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
#ifndef KVSTORE_H
#define KVSTORE_H 1

#include <assert.h>
#include <stdbool.h>
#include <stdexcept>
#include <iostream>
#include <sstream>
#include <list>

// Stolen from http://google-styleguide.googlecode.com/svn/trunk/cppguide.xml
// A macro to disallow the copy constructor and operator= functions
// This should be used in the private: declarations for a class
#define DISALLOW_COPY_AND_ASSIGN(TypeName)      \
    TypeName(const TypeName&);                  \
    void operator=(const TypeName&)

#include "callbacks.hh"

/**
 * Value for callback for GET operations.
 */
class GetValue {
public:
    GetValue() { }

    GetValue(std::string v, bool s) {
        value = v;
        success = s;
    }

    friend std::ostream& operator<<(std::ostream &o, GetValue &gv) {
        return o << "{GetValue success=" << gv.success
                 << ", value=\"" << gv.value << "\"}";
    }

    /**
     * The value retrieved for the key.
     */
    std::string value;
    /**
     * True if a value was successfully retrieved.
     */
    bool success;
};

/**
 * An individual kv storage (or way to access a kv storage).
 */
class KVStore {
public:

    KVStore() {}

    virtual ~KVStore() {}

    /**
     * Called after each test to reinitialize the test.
     */
    virtual void reset() {}

    /**
     * Method that should not return until the driver has done its job.
     *
     * @param c the callback that will fire when the noop is evalutated
     */
    virtual void noop(Callback<bool> &c) {
        bool t = true;
        c.callback(t);
    }

    /**
     * Set a given key and value.
     *
     * @param key the key to set
     * @param val the value to set
     * @param cb callback that will fire with true if the set succeeded
     */
    virtual void set(std::string &key, std::string &val,
                     Callback<bool> &cb) = 0;

    /**
     * Set a given key and (character) value.
     *
     * @param key the key to set
     * @param val the value to set
     * @param val the number of bytes in the value
     * @param cb callback that will fire with true if the set succeeded
     */
    virtual void set(std::string &key, const char *val, size_t nbytes,
                     Callback<bool> &cb) = 0;

    /**
     * Get the value for the given key.
     *
     * @param key the key
     * @param cb callback that will fire with the retrieved value
     */
    virtual void get(std::string &key, Callback<GetValue> &cb) = 0;

    /**
     * Delete a value for a key.
     *
     * @param key the key
     * @param cb callback that will fire with true if the value
     *           existed and then was deleted
     */
    virtual void del(std::string &key, Callback<bool> &cb) = 0;

    /**
     * For things that support transactions, this signals the
     * beginning of one.
     */
    virtual void begin() {}

    /**
     * For things that support transactions, this signals the
     * successful completion of one.
     */
    virtual void commit() {}

    /**
     * For things that support transactions, this signals the
     * unsuccessful completion of one.
     */
    virtual void rollback() {}

private:
    DISALLOW_COPY_AND_ASSIGN(KVStore);
};

#endif /* KVSTORE_H */