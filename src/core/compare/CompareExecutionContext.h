#pragma once
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <functional>
#include <limits>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <utility>

namespace adayo {
struct CompareExecutionContext {
    static constexpr std::size_t budget_bytes=512ull*1024*1024;
    static constexpr std::size_t file_bytes=64ull*1024*1024;
    static constexpr std::size_t record_codepoints=65536;
    std::stop_token stop;
    std::function<void(std::size_t,std::size_t)> progress;
    std::size_t used_bytes=8ull*1024*1024, peak_bytes=used_bytes;
    std::chrono::steady_clock::time_point last_progress{};
    struct Reservation {
        CompareExecutionContext* owner{};
        std::size_t bytes{};
        Reservation(CompareExecutionContext* context,std::size_t count):owner(context),bytes(count){}
        Reservation(const Reservation&)=delete;
        Reservation& operator=(const Reservation&)=delete;
        Reservation(Reservation&& other) noexcept:owner(std::exchange(other.owner,nullptr)),bytes(other.bytes){}
        ~Reservation(){if(owner) owner->used_bytes-=bytes;}
    };
    static std::size_t Multiply(std::size_t a,std::size_t b) {
        if(b && a>std::numeric_limits<std::size_t>::max()/b) throw std::runtime_error("Compare memory size overflow");
        return a*b;
    }
    static std::size_t Add(std::size_t a,std::size_t b) {
        if(a>std::numeric_limits<std::size_t>::max()-b) throw std::runtime_error("Compare memory size overflow");
        return a+b;
    }
    Reservation Reserve(std::size_t bytes,const char* stage) {
        Check();
        if(bytes>budget_bytes-used_bytes) throw std::runtime_error(std::string("Compare 512MiB memory budget exceeded: ")+stage);
        used_bytes+=bytes; peak_bytes=(std::max)(peak_bytes,used_bytes);
        return Reservation(this,bytes);
    }
    void Check(std::size_t done=0,std::size_t total=0) {
        if(stop.stop_requested()) throw std::runtime_error("COMPARE_CANCELED");
        const auto now=std::chrono::steady_clock::now();
        if(progress && total && (done==total || now-last_progress>=std::chrono::milliseconds(100))) {
            last_progress=now; progress(done,total);
        }
    }
};
} // namespace adayo
