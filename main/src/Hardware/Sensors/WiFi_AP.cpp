#include <WiFi_AP.h>
#include <freertos/event_groups.h>
#include <esp_timer.h>
#include <esp_wifi_types_generic.h>

// // 改为 AP 模式配置
#define AP_SSID      "ESP32Hotspot"
#define AP_PASS      "11111111" 

static const char *TAG = "wifi softAP";

// // ====================== WebSocket 配置 ======================
AsyncWebServer server(80);
AsyncWebSocket ws("/ws"); // WebSocket 端点

namespace WiFiAP {

    typedef struct wifi_sta_connect_Info
    {
        esp_ip4_addr_t dev_ip;      // 客户端设备连接AP后获取的IP
        uint8_t dev_mac[6];         // 客户端设备MAC地址
        uint8_t ap_ssid[32];        // AP的名称
        uint8_t ap_password[64];    // AP的密码
    } wifi_ap_connect_info;

    wifi_ap_connect_info wifi_ap_net_info = {0};
    static int s_retry_num = 0;     // 重连目标AP的次数


void bufferInit() {
    batchBuffer.mutex = xSemaphoreCreateMutex();
    if (batchBuffer.mutex == NULL) {
        ESP_LOGE(TAG, "无法创建批处理缓冲区互斥量！系统重启中...");
        esp_restart(); // 或进入安全模式
    }
    }

    bool bufferAddPacket(const FusionPacket &packet) {
        if (xSemaphoreTake(batchBuffer.mutex, pdMS_TO_TICKS(2)) != pdTRUE)
            return false;

        // 获取当前批次索引
        uint8_t batch_idx = batchBuffer.wr_ptr / BATCH_SIZE;
        uint8_t packet_idx = batchBuffer.wr_ptr % BATCH_SIZE;

        // 填充数据包
        batchBuffer.batches[batch_idx].packets[packet_idx] = packet;
        batchBuffer.wr_ptr = (batchBuffer.wr_ptr + 1) % (BUFFER_DEPTH * BATCH_SIZE);

        // 当批次填满时标记
        if (packet_idx == BATCH_SIZE - 1) {
            batchBuffer.batches[batch_idx].timestamp = esp_timer_get_time() / 1000;
            batchBuffer.batches[batch_idx].sequence = batchBuffer.sequence++;
        }
        xSemaphoreGive(batchBuffer.mutex);
        return true;
    }

    bool getCompleteBatch(FusionBatch &batch) {
        if (xSemaphoreTake(batchBuffer.mutex, pdMS_TO_TICKS(5)) != pdTRUE)
            return false;

        // 检查是否有完整批次
        uint16_t total_slots = BUFFER_DEPTH * BATCH_SIZE;
        uint16_t available =
            (batchBuffer.wr_ptr >= batchBuffer.rd_ptr)
                ? (batchBuffer.wr_ptr - batchBuffer.rd_ptr) / BATCH_SIZE
                : (total_slots - (batchBuffer.rd_ptr - batchBuffer.wr_ptr)) /BATCH_SIZE;
        if (available == 0) {
            xSemaphoreGive(batchBuffer.mutex);
            return false;
        }

        // 取出最早完整批次
        uint8_t batch_idx = batchBuffer.rd_ptr / BATCH_SIZE;
        memcpy(&batch, &batchBuffer.batches[batch_idx], sizeof(FusionBatch));
        batchBuffer.rd_ptr =(batchBuffer.rd_ptr + BATCH_SIZE) % (BUFFER_DEPTH * BATCH_SIZE);

        xSemaphoreGive(batchBuffer.mutex);
        return true;
    }

