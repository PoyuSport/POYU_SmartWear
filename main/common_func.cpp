#include "common_func.h"
#include "esp_private/cache_utils.h"
#include "Arduino.h"

async_memcpy_handle_t memcpy_driver = NULL;

void init_dma_driver() {
    if (memcpy_driver) return;
    
    async_memcpy_config_t config = ASYNC_MEMCPY_DEFAULT_CONFIG();
    config.backlog = 8;
    
    // 显式配置PSRAM支持
#if CONFIG_SPIRAM
    config.psram_trans_align = 64;
    config.sram_trans_align = 64;
    //config.flags |= ASYNC_MEMCPY_PSRAM_DEFAULT;
    ESP_LOGI("DMA", "Configuring for PSRAM support");
#endif

    esp_err_t ret = esp_async_memcpy_install(&config, &memcpy_driver);
    if (ret != ESP_OK) {
        printf( "Driver install failed: %s", esp_err_to_name(ret));
    }
}

void dma_memcpy(void *dst, void *src, size_t n) {
#if DMA_SUPPORT
    // 1. 检查驱动初始化
    if (memcpy_driver == NULL) {
        printf( "Driver not initialized! Call init_dma_driver() first");
        return; // 非致命错误可改为返回错误码
    }
    
    // 2. 检查NULL指针
    if (dst == NULL || src == NULL) {
        printf( "NULL pointer! dst=%p src=%p", dst, src);
        return;
    }
    
    // 3. 检查对齐要求（根据内存类型）
    uintptr_t dst_addr = reinterpret_cast<uintptr_t>(dst);
    uintptr_t src_addr = reinterpret_cast<uintptr_t>(src);
    
    // 确定内存类型
    bool dst_is_psram = heap_caps_get_allocated_size(dst) == MALLOC_CAP_SPIRAM;
    bool src_is_psram = heap_caps_get_allocated_size(src) == MALLOC_CAP_SPIRAM;
    
    size_t align_required = 4;
    if (dst_is_psram || src_is_psram) {
        align_required = 64; // PSRAM 需要64字节对齐
    }
    
    // 4. 对齐检查
    if ((dst_addr % align_required) != 0 ||
        (src_addr % align_required) != 0 ||
        (n % align_required) != 0) 
    {
        printf( "Alignment error! Require %d-byte align", align_required);
        printf( "dst: %p (%s), src: %p (%s), size: %zu",
                 dst, dst_is_psram ? "PSRAM" : "SRAM",
                 src, src_is_psram ? "PSRAM" : "SRAM",
                 n);
        return;
    }
    
    // 5. 边界检查（可选）
    size_t dst_size = heap_caps_get_allocated_size(dst);
    size_t src_size = heap_caps_get_allocated_size(src);
    
    if (n > dst_size || n > src_size) {
        printf( "Size overflow! dst_size=%zu src_size=%zu n=%zu",
                 dst_size, src_size, n);
        return;
    }
    
    // 6. 带错误处理的API调用
    esp_err_t ret = esp_async_memcpy(memcpy_driver, dst, src, n, NULL, NULL);
    if (ret != ESP_OK) {
        printf( "Copy failed: %s (0x%x)", esp_err_to_name(ret), ret);
    }

#else
    // 7. 普通memcpy作为备选
    memcpy(dst, src, n);
#endif
}


float sanitizeValue(float value) {
    if (!std::isfinite(value)) {  // 检查NaN和INF
        printf("Non-finite value: %f, replacing with 0", value);
        return 0.0f;
    }
    return value;
}

int8_t QuantizeToInt8(float value,
                             float scale,
                             int zero_point) {
    int32_t q = static_cast<int32_t>(std::round(value / scale)) + zero_point;
    if (q > 127) q = 127;
    if (q < -128) q = -128;
    return static_cast<int8_t>(q);
}