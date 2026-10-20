#include "esp_sleep.h"
// #include <ulp_riscv.h>
// #include <ulp_riscv_i2c.h>
#include "driver/uart.h"
#include "esp_log.h"
#include <esp_attr.h>
#include <driver/gpio.h>
#include <hal/gpio_types.h>
#include <freertos/task.h>
#include <freertos/FreeRTOS.h>
#include <common_func.h>    
#include "EEPROM_Utils.h"

#include <stdio.h>
#include "Arduino.h"
#include "Wire.h"
#include <Initialize.h>
#include <TaskManager.h>
#include <SDCard.h>
#include <PowerManager.h>
#include <BLE.h>


#include "system_diagnostics.h"
#include "SC7A22H.h"

#include "test.h"

extern int file_counter;
//////////////////////////////////////////////////////////
// GPIO 唤醒
#define GPIO_WAKEUP_NUM         GPIO_NUM_6
/* "Boot" button is active low */
#define GPIO_WAKEUP_LEVEL       0
// Threshold LSBit is 4mg. Range is 0mg to 1020mg.
uint8_t WOM_threshold = 200;

esp_sleep_wakeup_cause_t cause = ESP_SLEEP_WAKEUP_UNDEFINED;
/////////////////////////////////////////////////////////
SemaphoreHandle_t taskMutex = xSemaphoreCreateMutex();  // IMU数据保护

/******************** 核心任务定义 *********************** */
// 函数原型声明
void taskStateManager(void *pvParam);
/******************************************************** */
extern "C" {

    extern const uint8_t ulp_main_bin_start[] asm("_binary_ulp_main_bin_start");
    extern const uint8_t ulp_main_bin_end[] asm("_binary_ulp_main_bin_end");
    }

/************************************************
 * RTC I2C utility APIs
 ************************************************/

volatile bool isrFired = false;

static void init_rtc_i2c(void);
static void init_ulp_program(void);

static void init_rtc_i2c(void)
{
    // /* Configure RTC I2C */
    // printf("Initializing RTC I2C ...\n");
    
    // ulp_riscv_i2c_pin_cfg_t i2c_pin_cfg;
    // i2c_pin_cfg.sda_io_num = GPIO_NUM_1;
    // i2c_pin_cfg.scl_io_num = GPIO_NUM_2;
    // i2c_pin_cfg.sda_pullup_en = true;
    // i2c_pin_cfg.scl_pullup_en = true;
    // ulp_riscv_i2c_timing_cfg_t i2c_timing_cfg;
    // i2c_timing_cfg.scl_low_period = 1.4;
    // i2c_timing_cfg.scl_high_period = 0.3;
    // i2c_timing_cfg.sda_duty_period = 1;
    // i2c_timing_cfg.scl_start_period = 2;
    // i2c_timing_cfg.scl_stop_period = 1.3;
    // i2c_timing_cfg.i2c_trans_timeout = 20;
    // ulp_riscv_i2c_cfg_t i2c_cfg;
    // i2c_cfg.i2c_pin_cfg = i2c_pin_cfg;
    // i2c_cfg.i2c_timing_cfg = i2c_timing_cfg;

    // esp_err_t ret = ulp_riscv_i2c_master_init(&i2c_cfg);
    // if (ret!= ESP_OK) {
    //     vTaskDelay(pdMS_TO_TICKS(100));
    // }
}

static void init_ulp_program(void)
{
    // esp_err_t err = ulp_riscv_load_binary(ulp_main_bin_start, (ulp_main_bin_end - ulp_main_bin_start));
    // ESP_ERROR_CHECK(err);

    // /* Start the program */
    // err = ulp_riscv_run();
    // ESP_ERROR_CHECK(err);
}

/********************************************************************************* */


void ulp_deep_sleep(){  
    // 初始化RTC I2C - 每次唤醒都需要重新初始化
    init_rtc_i2c();  
    if (cause != ESP_SLEEP_WAKEUP_ULP)
    {
        // 初始化ULP程序
        init_ulp_program();
    }
    esp_sleep_enable_ulp_wakeup();
}


