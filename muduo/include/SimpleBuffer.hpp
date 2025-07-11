#pragma once

#include <vector>

class SimpleBuffer {
public:
    SimpleBuffer() = default;
    ~SimpleBuffer() = default;

    void write(const char* data, size_t len) {
        buffer_.insert(buffer_.end(), data, data + len);
        writePos_ += len;
    }
    void advance(size_t len) {
        readPos_ += len;
        if (noData()) {
            // reset buffer size to 0
            std::vector<char>().swap(buffer_);
            readPos_ = 0;
            writePos_ = 0;
        }
    }

    const char* data() const { return buffer_.data() + readPos_; }
    size_t size() const { return writePos_ - readPos_; }
    bool noData() const { return writePos_ == readPos_; }

private:
    std::vector<char> buffer_;
    size_t readPos_ = 0;
    size_t writePos_ = 0;
};