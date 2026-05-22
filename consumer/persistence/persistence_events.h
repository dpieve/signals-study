#pragma once
#include <stop_token>
#include <concurrentqueue.h>
#include "../common/telemetry_types.h"

/// Single-producer / single-consumer event bus that carries CommittedBatch
/// notifications from the persistence thread to the processing engine.
///
/// The persistence thread calls emit() after a successful COMMIT; the
/// processing engine calls wait_dequeue() in a loop until its stop token
/// fires.
class PersistenceEvents {
public:
    /// Publish a committed batch to the processing engine.
    void emit(CommittedBatch batch);

    /// Block until a batch is available or the stop token is signalled.
    /// Polls with a 1 ms back-off so the thread stays responsive.
    /// @return true if @p out was populated; false if stop was requested.
    bool wait_dequeue(CommittedBatch& out, std::stop_token st);

private:
    moodycamel::ConcurrentQueue<CommittedBatch> queue_;
};
