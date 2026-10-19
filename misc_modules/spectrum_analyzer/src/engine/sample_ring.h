#pragma once
#include "sweep_types.h"
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>

namespace spectrum {
    // Bounded ring of IQ samples with an absolute sample counter. The producer
    // (receiver stream reader) never blocks: old samples are overwritten. The
    // consumer waits for absolute positions, which makes "discard N samples
    // after a retune, then take M samples" exact even when it falls behind.
    class SampleRing {
    public:
        void configure(size_t capacity) {
            std::lock_guard<std::mutex> lck(mtx);
            buf.assign(std::max<size_t>(capacity, 16), cf32(0, 0));
            total = 0;
            want = 0;
            isClosed = false;
        }

        void write(const cf32* s, int n) {
            if (n <= 0) { return; }
            {
                std::lock_guard<std::mutex> lck(mtx);
                const size_t cap = buf.size();
                if ((size_t)n >= cap) {
                    s += n - cap;
                    total += n - cap;
                    n = (int)cap;
                }
                size_t pos = total % cap;
                size_t first = std::min((size_t)n, cap - pos);
                std::memcpy(&buf[pos], s, first * sizeof(cf32));
                if ((size_t)n > first) { std::memcpy(&buf[0], s + first, (n - first) * sizeof(cf32)); }
                total += n;
            }
            cv.notify_all();
        }

        uint64_t written() {
            std::lock_guard<std::mutex> lck(mtx);
            return total;
        }

        // Highest position a consumer is waiting for (lets on-demand producers idle).
        uint64_t demand() {
            std::lock_guard<std::mutex> lck(mtx);
            return want;
        }

        bool waitFor(uint64_t target, int timeoutMs) {
            std::unique_lock<std::mutex> lck(mtx);
            if (target > want) { want = target; }
            cvDemand.notify_all();
            return cv.wait_for(lck, std::chrono::milliseconds(timeoutMs), [&] { return total >= target || isClosed; }) && total >= target;
        }

        // Producer side: waits until a consumer asks for more data.
        bool waitForDemand(int timeoutMs) {
            std::unique_lock<std::mutex> lck(mtx);
            return cvDemand.wait_for(lck, std::chrono::milliseconds(timeoutMs), [&] { return want > total || isClosed; }) && !isClosed;
        }

        // Copies n samples starting at absolute position `from`. If they were
        // already overwritten, the newest n samples are copied instead (still
        // after `from`). Returns false if fewer than n samples exist after `from`.
        bool copy(uint64_t from, int n, cf32* dst) {
            std::lock_guard<std::mutex> lck(mtx);
            const size_t cap = buf.size();
            if ((size_t)n > cap || total < from + (uint64_t)n) { return false; }
            uint64_t oldest = (total > cap) ? total - cap : 0;
            if (from < oldest) { from = total - n; }
            size_t pos = from % cap;
            size_t first = std::min((size_t)n, cap - pos);
            std::memcpy(dst, &buf[pos], first * sizeof(cf32));
            if ((size_t)n > first) { std::memcpy(dst + first, &buf[0], (n - first) * sizeof(cf32)); }
            return true;
        }

        void close() {
            {
                std::lock_guard<std::mutex> lck(mtx);
                isClosed = true;
            }
            cv.notify_all();
            cvDemand.notify_all();
        }

        bool closed() {
            std::lock_guard<std::mutex> lck(mtx);
            return isClosed;
        }

        size_t capacity() const { return buf.size(); }

    private:
        std::mutex mtx;
        std::condition_variable cv;
        std::condition_variable cvDemand;
        std::vector<cf32> buf;
        uint64_t total = 0;
        uint64_t want = 0;
        bool isClosed = false;
    };
}
