/* -*- Mode: C++; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
#ifndef SQLITE_BASE_H
#define SQLITE_BASE_H 1

#include <map>
#include <vector>

#ifdef USE_SYSTEM_LIBSQLITE3
#include <sqlite3.h>
#else
#include "embedded/sqlite3.h"
#endif

#include "sqlite-pst.hh"
#include "sqlite-strategies.hh"
#include "item.hh"

class EventuallyPersistentEngine;
class EPStats;

class StrategicSqlite3 {
public:

    /**
     * Construct an instance of sqlite with the given database name.
     */
    StrategicSqlite3(EventuallyPersistentEngine &theEngine, SqliteStrategy *s);

    /**
     * Cleanup.
     */
    ~StrategicSqlite3() {
        close();
    }

    /**
     * Reset database to a clean state.
     */
    void reset();

    /**
     * Begin a transaction (if not already in one).
     */
    void begin() {
        if(!intransaction) {
            execute("begin");
            intransaction = true;
        }
    }

    /**
     * Commit a transaction (unless not currently in one).
     *
     * Returns false if the commit fails.
     */
    bool commit() {
        if(intransaction) {
            // If commit returns -1, we're still in a transaction.
            intransaction = (execute("commit") == -1);
        }
        // !intransaction == not in a transaction == committed
        return !intransaction;
    }

    /**
     * Rollback a transaction (unless not currently in one).
     */
    void rollback() {
        if(intransaction) {
            intransaction = false;
            execute("rollback");
        }
    }

    /**
     * Overrides set().
     */
    void set(const Item &item, Callback<std::pair<bool, int64_t> > &cb);

    /**
     * Overrides get().
     */
    void get(const std::string &key, uint64_t rowid, Callback<GetValue> &cb);

    /**
     * Overrides del().
     */
    void del(const std::string &key, uint16_t vbucket,
             Callback<bool> &cb);

    bool delVBucket(uint16_t vbucket);
    bool setVBState(uint16_t vbucket, const std::string &to);
    std::map<uint16_t, std::string> listPersistedVbuckets(void);

    /**
     * Overrides dump
     */
    void dump(Callback<GetValue> &cb);

private:
    /**
     * Shortcut to execute a simple query.
     *
     * @param query a simple query with no bindings to execute directly
     */
    int execute(const char *query) {
        PreparedStatement st(db, query);
        return st.execute();
    }

    void insert(const Item &itm, Callback<std::pair<bool, int64_t> > &cb);
    void update(const Item &itm, Callback<std::pair<bool, int64_t> > &cb);
    int64_t lastRowId();

    EventuallyPersistentEngine &engine;
    EPStats &stats;

    /**
     * Direct access to the DB.
     */
    sqlite3 *db;

    void open() {
        assert(strategy);
        db = strategy->open();
        intransaction = false;
    }

    void close() {
        strategy->close();
        intransaction = false;
        db = NULL;
    }

    SqliteStrategy *strategy;

    bool intransaction;
};

#endif /* SQLITE_BASE_H */
