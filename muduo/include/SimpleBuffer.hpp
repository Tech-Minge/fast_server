#pragma once

#include <vector>
#include "Utils.hpp"


class SimpleBuffer {
public:
    SimpleBuffer(size_t initialCapacity = 4096) {
        buffer_ = new char[initialCapacity];
        bufferSize_ = initialCapacity;
    }

    ~SimpleBuffer() {
        if (buffer_) {
            delete[] buffer_;
        }
    }

    void write(const char* data, size_t len) {
        ScopedTimer timer("BufferWrite");

        ensureCapacity(len);
        std::memcpy(buffer_ + writePos_, data, len);
        writePos_ += len;
    }

    void advance(size_t len) {
        readPos_ += len;
        if (noData()) {
            // 如果没有数据，重置位置指针，不清空 vector 内存
            readPos_ = 0;
            writePos_ = 0;
        } else if (readPos_ > bufferSize_ / 2) {
            // 如果读取位置太靠前，做一次滑动压缩
            compact();
        }
    }

    const char* data() const { return buffer_ + readPos_; }
    size_t size() const { return writePos_ - readPos_; }
    bool noData() const { return writePos_ == readPos_; }

private:
    char* buffer_;
    size_t bufferSize_ = 0;
    size_t readPos_ = 0;
    size_t writePos_ = 0;

    void ensureCapacity(size_t len) {
        size_t available = bufferSize_ - writePos_;
        if (available >= len) return;

        if (readPos_ + available >= len) {
            // 可以通过滑动腾出空间
            compact();
        } else {
            // 需要扩容，按 2 倍策略扩容
            size_t newSize = std::max(bufferSize_ * 2, writePos_ + len);
            bufferSize_ = newSize;
            char* newBuffer = new char[newSize];
            auto s = size();
            std::memcpy(newBuffer, buffer_ + readPos_, s);
            delete[] buffer_;
            buffer_ = newBuffer;
            readPos_ = 0;
            writePos_ = s;
        }
    }

    void compact() {
        if (readPos_ > 0) {
            size_t dataSize = size();
            std::memmove(buffer_, buffer_ + readPos_, dataSize);
            readPos_ = 0;
            writePos_ = dataSize;
        }
    }
};