void icmISR(void)
{
  isrFired = true; // Can't use I2C within ISR on 328p, so just set a flag to know that data is available
}

bool ext0_deep_sleep() {
    // 配置GPIO为上拉，确保默认高电平
    rtc_gpio_pulldown_dis((gpio_num_t)GPIO_WAKEUP_NUM);
    rtc_gpio_pulldown_en((gpio_num_t)GPIO_WAKEUP_NUM);

    imu.swReset();  
    imu.begin(Wire, 0);
    bool initialized = (imu.status == ICM_20948_Stat_Ok);
    if (!initialized) {
        imu.begin(Wire, 0);

        printf("Initialization of the sensor returned: ");
        printf(imu.statusString());
        if (imu.status != ICM_20948_Stat_Ok) {
            printf("Trying again...");
        } else {
            initialized = true;
        }
        imu.sleep(false);
    }

    imu.lowPower(false);
    if(imu.status != ICM_20948_Stat_Ok)
      {
        printf("lowPower returned: "); 
        imu.clearInterrupts();
      }                                                                                                        // 1
    if(imu.status != ICM_20948_Stat_Ok)
      {
        printf("swReset returned: "); 
        imu.clearInterrupts();
      }

    vTaskDelay(pdMS_TO_TICKS(5));

    ICM_20948_Status_e sampleModeStatus = imu.setSampleMode(ICM_20948_Internal_Acc, ICM_20948_Sample_Mode_Cycled);          // 2 
    if(imu.status != ICM_20948_Stat_Ok)
      {
        printf("setSampleMode returned: "); 
        imu.clearInterrupts();
      }
    ICM_20948_smplrt_t smplrt;
    smplrt.a = 22;
    imu.setSampleRate(ICM_20948_Internal_Acc, smplrt);
    if(imu.status != ICM_20948_Stat_Ok)
      {
        printf("setSampleRate returned: "); 
        imu.clearInterrupts();
      }
    // Set full scale ranges for both acc and gyr
    ICM_20948_fss_t fSS; 

    fSS.a = gpm2; // (ICM_20948_ACCEL_CONFIG_FS_SEL_e)
                    // gpm2
                    // gpm4
                    // gpm8
                    // gpm16

    imu.setFullScale(ICM_20948_Internal_Acc, fSS);                                                                             // 3
    if (imu.status != ICM_20948_Stat_Ok)
    {
        Serial.print(F("setFullScale returned: "));
        printf(imu.statusString());
    }

    vTaskDelay(pdMS_TO_TICKS(200)); // 等待100ms让传感器可能产生的新的中断稳定

    imu.cfgIntActiveLow(true);  
    if(imu.status != ICM_20948_Stat_Ok)
      {
        printf("cfgIntActiveLow returned: "); 
        imu.clearInterrupts();
      }
    imu.cfgIntOpenDrain(false);
    if(imu.status != ICM_20948_Stat_Ok)
      {
        printf("cfgIntActiveLow returned: "); 
        imu.clearInterrupts();
      }
    imu.cfgIntLatch(true);
    if(imu.status != ICM_20948_Stat_Ok)
      {
        printf("cfgIntLatch returned: "); 
        imu.clearInterrupts();
      }

    vTaskDelay(pdMS_TO_TICKS(50)); 
    imu.WOMLogic(true, 0); // enable WoM Logic mode 0                                                                           // 4
    if(imu.status != ICM_20948_Stat_Ok)
      {
        printf("womLogic returned: "); 
        imu.clearInterrupts();
      }
    vTaskDelay(pdMS_TO_TICKS(50)); 
    imu.WOMThreshold(WOM_threshold); // set WoM threshold                                                                        // 5
    if(imu.status != ICM_20948_Stat_Ok)
      {
        printf("WOMThreshold returned: "); 
        imu.clearInterrupts();
      }

    vTaskDelay(pdMS_TO_TICKS(100));
    imu.intEnableWOM(true); // enable interrupts on WakeOnMotion   
    if(imu.status != ICM_20948_Stat_Ok)
      {
        printf("intEnableWOM returned: "); 
        imu.clearInterrupts();
      }
                                                              
    vTaskDelay(pdMS_TO_TICKS(50)); 
    imu.WOMThreshold(WOM_threshold); // set WoM threshold    6
    if(imu.status != ICM_20948_Stat_Ok)
      {
        printf("WOMThreshold returned: "); 
        imu.clearInterrupts();
      }
    imu.lowPower(true);
    if(imu.status != ICM_20948_Stat_Ok)
      {
        printf("lowPower returned: "); 
        imu.clearInterrupts();
      }

    // 配置RTC参数
    rtc_config_t cfg = {
        .ck8m_wait = 20,      // 内部8M RC等待周期
        .xtal_wait = 150,     // 主晶振等待周期（增加稳定性）
        .pll_wait = 20,       // PLL等待周期
        .clkctl_init = 1,     // 启用时钟控制初始化
        .pwrctl_init = 1,     // 启用电源控制初始化
        .rtc_dboost_fpd = 0  
    };
    
    // 初始化RTC
    rtc_init(cfg);
    
    // 先设置时钟源为外部32K
    rtc_clk_slow_src_set(SOC_RTC_SLOW_CLK_SRC_XTAL32K);
    
    // 验证配置
    if(rtc_clk_slow_src_get() != SOC_RTC_SLOW_CLK_SRC_XTAL32K) {
        printf("使用备用时钟源");
    } else {
        printf("外部32K晶振已启用");
    }

    vTaskDelay(pdMS_TO_TICKS(100)); 

    // 在进入睡眠前检查引脚状态
    int pin_state = gpio_get_level((gpio_num_t)GPIO_WAKEUP_NUM);
    printf("GPIO %d state before sleep: %d\n", GPIO_WAKEUP_NUM, pin_state);

    // 使用EXT0唤醒替代GPIO唤醒
    if(rtc_gpio_is_valid_gpio(GPIO_WAKEUP_NUM)) {
        // 配置EXT0唤醒（使用RTC IO）
        esp_sleep_enable_ext0_wakeup((gpio_num_t)GPIO_WAKEUP_NUM, GPIO_WAKEUP_LEVEL);
        printf("EXT1 wakeup enabled on GPIO %d, level: %d\n", GPIO_WAKEUP_NUM, GPIO_WAKEUP_LEVEL);
    } else {
        printf("GPIO %d is not a valid RTC GPIO for EXT0 wakeup\n", GPIO_WAKEUP_NUM);
        return false;
    }

    return true;
}

