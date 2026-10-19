#pragma once
#include "sweep_types.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>

namespace spectrum {
    // Single producer / single consumer queue of IQ blocks with preallocated
    // slots. The producer (a USB callback thread) never blocks and never
    // allocates: when the queue is full the block is dropped and counted.
    class BlockQueue {
    public:
        struct Block {
            uint64_t freqHz = 0;
            int64_t timeNs = 0;
            int count = 0;
            const cf32* samples = nullptr;
        };

        void configure(int slotCount, int maxSamples) {
            slots.assign(std::max(slotCount, 2), Slot());
            for (auto& s : slots) { s.data.assign(std::max(maxSamples, 1), cf32(0, 0)); }
            maxSamp = std::max(maxSamples, 1);
            head.store(0);
            tail.store(0);
            dropped.store(0);
        }

        // Copies the LAST min(count, maxSamples) interleaved I/Q pairs of the block.
        bool push(uint64_t freqHz, int64_t timeNs, const float* iq, int count) {
            uint32_t h = head.load(std::memory_order_relaxed);
            uint32_t t = tail.load(std::memory_order_acquire);
            if (h - t >= (uint32_t)slots.size()) {
                dropped.fetch_add(1, std::memory_order_relaxed);
                return false;
            }
            Slot& s = slots[h % slots.size()];
            int n = std::min(count, maxSamp);
            const float* src = iq + 2 * (size_t)(count - n);
            for (int i = 0; i < n; i++) { s.data[i] = cf32(src[2 * i], src[2 * i + 1]); }
            s.freqHz = freqHz;
            s.timeNs = timeNs;
            s.count = n;
            head.store(h + 1, std::memory_order_release);
            cv.notify_one();
            return true;
        }

        // Waits up to timeoutMs for a block. The block stays valid until release().
        bool pop(Block& out, int timeoutMs) {
            uint32_t t = tail.load(std::memory_order_relaxed);
            if (head.load(std::memory_order_acquire) == t) {
                std::unique_lock<std::mutex> lck(mtx);
                cv.wait_for(lck, std::chrono::milliseconds(timeoutMs), [&] { return head.load(std::memory_order_acquire) != t || woken.load(); });
                woken.store(false);
                if (head.load(std::memory_order_acquire) == t) { return false; }
            }
            Slot& s = slots[t % slots.size()];
            out.freqHz = s.freqHz;
            out.timeNs = s.timeNs;
            out.count = s.count;
            out.samples = s.data.data();
            return true;
        }

        void release() { tail.fetch_add(1, std::memory_order_release); }

        // Discards everything queued (consumer side only).
        void drain() { tail.store(head.load(std::memory_order_acquire), std::memory_order_release); }

        void wake() {
            woken.store(true);
            cv.notify_all();
        }

        uint64_t droppedCount() const { return dropped.load(); }
        int size() const { return (int)(head.load() - tail.load()); }

    private:
        struct Slot {
            uint64_t freqHz = 0;
            int64_t timeNs = 0;
            int count = 0;
            std::vector<cf32> data;
        };
        std::vector<Slot> slots;
        int maxSamp = 1;
        std::atomic<uint32_t> head{ 0 };
        std::atomic<uint32_t> tail{ 0 };
        std::atomic<uint64_t> dropped{ 0 };
        std::atomic<bool> woken{ false };
        std::mutex mtx;
        std::condition_variable cv;
    };
}
