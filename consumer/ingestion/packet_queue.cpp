#include "packet_queue.h"
#include <chrono>
#include <thread>

PacketQueue::PacketQueue(std::size_t capacity)
    : capacity_(capacity),
      queue_(capacity)   // pre-allocate moodycamel internal blocks
{}

bool PacketQueue::enqueue(IngestedPacket&& pkt) noexcept {
    // Optimistically reserve a slot.
    std::size_t old = size_.fetch_add(1, std::memory_order_relaxed);
    if (old >= capacity_) {
        // No room — roll back the reservation and count the drop.
        size_.fetch_sub(1, std::memory_order_relaxed);
        drop_count.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    // The queue itself is effectively unbounded; capacity is enforced above.
    return queue_.enqueue(std::move(pkt));
}

bool PacketQueue::wait_dequeue_timed(IngestedPacket& out, std::stop_token st) {
    using namespace std::chrono_literals;
    while (!st.stop_requested()) {
        if (queue_.try_dequeue(out)) {
            size_.fetch_sub(1, std::memory_order_relaxed);
            return true;
        }
        std::this_thread::sleep_for(1ms);
    }
    // Drain one last item if available before acknowledging stop so callers
    // can flush remaining items by looping until false.
    if (queue_.try_dequeue(out)) {
        size_.fetch_sub(1, std::memory_order_relaxed);
        return true;
    }
    return false;
}