bool ext1_deep_sleep(void)
{
    /*********************************************************************************/
#if ENABLE_SOC_ALERT_FLAG
    gpio_num_t gpio = GPIO_NUM_5;

    rtc_gpio_pullup_dis(gpio);     // 关闭内部上拉，避免和外部电阻叠加
    rtc_gpio_pulldown_dis(gpio);   // 不启用内部下拉，避免和外部上拉对拉

    // 先确认是 RTC GPIO
    if (!rtc_gpio_is_valid_gpio(gpio)) {
        printf("GPIO %d is not a valid RTC GPIO for EXT1 wakeup\n", gpio);
        return false;
    }

    int pin_state = gpio_get_level(gpio);
    printf("GPIO %d state before sleep: %d\n", gpio, pin_state);
#endif
    /*********************************************************************************/
#if ENABLE_ACCEL_FLAG
    motionSetting();
    gpio_num_t acc_gpio = GPIO_NUM_14;

    rtc_gpio_pullup_dis(acc_gpio);   
    rtc_gpio_pulldown_dis(acc_gpio); 

    // 先确认是 RTC GPIO
    if (!rtc_gpio_is_valid_gpio(acc_gpio)) {
        printf("GPIO %d is not a valid RTC GPIO for EXT1 wakeup\n", acc_gpio);
        return false;
    }

    int acc_pin_state = gpio_get_level(acc_gpio);
    printf("GPIO %d state before sleep: %d\n", acc_gpio, acc_pin_state);
#endif
    /*********************************************************************************/
    uint64_t io_mask = (1ULL << gpio) | (1ULL << acc_gpio);   // 关键：位掩码
    // uint64_t io_mask = (1ULL << acc_gpio);   // 关键：位掩码

    esp_err_t err = esp_sleep_enable_ext1_wakeup_io(
                        io_mask,
                        ESP_EXT1_WAKEUP_ANY_LOW  // 或 ESP_EXT1_WAKEUP_ANY_HIGH
                    );
    if (err != ESP_OK) {
        printf("EXT1 wakeup enable failed, err=0x%x\n", err);
        return false;
    }

    return true;
}


