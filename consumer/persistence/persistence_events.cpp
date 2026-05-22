#include "persistence_events.h"
#include <chrono>
#include <thread>

void PersistenceEvents::emit(CommittedBatch batch) {
    queue_.enqueue(std::move(batch));
}

bool PersistenceEvents::wait_dequeue(CommittedBatch& out, std::stop_token st) {
    using namespace std::chrono_literals;
    while (!st.stop_requested()) {
        if (queue_.try_dequeue(out)) {
            return true;
        }
        std::this_thread::sleep_for(1ms);
    }
    // Drain one remaining item so the processing engine can flush on shutdown.
    if (queue_.try_dequeue(out)) {
        return true;
    }
    return false;
}
