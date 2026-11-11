// SPDX-License-Identifier: Apache-2.0
// Hand-off of runtime state (SchedulerPersist) and the run budget
// (BudgetRecord) from the control task to a low-priority writer task. The control task only copies a plain struct
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

#include "zf/app/budget.hpp"
#include "zf/config/config_store.hpp"
#include "zf/sched/scheduler.hpp"

// The runtime record (the scheduler's consumed-start keys and the rain
// delay) has two copies too, `runtime` and `runtime_b`, written alternately,
// each framed as "ZFR1", a 32-bit sequence number and a CRC-32 over both
// ahead of the JSON. A copy that fails its check is never used. When the
// newest copy may be the damaged one (RecordLoad::Uncertain), the scheduler
// restores the older copy and reports every start since its keys as missed
// rather than run one of them a second time (system soak simulation, section
// 22 gap 9). A record from before the frame (plain JSON in `runtime`) is
// read as it is.

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

    // The run budget, same rules: only the latest matters.
    void postBudget(const BudgetRecord &r)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        budget_ = r;
        budgetPending_ = true;
    }
    bool takeBudget(BudgetRecord &out)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!budgetPending_) {
            return false;
        }
        out = budget_;
        budgetPending_ = false;
        return true;
    }

    // Sequence numbers of the newest copies found at boot; the writer
    // continues from them. Before the writer task starts.
    void setSequences(uint32_t runtimeSeq, uint32_t budgetSeq)
    {
        runtimeSeq_ = runtimeSeq;
        budgetSeq_ = budgetSeq;
    }
    // Writer task only.
    uint32_t nextRuntimeSeq() { return ++runtimeSeq_; }
    uint32_t nextBudgetSeq() { return ++budgetSeq_; }

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
    BudgetRecord budget_{};
    bool budgetPending_ = false;
    uint32_t runtimeSeq_ = 0; // writer task
    uint32_t budgetSeq_ = 0;  // writer task
    std::atomic<bool> failed_{false};
};

inline constexpr const char *kRuntimeKeyB = "runtime_b";

// Writer task: stores the latest posted state in the older of its two
// copies, and the latest run budget likewise. Returns false if nothing was
// posted.
bool persistWriteOnce(PersistMailbox &box, IBlobStorage &storage);

// The framed runtime record.
std::string frameRuntime(uint32_t seq, const std::string &json);
// Reads both copies of the runtime record; `seq` is the newest valid copy's.
RecordLoad loadRuntime(IBlobStorage &storage, SchedulerPersist &out, uint32_t &seq);

} // namespace zf