/********************************************************************************* */
void handle_reset_reason(void)
{
    // 获取复位原因
    esp_reset_reason_t reason = esp_reset_reason();

    // 根据复位原因分支处理
    switch (reason) {
        case ESP_RST_UNKNOWN:
            // 无法确定复位原因 
            ledState.store(eRED);
            break;

        case ESP_RST_POWERON:
            // 上电复位 - 设为绿色
            ledState.store(eGREEN);
            break;

        case ESP_RST_EXT:
            // 外部引脚复位（ESP32 无此功能，仅兼容）
            ledState.store(eBLUE);
            break;

        case ESP_RST_SW:
            // 软件复位（esp_restart()）
            ledState.store(ePURPLE);
            break;

        case ESP_RST_PANIC:
            // 异常/崩溃复位
            ledState.store(eYELLOW);
            //esp_restart();
            break;

        case ESP_RST_INT_WDT:
            // 中断看门狗复位
            ledState.store(eGREEN);
            break;

        case ESP_RST_TASK_WDT:
            // 任务看门狗复位 
            ledState.store(eRED);
            break;

        case ESP_RST_WDT:
            // 其他看门狗复位
            ledState.store(eBLUE);
            break;

        case ESP_RST_DEEPSLEEP:
            // 深度睡眠唤醒
            ledState.store(eBLUE);
            break;

        case ESP_RST_BROWNOUT:
            // 掉电检测复位 
            ledState.store(eRED);
            break;

        case ESP_RST_SDIO:
            // SDIO 复位 
            ledState.store(eWHITE);
            break;

        case ESP_RST_USB:
            // USB 外设复位 
            ledState.store(eWHITE);
            break;

        case ESP_RST_JTAG:
            // JTAG 复位 
            ledState.store(eGREEN);
            break;

        case ESP_RST_EFUSE:
            // eFuse 错误复位
            ledState.store(eRED);
            break;

        case ESP_RST_PWR_GLITCH:
            // 电源毛刺复位
            ledState.store(ePURPLE);
            break;

        case ESP_RST_CPU_LOCKUP:
            // CPU 锁死复位
            ledState.store(eRED);
            break;

        default:
            // 未知复位原因 - 设为关闭
            ledState.store(eBLUE);
            break;
    }
}

