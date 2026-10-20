#ifndef WIFI_AP_H
#define WIFI_AP_H

// // 新增头文件 (需安装 ESPAsyncWebServer 和 AsyncTCP 库)
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <lwip/netdb.h>
#include <lwip/sockets.h>
#include "lwip/err.h"
#include "lwip/sys.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"

#include <freertos/semphr.h>
#include <esp_netif.h>
#include <esp_event_base.h>

#include <SW_struct.h>

// ====================== 新增全局配置 ======================
#define WS_PING_INTERVAL 30000 // 30秒发送一次Ping
#define WS_QUEUE_SIZE 10       // 每个客户端的最大待处理消息数
#define WS_TIMEOUT 10000       // 10秒无响应视为超时

#define DEVICE_CONNECTED_AP_BIT    BIT0
#define DEVICE_DISCONNECTED_AP_BIT BIT1
#define DEVICE_GET_IP_AP_BIT       BIT2

// ====================== WebSocket 事件处理器修改 ======================
namespace WiFiAP {
    static EventGroupHandle_t wifi_event_group = nullptr;     // 定义一个事件标志组

    void bufferInit();

    bool bufferAddPacket(const FusionPacket &packet);

    bool getCompleteBatch(FusionBatch &batch);

    void wifiInitSoftAP(void);

    static void wifi_event_handler(void *arg,
                                          esp_event_base_t event_base,
                                          int32_t event_id, void *event_data);

    // ====================== 服务端 初始化修改 ======================
    void initWebsocketServer();

    void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
                AwsEventType type, void *arg, uint8_t *data, size_t len);
}

#endif