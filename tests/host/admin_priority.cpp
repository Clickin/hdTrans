#include "hdTrans/hdtrans.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

class RoundDeadline {
public:
    explicit RoundDeadline(int round) : watcher_([this, round] {
        std::unique_lock<std::mutex> lock(mutex_);
        if (!done_.wait_for(lock, std::chrono::seconds(5), [this] { return finished_; })) {
            std::fprintf(stderr, "FAIL admin round %d exceeded 5 s under continuous translation\n", round);
            std::fflush(stderr);
            std::_Exit(EXIT_FAILURE);
        }
    }) {}

    ~RoundDeadline() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            finished_ = true;
        }
        done_.notify_one();
        watcher_.join();
    }

private:
    std::mutex mutex_;
    std::condition_variable done_;
    bool finished_ = false;
    std::thread watcher_;
};

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::string dirs(argv[1]);
    dirs.append(2, '\0');
    if (hdTrans_Initialize(dirs.c_str()) != 0) {
        std::fprintf(stderr, "FAIL initialize fixture\n");
        return 1;
    }
    std::string input;
    for (int i = 0; i < 32; ++i) input += "学生。\n";
    char* baseline = nullptr;
    std::size_t baselineSize = 0;
    if (hdTrans_Translate(input.data(), input.size(), &baseline, &baselineSize) != 0 || !baseline || !baselineSize) {
        std::fprintf(stderr, "FAIL baseline translation\n");
        hdTrans_FreeMem(baseline);
        hdTrans_Terminate();
        return 1;
    }
    const std::string expected(baseline, baselineSize);
    hdTrans_FreeMem(baseline);

    std::atomic<bool> start{false}, stop{false}, failed{false};
    std::atomic<unsigned> ready{0};
    std::atomic<unsigned long> translated{0}, unloaded{0};
    std::vector<std::thread> workers;
    for (int i = 0; i < 8; ++i) {
        workers.emplace_back([&] {
            ++ready;
            while (!start) std::this_thread::yield();
            while (!stop) {
                char* out = nullptr;
                std::size_t size = 0;
                const int status = hdTrans_Translate(input.data(), input.size(), &out, &size);
                if (status == 0) {
                    if (!out || size != expected.size() || std::memcmp(out, expected.data(), size) != 0 || out[size] != '\0')
                        failed = true;
                    ++translated;
                } else if (status == -1) {
                    if (out || size) failed = true;
                    ++unloaded;
                } else {
                    failed = true;
                }
                hdTrans_FreeMem(out);
            }
        });
    }
    while (ready != 8) std::this_thread::yield();
    start = true;
    while (translated < 16) std::this_thread::yield();
    std::printf("eight tight translators, six admin rounds, 5 s deadline per round\n");
    for (int round = 1; round <= 6; ++round) {
        RoundDeadline deadline(round);
        const auto before = std::chrono::steady_clock::now();
        if (hdTrans_Reload() != 0) failed = true;
        if (hdTrans_Initialize(dirs.c_str()) != 0) failed = true;
        hdTrans_Terminate();
        hdTrans_Terminate();
        if (hdTrans_Reload() != -1) failed = true;
        if (hdTrans_Initialize(dirs.c_str()) != 0) failed = true;
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - before).count();
        std::printf("admin round %d completed in %.3f ms\n", round, ms);
    }
    stop = true;
    for (auto& worker : workers) worker.join();
    hdTrans_Terminate();
    char* out = nullptr;
    std::size_t size = 0;
    if (hdTrans_Translate(input.data(), input.size(), &out, &size) != -1 || out || size) failed = true;
    hdTrans_FreeMem(out);
    std::printf("%s admin priority: translations=%lu, uninitialized calls=%lu\n",
                failed ? "FAIL" : "PASS", translated.load(), unloaded.load());
    return failed ? 1 : 0;
}
