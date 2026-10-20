#include "esp_async_memcpy.h"
#include <cmath>

extern async_memcpy_handle_t memcpy_driver;

void init_dma_driver();

void dma_memcpy(void *dst, void *src, size_t n);

float sanitizeValue(float value); 

int8_t QuantizeToInt8(float value,
                             float scale,
                             int zero_point);