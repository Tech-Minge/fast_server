#pragma once
#include <vector>
#include <cstdint>
#include <stdexcept>
#include <cstring>
#include <arpa/inet.h>

size_t encoder(uint8_t type, const char* data, uint32_t data_len, char* output, size_t out_size) {
    if (out_size < 5 + data_len || !output)
        return 0;

    output[0] = static_cast<char>(type);

    std::memcpy(output + 1, &data_len, 4);

    for (uint32_t i = 0; i < data_len; ++i) {
        output[5 + i] = static_cast<char>(data[i] + (i % 2 ? 0x51 : 0x4A));
    }
    return 5 + data_len;
}

void decoder(const char* input, size_t in_size, uint8_t* out_type, char* out_data, uint32_t* out_len) {
    if (in_size < 5 || !input || !out_type || !out_data || !out_len) 
        return;

    *out_type = static_cast<uint8_t>(input[0]);

    uint32_t net_len;
    std::memcpy(&net_len, input + 1, 4);
    *out_len = net_len;

    if (in_size < 5 + *out_len)
        return;

    for (uint32_t i = 0; i < *out_len; ++i) {
        uint8_t byte = static_cast<uint8_t>(input[5 + i]);
        out_data[i] = byte - (i % 2 ? 0x51 : 0x4A);
    }
}