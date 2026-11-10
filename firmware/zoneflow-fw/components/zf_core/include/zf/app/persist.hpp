// SPDX-License-Identifier: Apache-2.0
// Hand-off of runtime state (SchedulerPersist) from the control task to a
// low-priority writer task. The control task only copies a plain struct
// under a mutex held for that copy; it never builds JSON, allocates, or
// waits for flash. The writer task builds the JSON and writes storage, and
// may block there for as long as flash takes (NVS garbage collection
// erases sectors).
//
// Threads:
//  - post(), takeFailure(): control task.
//  - take(), reportWrite(), persistWriteOnce(): writer task.
#pragma once

#include <atomic>
#include <mutex>

#include "zf/config/config_store.hpp"
#include "zf/sched/scheduler.hpp"

namespace zf {

class PersistMailbox {
public:
    // Replaces any copy not yet taken: only the latest state matters.
    void post(const SchedulerPersist &state)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        slot_ = state;
        pending_ = true;
    }

    bool take(SchedulerPersist &out)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!pending_) {
            return false;
        }
        out = slot_;
        pending_ = false;
        return true;
    }

    void reportWrite(bool ok)
    {
        if (!ok) {
            failed_.store(true, std::memory_order_release);
        }
    }

    // True once after a failed write.
    bool takeFailure() { return failed_.exchange(false, std::memory_order_acq_rel); }

private:
    std::mutex mutex_; // held for one struct copy, never across a write
    SchedulerPersist slot_{};
    bool pending_ = false;
    std::atomic<bool> failed_{false};
};

// Writer task: stores the latest posted state under ConfigStore::kRuntimeKey.
// Returns false if nothing was posted.
bool persistWriteOnce(PersistMailbox &box, IBlobStorage &storage);

} // namespace zf