void preferences(){
    /// 初始化Preferences NVS
    prefs.begin("smart_wear", true);
    uint32_t bootCount = prefs.getUInt("boot_count", 0);
    bootCount++;
    prefs.putUInt("boot_count", bootCount);
    //printf("当前启动次数: %u\n", bootCount);
    BLE::g_isStartDeduction.store(prefs.getBool("run_state", false));
    if(BLE::g_isStartDeduction.load())
    {
        g_climbHeight.store(prefs.getFloat("climb_height", 0.0f));
        BLE::g_sportCount.store(prefs.getFloat("sport_laps", 0.0f));
        
        printf("run_state:%d", BLE::g_isStartDeduction.load()?1:0);                  
        printf("climb_height:%.2f", g_climbHeight.load());  
        printf("sport_laps:%.2f",BLE::g_sportCount.load());   
    }
    BLE::g_seaLevelPressure.store(prefs.getFloat("sea_level_pressure", 1013.25f));  
    file_counter = prefs.getInt("file_counter", 0);
    prefs.end();

    // 创建存储任务
    xTaskCreate(
        TaskManager::taskStorage,    // 任务函数
        "StorageTask",  // 任务名称
        4096,           // 堆栈大小
        NULL,           // 参数
        1,              // 优先级
        &TaskManager::taskStorageHandle  // 任务句柄
    );
    
    // 通知存储任务保存数据
    vTaskDelay(pdMS_TO_TICKS(300)); 
  if(TaskManager::taskStorageHandle != NULL) {
    xTaskNotifyGive(TaskManager::taskStorageHandle);
  }
}

void enterDeepSleep(){
    printf("Light-Sleep Mode Enter !");
    BLE::bleSuspend();
    BLE::closeBLE();
    if(BLE::commandTaskHandle)  
    {
        vTaskDelete(BLE::commandTaskHandle);
        BLE::commandTaskHandle=NULL;
    }

    PowerManager::enter_low_power_sleep_mode();

    // 配置深度睡眠
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);  

    ext1_deep_sleep();
#if ENABLE_IMU_FLAG
    ext0_deep_sleep();
#endif
    Serial.flush();
    vTaskDelay(pdMS_TO_TICKS(10));
    // 进入深度睡眠
    esp_deep_sleep_start(); 
}