    static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                        int32_t event_id, void* event_data)
    {
        if (event_id == WIFI_EVENT_AP_STACONNECTED) {
            wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
            memcpy(wifi_ap_net_info.dev_mac,event->mac,sizeof(event->mac));
            xEventGroupSetBits(wifi_event_group,DEVICE_CONNECTED_AP_BIT);
            ESP_LOGI(TAG, "station "MACSTR" join, AID=%d",
                    MAC2STR(event->mac), event->aid);
        } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
            wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
            memcpy(wifi_ap_net_info.dev_mac,event->mac,sizeof(event->mac));
            ESP_LOGI(TAG, "station "MACSTR" leave, AID=%d, reason=%d",
                    MAC2STR(event->mac), event->aid, event->reason);
            xEventGroupSetBits(wifi_event_group,DEVICE_DISCONNECTED_AP_BIT);
        }else if(event_id == IP_EVENT_AP_STAIPASSIGNED){
            ip_event_ap_staipassigned_t *eventData =
                (ip_event_ap_staipassigned_t *)event_data;
            memcpy(wifi_ap_net_info.dev_mac,eventData->mac,6);
            wifi_ap_net_info.dev_ip = eventData->ip;
            xEventGroupSetBits(wifi_event_group, DEVICE_GET_IP_AP_BIT);
            // ESP_LOGI(TAG,"DHCP server assigned IP to a client : MAC ="MACSTR",IP="IPSTR"",MAC2STR(event_data->mac),IP2STR(event_data->aid));
        }else
        {
            ESP_LOGI(TAG,"Unexpected event ! event_id = %ld \n",event_id);
        }
    }

    void wifiInitSoftAP(void)
    {
        // 1. 创建新的事件循环
        ESP_ERROR_CHECK(esp_event_loop_create_default());
        // 2. 初始化WIFI连接/断开的标志事件组
        wifi_event_group = xEventGroupCreate();
        if (wifi_event_group == nullptr) {
            ESP_LOGE(TAG, "事件组创建失败：内存不足！");
            return; // 或执行错误处理
        }
        // 3. 注册事件ID和IP获取服务函数
        ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                            ESP_EVENT_ANY_ID,
                                                            &wifi_event_handler,
                                                            NULL,
                                                            NULL));

        ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                            ESP_EVENT_ANY_ID,
                                                            &wifi_event_handler,
                                                            NULL,
                                                            NULL));
        // 4. 网卡初始化，初始化底层TCP/IP堆栈
        ESP_ERROR_CHECK(esp_netif_init());
        // 5. 使用默认WiFi Station配置创建esp_netif对象，将netif连接到WiFi并注册默认WiFi处理函数
        esp_netif_t *ap_netif = esp_netif_create_default_wifi_ap();
        assert(ap_netif);
        // 6. netif连接到WiFi并注册默认WiFi处理程序
        esp_netif_set_default_netif(ap_netif);

        // 7. 初始化WiFi配置参数为默认
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        // 8. 为WiFi驱动初始化WiFi分配资源，如WiFi控制结构、RX/TX缓冲区、WiFi NVS结构等。这个WiFi也
        // 启动 WiFi 任务。必须先调用次API,然后先调用此API，然后才能调用所有其他WiFi API
        ESP_ERROR_CHECK(esp_wifi_init(&cfg));

        wifi_config_t wifi_config = {
            .ap = {
                .ssid = AP_SSID,
                .password = AP_PASS,
                 .ssid_len = strlen(AP_SSID),
                 .channel = 1,
    #ifdef CONFIG_ESP_WIFI_SOFTAP_SAE_SUPPORT
                .authmode = wifi_auth_mode_t::WIFI_AUTH_WPA2_PSK,
    #else /* CONFIG_ESP_WIFI_SOFTAP_SAE_SUPPORT */
                .authmode = wifi_auth_mode_t::WIFI_AUTH_WPA2_PSK,
    #endif
                .max_connection = 4,
                .pmf_cfg = {
                        .required = true,
                },
            },
        };

        if (strlen(AP_PASS) == 0) {
            wifi_config.ap.authmode = WIFI_AUTH_OPEN;
        }

        // 9. 设置WIFI为AP模式
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
        // 10. 设置WIFI AP模式的配置
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
        // 11. 启动WIFI
        ESP_ERROR_CHECK(esp_wifi_start());

        ESP_LOGI(TAG, "wifi_init_softap finished. SSID:%s password:%s channel:%d",
                AP_SSID, AP_PASS, 1);
    }

    void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
                AwsEventType type, void *arg, uint8_t *data, size_t len) {
        switch (type) {
        case WS_EVT_CONNECT: {
            //ESP_LOGI("WIFI", "[WebSocket] 客户端 #%u 连接, IP: %s", client->id(),
            //          client->remoteIP().toString().c_str());
            break;
        }
        case WS_EVT_DISCONNECT:
            //ESP_LOGI("WIFI", "[WebSocket] 客户端 #%u 断开", client->id());
            break;

        case WS_EVT_DATA: {
            AwsFrameInfo *info = (AwsFrameInfo *)arg;
            // MODIFIED: 处理分片消息
            static uint8_t buffer[2048]; // 分片缓冲区
            static size_t total_len = 0;

            if (info->index == 0) {
                total_len = 0; // 新消息开始
            }

            // 检查缓冲区溢出
            if (total_len + len > sizeof(buffer)) {
                ESP_LOGE("WIFI", "消息过长，丢弃分片");
                total_len = 0;
                break;
            }

            memcpy(buffer + total_len, data, len);
            total_len += len;

            if (info->final && total_len > 0) {
                String response = "ACK"; // 示例响应
                client->text(response.c_str(), response.length());
                total_len = 0; // 重置缓冲区
            }
            break;
        }

        case WS_EVT_ERROR:
            // MODIFIED: 解析详细错误码
            ESP_LOGE("WIFI", "[WebSocket] 错误: %s (代码: %u)", (char *)data,
                    *((uint16_t *)arg));
            break;

        case WS_EVT_PONG:
            // ADDED: 监控Pong响应
            ESP_LOGD("WIFI", "收到Pong响应");
            break;
        }
    }

    // ====================== WiFi 初始化修改 ======================
    void initWebsocketServer() {
        // WebSocket 初始化
        ws.onEvent(onWsEvent);
        server.addHandler(&ws);

        // HTTP 基础服务
        server.on("/hello", HTTP_GET, [](AsyncWebServerRequest *request) {
            request->send(200, "text/plain", "ESP32-S3");
        });

        server.onNotFound([](AsyncWebServerRequest* request) {
            request->send(404, "text/plain", "Not Found");
        });

        server.begin();
    }
}
