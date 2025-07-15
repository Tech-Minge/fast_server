#pragma once

#include <vector>
#include "Utils.hpp"
// class SimpleBuffer {
// public:
//     SimpleBuffer() = default;
//     ~SimpleBuffer() = default;

//     void write(const char* data, size_t len) {
//         ScopedTimer timer("BufferWrite");

//         // use memcpy
//         buffer_.resize(writePos_ + len);
//         std::memcpy(buffer_.data() + writePos_, data, len);
//         writePos_ += len;
//     }
//     void advance(size_t len) {
//         readPos_ += len;
//         if (noData()) {
//             // reset buffer size to 0
//             buffer_.resize(0);
//             readPos_ = 0;
//             writePos_ = 0;
//         }
//     }

//     const char* data() const { return buffer_.data() + readPos_; }
//     size_t size() const { return writePos_ - readPos_; }
//     bool noData() const { return writePos_ == readPos_; }

// private:
//     std::vector<char> buffer_;
//     size_t readPos_ = 0;
//     size_t writePos_ = 0;
// };


class SimpleBuffer {
public:
    SimpleBuffer(size_t initialCapacity = 1024) {
        buffer_.resize(initialCapacity);
    }

    ~SimpleBuffer() = default;

    void write(const char* data, size_t len) {
        ScopedTimer timer("BufferWrite");

        ensureCapacity(len);
        std::memcpy(buffer_.data() + writePos_, data, len);
        writePos_ += len;
    }

    void advance(size_t len) {
        readPos_ += len;
        if (noData()) {
            // 如果没有数据，重置位置指针，不清空 vector 内存
            readPos_ = 0;
            writePos_ = 0;
        } else if (readPos_ > buffer_.size() / 2) {
            // 如果读取位置太靠前，做一次滑动压缩
            compact();
        }
    }

    const char* data() const { return buffer_.data() + readPos_; }
    size_t size() const { return writePos_ - readPos_; }
    bool noData() const { return writePos_ == readPos_; }

private:
    std::vector<char> buffer_;
    size_t readPos_ = 0;
    size_t writePos_ = 0;

    void ensureCapacity(size_t len) {
        size_t available = buffer_.size() - writePos_;
        if (available >= len) return;

        if (readPos_ + available >= len) {
            // 可以通过滑动腾出空间
            compact();
        } else {
            // 需要扩容，按 2 倍策略扩容
            size_t newSize = std::max(buffer_.size() * 2, writePos_ + len);
            buffer_.resize(newSize);
        }
    }

    void compact() {
        if (readPos_ > 0) {
            size_t dataSize = size();
            std::memmove(buffer_.data(), buffer_.data() + readPos_, dataSize);
            readPos_ = 0;
            writePos_ = dataSize;
        }
    }
};
