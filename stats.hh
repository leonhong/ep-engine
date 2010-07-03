/* -*- Mode: C++; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */

#include <memcached/engine.h>

#include "common.hh"
#include "atomic.hh"

/**
 * Global engine stats container.
 */
class EPStats {
public:

    EPStats() {}

    //! How long it took us to load the data from disk.
    Atomic<time_t> warmupTime;
    //! Whether we're warming up.
    Atomic<bool> warmupComplete;
    //! Number of records warmed up.
    Atomic<size_t> warmedUp;
    //! size of the input queue
    Atomic<size_t> queue_size;
    //! Size of the in-process (output) queue.
    Atomic<size_t> flusher_todo;
    //! Objects that were rejected from persistence for being too fresh.
    Atomic<size_t> tooYoung;
    //! Objects that were forced into persistence for being too old.
    Atomic<size_t> tooOld;
    //! Number of items persisted.
    Atomic<size_t> totalPersisted;
    //! Cumulative number of items added to the queue.
    Atomic<size_t> totalEnqueued;
    //! Number of times an item flush failed.
    Atomic<size_t> flushFailed;
    //! Number of times a commit failed.
    Atomic<size_t> commitFailed;
    //! How long an object is dirty before written.
    Atomic<rel_time_t> dirtyAge;
    //! Oldest enqueued object we've seen while persisting.
    Atomic<rel_time_t> dirtyAgeHighWat;
    //! How old persisted data was when it hit the persistence layer
    Atomic<rel_time_t> dataAge;
    //! Oldest data we've seen while persisting.
    Atomic<rel_time_t> dataAgeHighWat;
    //! How long does it take to do an entire flush cycle.
    Atomic<rel_time_t> flushDuration;
    //! Longest flush cycle we've seen.
    Atomic<rel_time_t> flushDurationHighWat;
    //! Amount of time spent in the commit phase.
    Atomic<rel_time_t> commit_time;
    /**
     * Total number of items.
     *
     * This would be total_items if we recycled items, but we don't
     * right now.
     */
    Atomic<size_t> curr_items;
    //! Beyond this point are config items
    //! Minimum data age before a record can be persisted
    Atomic<int> min_data_age;
    //! Maximum data age before a record is forced to be persisted
    Atomic<int> queue_age_cap;
    //! Current tap queue size.
    Atomic<size_t> tap_queue;
    //! Total number of tap messages sent.
    Atomic<size_t> tap_fetched;

private:

    DISALLOW_COPY_AND_ASSIGN(EPStats);
};

/**
 * Stats returned by key stats.
 */
struct key_stats {
    //! The item's CAS
    uint64_t cas;
    //! The expiration time of the itme
    rel_time_t exptime;
    //! When the item was dirtied (if applicable).
    rel_time_t dirtied;
    //! How long the item has been dirty (if applicable).
    rel_time_t data_age;
    //! The item's current flags.
    uint32_t flags;
    //! True if this item is dirty.
    bool dirty;
};