void setup() {
    Serial.begin(115200);
    vTaskDelay(pdMS_TO_TICKS(100));
    printf("Hello from Arduino-ESP32 in ESP-IDF!");

    // 第1步：设置时区（必须在任何时间操作之前）
    setenv("TZ", "CST-8", 1);
    tzset();
    printf("时区设置为北京时间(CST-8)");

    // 初始化RGBLED
    init_RGBLED();
    vTaskDelay(pdMS_TO_TICKS(100)); 

    // 初始化片外RAM
    init_psram_buffers();

#if DMA_SUPPORT
    init_dma_driver();
#endif
    // NVS存储
    preferences();

    BLE::restoreTimeFromNVS();

    // 初始化并运行系统诊断
    system_diagnostics_init();
    // 1秒后打印统计报告
    vTaskDelay(pdMS_TO_TICKS(1000));
    print_reset_stats();

    pinMode(GPIO_NUM_14, INPUT);
    digitalWrite(GPIO_NUM_14, HIGH);

    pinMode(GPIO_NUM_5, INPUT);
    digitalWrite(GPIO_NUM_5, HIGH);

    // 充电使能
    pinMode(CE_PIN, OUTPUT); 
    digitalWrite(CE_PIN, HIGH);
#if ENABLE_ACCEL_FLAG
// 初始化加速度计
    init_Accel();
    vTaskDelay(pdMS_TO_TICKS(100));
#endif
    // 初始化IMU
    init_IMU();

    //初始化气压传感器
    init_MS5611();
    // 初始化电量传感器
    init_MAX17043();
    // 初始化AI推理
    init_AI();

#if ENABLE_DEDUCTION_FLAG
     // 初始化低功耗蓝牙
    init_BLE();

    // 创建命令处理任务（分配足够栈空间）
    xTaskCreatePinnedToCore(
        BLE::commandProcessorTask, 
        "CmdProcessor", 
        3072,  // 3KB栈空间
        nullptr, 
        3,     // 中等优先级
        &BLE::commandTaskHandle,
        1
    );

    vTaskDelay(pdMS_TO_TICKS(1000)); 
    xTaskCreate(TaskManager::taskBLEComms, "BLE", 4096, NULL, 2, &TaskManager::taskBLEHandle);

    esp_wifi_deinit();  // 卸载 Wi-Fi 驱动  
    esp_wifi_stop();    // 停止 Wi-Fi 服务（若已启动）

    PowerManager::init(); // 添加电源管理初始化
    PowerManager::configure_radio_power();
    PowerManager::configure_cpu_freq();
    // 修改电源管理回调注册
    PowerManager::set_device_callbacks([]() { TaskManager::TaskStateManager::resume_tasks(); },
                                []() { TaskManager::TaskStateManager::suspend_tasks(); });
    xTaskCreate(TaskManager::taskPowerCheck, "PowerCheck", 3072, NULL, 1, NULL);

#else
    // WiFi AP模式
    init_WiFiAP();
   xTaskCreate(TaskManager::taskWiFiComms, "WiFi", 4096, NULL, 4, NULL);
#endif
/*********************************************************************************/
    // 检查唤醒原因
    cause = esp_sleep_get_wakeup_cause();
    if (cause == ESP_SLEEP_WAKEUP_EXT0) {
        printf("\nEXT0 wakeup! Processing data...");
        ledState.store(ePURPLE);
    }else if(cause == ESP_SLEEP_WAKEUP_EXT1 ){
        printf("\nEXT1 wakeup! Processing data...");
        ledState.store(eGREEN);   
    } else if (cause == ESP_SLEEP_WAKEUP_ULP) {
        printf("\nULP wakeup! Processing data...");
        ledState.store(eRED);
    }else if(cause == ESP_SLEEP_WAKEUP_UNDEFINED){
        printf("\nundefined wakeup! Processing data...");
        ledState.store(eGRAY);      // 灰色
        //handle_reset_reason();
    }else if(cause == ESP_SLEEP_WAKEUP_COCPU_TRAP_TRIG){
        printf("\nCOCPU_TRAP_TRIG wakeup! Processing data...");
        ledState.store(eBLUE);
    }
    else {
        printf("\nCold start or non-ULP wakeup");
        // 这里可以执行首次启动的特殊初始化
        ledState.store(eGRAY);
    }
/*********************************************************************************/

    bool sd_init_success = init_sd_card();
    //init_usb_msc();

    vTaskDelay(pdMS_TO_TICKS(300)); 
    if(sd_init_success){

        xTaskCreate(taskStateManager, "StateMgr", 10240 , NULL, 3, NULL);
        sensorState = RUNNING;
    }
    else{
    vTaskDelay(pdMS_TO_TICKS(10000)); 
        FastLED.setBrightness(0); // 亮度（0-255）
        fill_solid(leds, NUM_LEDS, CRGB::Black);
        FastLED.show();

        // 配置深度睡眠
        esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);  

        ext1_deep_sleep();
#if ENABLE_IMU_FLAG
        ext0_deep_sleep();
#endif
        //Serial.flush();
        vTaskDelay(pdMS_TO_TICKS(10));
        // 进入深度睡眠
        esp_deep_sleep_start(); 

    }
}

void loop() {
    // 保持空，所有功能由 FreeRTOS 任务处理
    vTaskDelete(NULL); // 删除主循环任务
}

