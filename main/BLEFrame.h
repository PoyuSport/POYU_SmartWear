#pragma once

#include <cstddef>
#include <cstdint>

struct BLEFrame {
    uint8_t* data;
    size_t   len;

    BLEFrame(uint8_t* d, size_t l)
        : data(d), len(l) {}

    ~BLEFrame() {
        if (data) {
            delete[] data;
            data = nullptr;
        }
    }
};