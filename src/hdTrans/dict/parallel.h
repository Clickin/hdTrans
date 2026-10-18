#pragma once
// Loading runs independent parts on other threads where the platform has threads;
// without them everything runs on the calling thread.
#include <algorithm>
#include <cstddef>
#include <utility>
#include <vector>

#if !defined(__EMSCRIPTEN__) || defined(__EMSCRIPTEN_PTHREADS__)
#define HDTRANS_DICT_THREADS 1
#include <thread>
#endif

namespace hdTrans::dict::internal {

// Runs a function on another thread and waits for it in Wait or the destructor.
class Background {
public:
    template <class F>
    explicit Background(F&& f) {
#ifdef HDTRANS_DICT_THREADS
        thread_ = std::thread(std::forward<F>(f));
#else
        f();
#endif
    }
    Background(const Background&) = delete;
    Background& operator=(const Background&) = delete;
    ~Background() { Wait(); }

    void Wait() {
#ifdef HDTRANS_DICT_THREADS
        if (thread_.joinable()) thread_.join();
#endif
    }

private:
#ifdef HDTRANS_DICT_THREADS
    std::thread thread_;
#endif
};

// Calls f(part, begin, end) for consecutive parts of [0, n) at once, at most four,
// and returns when all have returned.
template <class F>
void ParallelFor(std::size_t n, F&& f) {
#ifdef HDTRANS_DICT_THREADS
    std::size_t parts = std::clamp<std::size_t>(std::thread::hardware_concurrency(), 1, 4);
    parts = std::min(parts, n / 4096 + 1);
    std::vector<std::thread> others;
    others.reserve(parts - 1);
    for (std::size_t p = 1; p < parts; p++) {
        others.emplace_back([&f, p, n, parts] { f(p, n * p / parts, n * (p + 1) / parts); });
    }
    f(std::size_t{0}, std::size_t{0}, n / parts);
    for (std::thread& t : others) t.join();
#else
    f(std::size_t{0}, std::size_t{0}, n);
#endif
}

}  // namespace hdTrans::dict::internal