// 状态管理任务
void taskStateManager(void *pvParam) {
#if ENABLE_DEDUCTION_FLAG
    static uint32_t last_Wake_Time = xTaskGetTickCount();
    static uint32_t current_Wake_Time = xTaskGetTickCount();
#endif

    while (1) {
        switch (sensorState) {
        case CALIBRATING:
#if ENABLE_IMU_TYPE_MPU9250
            calibrationIMU();
#endif
            break;

        case RUNNING:
#if ENABLE_DEDUCTION_FLAG
            if(TaskManager::taskAIHandle == NULL){
                xTaskCreatePinnedToCore(TaskManager::taskAIInference, "AI", 4096, NULL, 2,
                                        &TaskManager::taskAIHandle, 1);
            }
#else
            xTaskCreatePinnedToCore(TaskManager::taskBatchSender, "BatchSend", 3072,
                                    NULL, 3, NULL, 1);
#endif
            // 确保传感器任务已启动
            if (TaskManager::taskFusionHandle == NULL) {
#if ENABLE_IMU_TYPE_MPU9250
                EEPROM_Utils::loadCalibration();
                EEPROM_Utils::printCalibration();
#endif
                xTaskCreatePinnedToCore(TaskManager::taskDataFusion, "Fusion",
                                        4096, NULL, 3,
                                        &TaskManager::taskFusionHandle, 0);
            }
            if(TaskManager::taskBatteryHandle == NULL){
                xTaskCreatePinnedToCore(TaskManager::taskBatteryCheck, "Battery", 4096, NULL, 1,
                                        &TaskManager::taskBatteryHandle, 1);
            }
            if(TaskManager::taskBaroHandle == NULL){
                xTaskCreatePinnedToCore(TaskManager::taskBaroData, "Baro", 4096, NULL, 2,
                                        &TaskManager::taskBaroHandle, 1);
            }
            if(TaskManager::taskIMUHandle == NULL){
                xTaskCreatePinnedToCore(TaskManager::taskIMUData, "IMU", 4096, NULL, 3,
                        &TaskManager::taskIMUHandle, 0);
            }
            if(TaskManager::taskFastLEDHandle == NULL){
                xTaskCreatePinnedToCore(TaskManager::taskRGBLED, "RGB_LED", 2048, NULL, 1,
                        &TaskManager::taskFastLEDHandle, 0);
            }
            break;

        case ERROR:
            // 停止所有任务
#if ENABLE_DEDUCTION_FLAG
            if (TaskManager::taskAIHandle) {
                vTaskDelete(TaskManager::taskAIHandle);
            }

            TaskManager::taskAIHandle = NULL;
#endif
            if (TaskManager::taskFusionHandle){
                vTaskDelete(TaskManager::taskFusionHandle);
            }
            if(TaskManager::taskBatteryHandle){
                vTaskDelete(TaskManager::taskBatteryHandle);
            }
            if(TaskManager::taskBaroHandle){
                vTaskDelete(TaskManager::taskBaroHandle);
            }
            if(TaskManager::taskIMUHandle){
                vTaskDelete(TaskManager::taskIMUHandle);
            }
            if(TaskManager::taskFastLEDHandle){
                vTaskDelete(TaskManager::taskFastLEDHandle);
            }

            TaskManager::taskFusionHandle   =   NULL;
            TaskManager::taskBatteryHandle   =   NULL;
            TaskManager::taskBaroHandle   =   NULL;
            TaskManager::taskIMUHandle   =   NULL;
            TaskManager::taskFastLEDHandle   =   NULL;        
            break;
        default:
                break;
            }

#if ENABLE_DEDUCTION_FLAG
        if (xSemaphoreTake(taskMutex, pdMS_TO_TICKS(200)) ==
            pdTRUE) {
            static bool isSuccess = false;
            static bool flag = false;
            if(BLE::bleState.load() != BLE::BLE_CONNECTED){                      // 1. 先保证蓝牙是不连接的
                if(!BLE::g_isStartDeduction.load())                              // 2. 并且目前不是运行状态
                {
                    if( battery_soc.load() > SOC_THRESHOLD){
                        current_Wake_Time = xTaskGetTickCount();
                        if((current_Wake_Time - last_Wake_Time > (1000*60))&& (!isSuccess)){ 
                            printf("Light-Sleep Mode Enter !");

                            last_Wake_Time = xTaskGetTickCount();
                            deinit_sd_card();
                            baro.sleep();
                            enterDeepSleep();
                        }
                    }
                }
                else{
                        last_Wake_Time = xTaskGetTickCount();
                }
            }
            else
            {
                last_Wake_Time = xTaskGetTickCount();
            }

            xSemaphoreGive(taskMutex);
        } 
#endif
        vTaskDelay(pdMS_TO_TICKS(50)); // 降低状态检查频率
    }
}
