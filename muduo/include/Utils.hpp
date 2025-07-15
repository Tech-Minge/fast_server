#pragma once
#include <chrono>
#include "spdlog/spdlog.h"


class ScopedTimer {
public:
    ScopedTimer(const std::string& func_name) 
        : name(func_name), start(std::chrono::steady_clock::now()) {}
    
    ~ScopedTimer() {
        auto end = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        spdlog::warn("{} cost: {}us", name, duration.count());
    }

private:
    std::string name;
    std::chrono::steady_clock::time_point start;
};

class Spinlock {
private:
  std::atomic<bool> lock_ = {0};
public:
  void lock() noexcept {
    for (;;) {
      if (!lock_.exchange(true, std::memory_order_acquire)) {
        return;
      }
      // Wait for lock to be released without generating cache misses
      while (lock_.load(std::memory_order_relaxed)) {
        
      }
    }
  }

  void unlock() noexcept {
    lock_.store(false, std::memory_order_release);
  }
};



/*
#include <chrono>
#include <string>
#include <iomanip>
#include <sstream>
#include <ctime>
#include <spdlog/spdlog.h>

class ScopedTimer {
public:
    ScopedTimer(const std::string& func_name) 
        : name(func_name), start(std::chrono::system_clock::now()) {}
    
    ~ScopedTimer() {
        auto end = std::chrono::system_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        
        // 格式化时间输出
        spdlog::warn("{} cost: {}us (start {}, end {})", 
                    name, 
                    duration.count(),
                    format_time(start),
                    format_time(end));
    }

private:
    // 将时间点格式化为可读字符串 (14点56分30.123456秒)
    static std::string format_time(const std::chrono::system_clock::time_point& tp) {
        // 转换为time_t和微秒部分
        auto t = std::chrono::system_clock::to_time_t(tp);
        auto since_epoch = tp.time_since_epoch();
        auto secs = std::chrono::duration_cast<std::chrono::seconds>(since_epoch);
        auto micros = std::chrono::duration_cast<std::chrono::microseconds>(since_epoch - secs);
        
        // 转换为本地时间
        std::tm tm;
        #ifdef _WIN32
            localtime_s(&tm, &t);  // Windows线程安全版本
        #else
            localtime_r(&t, &tm);   // POSIX线程安全版本
        #endif
        
        // 格式化为字符串
        std::ostringstream oss;
        oss << std::put_time(&tm, "%H点%M分")              // 小时和分钟 (14点56分)
            << tm.tm_sec << "."                             // 秒
            << std::setfill('0') << std::setw(6)            // 微秒6位补零
            << micros.count() << "秒";                     // 123456秒
        
        return oss.str();
    }

private:
    std::string name;
    std::chrono::system_clock::time_point start;
};
*/