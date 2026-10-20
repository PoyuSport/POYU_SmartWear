#include <Initialize.h>

#include <RPM.h>
#include <BarometerDriver.h>
#include <SW_define.h>
#include <SW_struct.h>
#include <EEPROM_Utils.h>
#include <MAX17043.h>
#include <PowerManager.h>
#include <BLE.h>
#include <BLEAdvertising.h>

#include <WiFi_AP.h>

#include <Wire.h>
#include <SoftWire.h>

#include <SDCard.h>
#include <SC7A22H.h>
//#include <driver/adc.h>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string.h>

#include <Preferences.h>

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <esp_sleep.h>
#include <esp_pm.h>
#include <soc/rtc.h>
#include <driver/rtc_io.h>

#include <FastLED.h>

#include <freertos/event_groups.h>

#include "esp_heap_caps.h"
/**************************** */
#if ENABLE_DEDUCTION_FLAG
    #if TFLITE_INT8_NORMALIZE_FLAG
        #include "model_io_int8.h"
    #else
        #include "model_io12_float32.h"
    #endif
    #include <tensorflow/lite/c/common.h>
    #include <tensorflow/lite/micro/micro_interpreter.h>
    #include <tensorflow/lite/micro/micro_mutable_op_resolver.h>
    #include <tensorflow/lite/micro/tflite_bridge/micro_error_reporter.h>
    #include <tensorflow/lite/schema/schema_generated.h>
    #include <tensorflow/lite/micro/micro_op_resolver.h>
#endif
/**************************** */

extern "C" { // 必须包裹 C 语言头文件
#include "esp_log.h"
}

/////////////////////////// 外设 //////////////////////////////////

// GRB-LED
CRGB leds[NUM_LEDS];

#if ENABLE_IMU_TYPE_MPU9250
    MPU9250 imu;
#else
    ICM_20948_I2C imu; 
#endif

TwoWire i2cBus1 = TwoWire(1);
/// 气压计
BarometerDriver baro(i2cBus1, BARO_ADDR);

SFE_MAX1704X lipo; 
SoftWire soft_i2cBus2(MAX17043_SDA, MAX17043_SCL);
std::atomic<int> ledState(eBLUE);

/////////////////////////// 状态 //////////////////////////////////
char swTxBuffer[16];
char swRxBuffer[16];

SensorState sensorState = ERROR;

#if ENABLE_DEDUCTION_FLAG
    tflite::MicroMutableOpResolver<27> op_resolver; // 增加容量
    // 初始化解释器
    const tflite::Model *model = tflite::GetModel(model_data);
    // 预分配内存（通常在全局作用域定义）
    constexpr int32_t kTensorArenaSize = 1024 * 256;
    static uint8_t *tensor_arena = (uint8_t *)heap_caps_aligned_alloc(
        16, kTensorArenaSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    std::unique_ptr<tflite::MicroInterpreter> interpreter;

    TfLiteTensor *input = nullptr;
    TfLiteTensor *output = nullptr;
#endif

#if TFLITE_INT8_NORMALIZE_FLAG
    // 双缓冲区定义（预分配）
    int8_t *buffer_ping = nullptr;
    int8_t *buffer_pong = nullptr;

    int8_t *ai_buffer_ping = nullptr;
    int8_t *ai_buffer_pong = nullptr;

    int8_t *aiBuffer_copy = nullptr;
#else
    float *buffer_ping = nullptr;
    float *buffer_pong = nullptr;

    float *ai_buffer_ping = nullptr;
    float *ai_buffer_pong = nullptr;

    float *aiBuffer_copy = nullptr;
#endif

#if ENABLE_DEDUCTION_FLAG
    #if TFLITE_INT8_NORMALIZE_FLAG
    void load_quantization_params() {
        input_scale = input->params.scale;
        input_zero_point = input->params.zero_point;
        output_scale = output->params.scale;
        output_zero_point = output->params.zero_point;

        printf("----input_scale : %f", input_scale);
        printf("----input_zero_point : %ld", input_zero_point);
        printf("----output_scale : %f", output_scale);
        printf("----output_zero_point : %ld", output_zero_point);
    }
    #endif
#endif

    void init_psram_buffers() {
#if TEST_SHOW_FREE_SPIRAM_FLAG
        size_t free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
            printf("[Data Fusion] Free PSRAM: %d bytes\n", free_psram);
#endif
#if DMA_SUPPORT
             uint32_t caps = MALLOC_CAP_SPIRAM |MALLOC_CAP_DMA| MALLOC_CAP_8BIT;
#else
            uint32_t caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
#endif
#if TFLITE_INT8_NORMALIZE_FLAG
        // 分配对齐内存（ALIGNMENT_SIZE字节对齐，PSRAM）
        buffer_ping = (int8_t *)heap_caps_aligned_alloc(
            ALIGNMENT_SIZE, buffer_block, caps);
        buffer_pong = (int8_t *)heap_caps_aligned_alloc(
            ALIGNMENT_SIZE, buffer_block, caps);

        // 分配对齐内存（ALIGNMENT_SIZE字节对齐，PSRAM）
        ai_buffer_ping = (int8_t *)heap_caps_aligned_alloc(
            ALIGNMENT_SIZE, ai_buffer_block, caps);
        ai_buffer_pong = (int8_t *)heap_caps_aligned_alloc(
            ALIGNMENT_SIZE, ai_buffer_block, caps);

        aiBuffer_copy = (int8_t *)heap_caps_aligned_alloc(
            ALIGNMENT_SIZE,ai_buffer_block,caps);
#else
        buffer_ping = (float *)heap_caps_aligned_alloc(
            ALIGNMENT_SIZE, buffer_block, caps);
        buffer_pong = (float *)heap_caps_aligned_alloc(
            ALIGNMENT_SIZE, buffer_block, caps);

        ai_buffer_ping = (float *)heap_caps_aligned_alloc(
            ALIGNMENT_SIZE, ai_buffer_block, caps);
        ai_buffer_pong = (float *)heap_caps_aligned_alloc(
            ALIGNMENT_SIZE, ai_buffer_block, caps);

        aiBuffer_copy = (float *)heap_caps_aligned_alloc(
            ALIGNMENT_SIZE,ai_buffer_block,caps);

#endif

        assert(buffer_ping && buffer_pong && "PSRAM allocation failed!");
        memset(buffer_ping, 0, buffer_block); // 初始化为0
        memset(buffer_pong, 0, buffer_block);

        assert(ai_buffer_ping && ai_buffer_pong && "PSRAM allocation failed!");
        memset(ai_buffer_ping, 0, ai_buffer_block); // 初始化为0
        memset(ai_buffer_pong, 0, ai_buffer_block);

    #if TEST_SHOW_FREE_SPIRAM_FLAG
            free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
            printf("[Data Fusion] Free PSRAM: %d bytes\n", free_psram);
    #endif
    }


void scanI2C(TwoWire &i2cBus){
    uint8_t error, address; // 改用 uint8_t 类型
    int devices = 0;
    for (address = 1; address < 127; address++) {
        i2cBus.beginTransmission(address);
        error = i2cBus.endTransmission();
        
        switch(error) {
            case 0:  // 成功
                printf( "设备发现: 0x%02X", address);
                devices++;
                break;
                
            case 1:  // 数据过长
                printf("地址 0x%02X: 数据过长错误", address);
                break;
                
            case 2:  // 地址NACK
                // 静默处理或调试输出
                printf( "地址 0x%02X 无设备", address);
                break;
                
            case 3:  // 数据NACK
                printf("地址 0x%02X: 设备拒绝数据", address);
                // 可能是有效设备但状态异常
                break;
                
            case 4:  // 其他错误
                printf("地址 0x%02X: 总线错误", address);
                break;
                
            default: // 未知错误
                printf("地址 0x%02X: 未知错误 %d", address, error);
        }

        vTaskDelay(pdMS_TO_TICKS(10)); // 防止总线阻塞
    }
}

void scanSoftI2C(SoftWire &softI2CBus){
    uint8_t error, address; // 改用 uint8_t 类型
    int devices = 0;
    for (address = 1; address < 127; address++) {
        softI2CBus.beginTransmission(address);
        error = softI2CBus.endTransmission();
        
        switch(error) {
            case 0:  // 成功
                ESP_LOGI("TAG", "设备发现: 0x%02X", address);
                devices++;
                break;
                
            case 1:  // 数据过长
                ESP_LOGW("TAG", "地址 0x%02X: 数据过长错误", address);
                break;
                
            case 2:  // 地址NACK
                // 静默处理或调试输出
                //ESP_LOGD("TAG", "地址 0x%02X 无设备", address);
                break;
                
            case 3:  // 数据NACK
                ESP_LOGW("TAG", "地址 0x%02X: 设备拒绝数据", address);
                // 可能是有效设备但状态异常
                break;
                
            case 4:  // 其他错误
                ESP_LOGE("TAG", "地址 0x%02X: 总线错误", address);
                break;
                
            default: // 未知错误
                ESP_LOGE("TAG", "地址 0x%02X: 未知错误 %d", address, error);
        }

        vTaskDelay(pdMS_TO_TICKS(10)); // 防止总线阻塞
    }
}


#if ENABLE_IMU_TYPE_MPU9250
void calibrationIMU(){
    imu.verbose(true);
    vTaskDelay(pdMS_TO_TICKS(5000));
    imu.calibrateAccelGyro();
    vTaskDelay(pdMS_TO_TICKS(5000));
    imu.calibrateMag();
    imu.verbose(false);

    // 保存到EEPROM中
    EEPROM_Utils::saveCalibration();
    sensorState = RUNNING;
}
#endif

void init_IMU(){
#if ENABLE_IMU_TYPE_MPU9250
    // 加速度计和陀螺仪
    Wire.begin(8,9);
    scanI2C(Wire);

    MPU9250Setting setting;
    setting.accel_fs_sel = ACCEL_FS_SEL::A16G;
    setting.gyro_fs_sel = GYRO_FS_SEL::G2000DPS;
    setting.mag_output_bits = MAG_OUTPUT_BITS::M16BITS;
    setting.fifo_sample_rate = FIFO_SAMPLE_RATE::SMPL_200HZ;
    setting.gyro_fchoice = 0x03;
    setting.gyro_dlpf_cfg = GYRO_DLPF_CFG::DLPF_41HZ;
    setting.accel_fchoice = 0x01;
    setting.accel_dlpf_cfg = ACCEL_DLPF_CFG::DLPF_45HZ;

    while (!imu.setup(IMU_ADDR, setting, Wire)) {
        ESP_LOGE("TAG", "MPU connection failed. Please check your "
                      "connection with `connection_check` example.");
    }

    imu.wakeUp();
    EEPROM.begin(0x80);
#if ENABLE_IMU_CLEAR_CALIB_FLAG
    // [IMU校准]当IMU需要校准时，EEPROM先清除
    EEPROM_Utils::clearCalibration();
#endif
    // 加载到EEPROM中
    if (!EEPROM_Utils::isCalibrated()) {
        ESP_LOGE("TAG", "No valid calibration data, starting calibration...");
        sensorState = CALIBRATING;
    } else {
        ESP_LOGI("TAG", "Loaded calibration data from EEPROM");
        //EEPROM_Utils::printCalibration();
        ESP_LOGI(
            "TAG",
            "---------------------------------- sensorState = RUNNING--------------");
        sensorState = RUNNING;
    }
#else
    Wire.begin(8,9);
    //scanI2C(Wire);
    //imu.enableDebugging(); // Uncomment this line to enable helpful debug messages on Serial
    bool initialized = false;
    while (!initialized) {

        // Initialize the ICM-20948
        // If the DMP is enabled, .begin performs a minimal startup. We need to
        // configure the sample mode etc. manually.
        imu.begin(Wire, 0);

        printf("Initialization of the sensor returned: ");
        if (imu.status != ICM_20948_Stat_Ok) {
            printf("Trying again...");
        } else {
            initialized = true;
        }
        imu.sleep(false);
  }



//   // The next few configuration functions accept a bit-mask of sensors for which the settings should be applied.

//   // Set Gyro and Accelerometer to a particular sample mode
//   // options: ICM_20948_Sample_Mode_Continuous
//   //          ICM_20948_Sample_Mode_Cycled
//   imu.setSampleMode((ICM_20948_Internal_Acc | ICM_20948_Internal_Gyr), ICM_20948_Sample_Mode_Continuous);
//   if (imu.status != ICM_20948_Stat_Ok)
//   {
//     printf("setSampleMode returned: ");
//     printf(imu.statusString());
//   }
  
//   // Set full scale ranges for both acc and gyr
//   ICM_20948_fss_t fss; // This uses a "Full Scale Settings" structure that can contain values for all configurable sensors

//   fss.a = gpm16; // (ICM_20948_ACCEL_CONFIG_FS_SEL_e)
//                   // gpm2
//                   // gpm4
//                   // gpm8
//                   // gpm16

//   fss.g = dps2000; // (ICM_20948_GYRO_CONFIG_1_FS_SEL_e)
//                     // dps250
//                     // dps500
//                     // dps1000
//                     // dps2000

//   imu.setFullScale((ICM_20948_Internal_Acc | ICM_20948_Internal_Gyr), fss);
//   if (imu.status != ICM_20948_Stat_Ok)
//   {
//     printf("setFullScale returned: ");
//     printf(imu.statusString());
//   }

//   // Set up Digital Low-Pass Filter configuration
//   ICM_20948_dlpcfg_t DLPcfg;    // Similar to FSS, this uses a configuration structure for the desired sensors

//     // 加速度计：推荐 ~50Hz 带宽
//     // 这能滤掉大部分非运动产生的高频震动（如电机震动、布料摩擦），同时保留脚步落地的“冲击感”
//     DLPcfg.a = acc_d50bw4_n68bw8; 

//     // 陀螺仪：推荐 ~51Hz 带宽
//     // 为了保证 Sensor Fusion（姿态解算）的效果，陀螺仪的带宽最好和加速度计保持一致，避免相位差
//     DLPcfg.g = gyr_d51bw2_n73bw3;

//   imu.setDLPFcfg((ICM_20948_Internal_Acc | ICM_20948_Internal_Gyr), DLPcfg);
//   if (imu.status != ICM_20948_Stat_Ok)
//   {
//     printf("setDLPcfg returned: ");
//     printf(imu.statusString());
//   }

//   // Choose whether or not to use DLPF
//   // Here we're also showing another way to access the status values, and that it is OK to supply individual sensor masks to these functions
//   ICM_20948_Status_e accDLPEnableStat = imu.enableDLPF(ICM_20948_Internal_Acc, false);
//   ICM_20948_Status_e gyrDLPEnableStat = imu.enableDLPF(ICM_20948_Internal_Gyr, false);
//   printf("Enable DLPF for Accelerometer returned: ");
//   printf(imu.statusString(accDLPEnableStat));
//   printf("Enable DLPF for Gyroscope returned: ");
//   printf(imu.statusString(gyrDLPEnableStat));

//   // Choose whether or not to start the magnetometer
//   imu.startupMagnetometer();


  bool success = true; // Use success to show if the DMP configuration was successful

  // Initialize the DMP. initializeDMP is a weak function. In this example we overwrite it to change the sample rate (see below)
  success &= (imu.initializeDMP() == ICM_20948_Stat_Ok);

  // DMP sensor options are defined in ICM_20948_DMP.h
  //    INV_ICM20948_SENSOR_ACCELEROMETER               (16-bit accel)
  //    INV_ICM20948_SENSOR_GYROSCOPE                   (16-bit gyro + 32-bit calibrated gyro)
  //    INV_ICM20948_SENSOR_RAW_ACCELEROMETER           (16-bit accel)
  //    INV_ICM20948_SENSOR_RAW_GYROSCOPE               (16-bit gyro + 32-bit calibrated gyro)
  //    INV_ICM20948_SENSOR_MAGNETIC_FIELD_UNCALIBRATED (16-bit compass)
  //    INV_ICM20948_SENSOR_GYROSCOPE_UNCALIBRATED      (16-bit gyro)
  //    INV_ICM20948_SENSOR_STEP_DETECTOR               (Pedometer Step Detector)
  //    INV_ICM20948_SENSOR_STEP_COUNTER                (Pedometer Step Detector)
  //    INV_ICM20948_SENSOR_GAME_ROTATION_VECTOR        (32-bit 6-axis quaternion)
  //    INV_ICM20948_SENSOR_ROTATION_VECTOR             (32-bit 9-axis quaternion + heading accuracy)
  //    INV_ICM20948_SENSOR_GEOMAGNETIC_ROTATION_VECTOR (32-bit Geomag RV + heading accuracy)
  //    INV_ICM20948_SENSOR_GEOMAGNETIC_FIELD           (32-bit calibrated compass)
  //    INV_ICM20948_SENSOR_GRAVITY                     (32-bit 6-axis quaternion)
  //    INV_ICM20948_SENSOR_LINEAR_ACCELERATION         (16-bit accel + 32-bit 6-axis quaternion)
  //    INV_ICM20948_SENSOR_ORIENTATION                 (32-bit 9-axis quaternion + heading accuracy)

  success &= (imu.enableDMPSensor(INV_ICM20948_SENSOR_GYROSCOPE) == ICM_20948_Stat_Ok);
  // Enable the DMP Game Rotation Vector sensor (Quat6)
  success &= (imu.enableDMPSensor(INV_ICM20948_SENSOR_LINEAR_ACCELERATION) == ICM_20948_Stat_Ok);

  int odr_div = 1; // 强烈推荐改成 1

  // Configuring DMP to output data at multiple ODRs:
  // DMP is capable of outputting multiple sensor data at different rates to FIFO.
  // Setting value can be calculated as follows:
  // Value = (DMP running rate / ODR ) - 1
  // E.g. For a 225Hz ODR rate when DMP is running at 225Hz, value = (225/225) - 1 = 0.
  success &= (imu.setDMPODRrate(DMP_ODR_Reg_Quat6,odr_div) == ICM_20948_Stat_Ok); // Set to 112.5Hz
  success &= (imu.setDMPODRrate(DMP_ODR_Reg_Accel, odr_div) == ICM_20948_Stat_Ok); // Set to 112.5Hz
  success &= (imu.setDMPODRrate(DMP_ODR_Reg_Gyro, odr_div) == ICM_20948_Stat_Ok); 

  // Enable the FIFO
  success &= (imu.enableFIFO() == ICM_20948_Stat_Ok);

  // Enable the DMP
  success &= (imu.enableDMP() == ICM_20948_Stat_Ok);

  // Reset DMP
  success &= (imu.resetDMP() == ICM_20948_Stat_Ok);

  // Reset FIFO
  success &= (imu.resetFIFO() == ICM_20948_Stat_Ok);

  // Check success
  if (success)
  {
    printf("DMP ODR rates set to 75Hz.\n");
  }
#endif
}

void scanI2C(SoftWire &bus) {
    printf("Scanning I2C bus...\n");
    byte error;
    int nDevices = 0;

    for (byte address = 1; address < 127; address++) {
        bus.beginTransmission(address);
        error = bus.endTransmission(); // 使用带STOP的常规结束

        if (error == 0) { // 0 表示设备已应答(ACK)
            printf("I2C device found at address 0x%02X\n", address);
            nDevices++;
        } else if (error == 4) { // 4 表示其他错误
            printf("Unknown error at address 0x%02X\n", address);
        }
    }
    if (nDevices == 0) {
        printf("No I2C devices found. Check wiring and pull-up resistors!\n");
    } else {
        printf("Scan complete.\n");
    }
}

void init_MAX17043() {
    // 1. 初始化I2C总线 (您的配置基本正确)
    soft_i2cBus2.setTxBuffer(swTxBuffer, sizeof(swTxBuffer));
    soft_i2cBus2.setRxBuffer(swRxBuffer, sizeof(swRxBuffer));
    
    // 推荐使用外部上拉电阻，并将此项设为false。如果只用内部上拉，设为true。
    soft_i2cBus2.enablePullups(false); 

    soft_i2cBus2.setDelay_us(50); // 尝试放慢速度到50kHz，增加稳定性
    soft_i2cBus2.setTimeout(200);
    soft_i2cBus2.begin();

    // 2. 检测设备是否存在
    lipo.setDevice(sfe_max1704x_devices_e::MAX1704X_MAX17043); 
    if (lipo.begin(soft_i2cBus2) == false) {
        printf("MAX17043 not detected. Please check wiring and pull-ups. Freezing.\n");
        while(1) vTaskDelay(pdMS_TO_TICKS(1000)); // 错误则冻结
    }
    printf("MAX17043 detected successfully.\n");

    // ================== 关键的初始化流程优化 ==================

    // 3. 等待芯片内部稳定
    // 在进行任何操作前，给芯片足够的时间来完成上电后的第一次有效ADC转换。
    // 数据手册建议至少等待175ms，这里给更长一点的时间确保稳定。
    printf("Waiting for MAX17043 to stabilize...\n");
    vTaskDelay(pdMS_TO_TICKS(350)); 

    // 4. 执行Quick Start来强制算法复位
    printf("Performing Quick Start...\n");
    lipo.quickStart();

    // 5. 在Quick Start后给算法足够的时间来收敛和更新SOC寄存器
    // 这是最关键的一步，这个延迟不能太短。
    printf("Waiting for algorithm to converge after Quick Start...\n");
    vTaskDelay(pdMS_TO_TICKS(500)); // 增加等待时间到500ms

    // 7. （可选）设置警报阈值
    lipo.setThreshold(SOC_THRESHOLD);
    printf("Alert threshold set to 10%%.\n");
    int alert = lipo.getAlert();
    printf("MAX17043 initialization complete.\n");

    // 在这里第一次读取SOC，看看结果是否正常
    float soc = lipo.getSOC();
    float voltage = lipo.getVoltage();
    printf("Initial read after init: Voltage = %.2fV, SOC = %.2f%%\n", voltage, soc);

    if(soc > SOC_THRESHOLD){
        // 6. （可选但推荐）清除警报标志位
        // 如果之前有过低电量警报，这个标志位可能还存在，最好清除它。
        lipo.clearAlert();
    }
}
void init_MS5611(){
    i2cBus1.begin(MS5611_SDA, MS5611_SCL);

    if (!baro.begin()) {
        ESP_LOGE("TAG", "BarometerDriver not begin!");
    }

}

void init_AI(){
#if ENABLE_DEDUCTION_FLAG
    // 必须在setup()内，以下算子
    op_resolver.AddAdd();
    op_resolver.AddMul();
    op_resolver.AddSqrt();
    op_resolver.AddAbs();
    op_resolver.AddSub();
    op_resolver.AddTranspose();
    op_resolver.AddShape(); // 添加 SHAPE 算子支持
    op_resolver.AddReshape();
    op_resolver.AddSquaredDifference();
    op_resolver.AddStridedSlice();
    op_resolver.AddMean();
    op_resolver.AddPack();
    op_resolver.AddUnpack();
    op_resolver.AddFill();
    op_resolver.AddExpandDims();
    op_resolver.AddConcatenation();
    op_resolver.AddConv2D();
    op_resolver.AddMaxPool2D();
    op_resolver.AddReduceMax();
    op_resolver.AddSoftmax();
    op_resolver.AddFullyConnected();
    op_resolver.AddQuantize();
    op_resolver.AddDequantize();
    op_resolver.AddTanh();
    op_resolver.AddLogistic();
    op_resolver.AddSplit();
    op_resolver.AddWhile();

    // // set up the error reporter
    static tflite::MicroErrorReporter micro_error_reporter;
    tflite::ErrorReporter *error_reporter = &micro_error_reporter;

    static tflite::MicroInterpreter static_interpreter(
        model, op_resolver, tensor_arena, kTensorArenaSize);
    interpreter =
        std::unique_ptr<tflite::MicroInterpreter>(&static_interpreter);

    // 在 setup() 中初始化 tensor_arena 后添加：
    if (tensor_arena == NULL) {
        ESP_LOGE("TAG", "Failed to allocate Tensor Arena from PSRAM!");
        while (1); // 阻塞或重启
    } else {
        // ESP_LOGI("TAG", "Tensor Arena allocated from PSRAM: %p (Size: %d KB)",
        //             tensor_arena, kTensorArenaSize / 1024);
    }

    // Allocate memory for the model's input buffers
    TfLiteStatus allocate_status = interpreter->AllocateTensors();
    if (allocate_status != kTfLiteOk) {
            TF_LITE_REPORT_ERROR(error_reporter, "Tensor allocation failed");
    } else {
        printf("Tensor allocation success\n");
        printf("Used bytes: ");
        printf("%d", interpreter->arena_used_bytes()/1024);
        printf("KB\n");
    }

    // Obtain a pointer to the model's input tensor
    input = interpreter->input(0);
    output = interpreter->output(0);

    #if TFLITE_INT8_NORMALIZE_FLAG
        load_quantization_params(); // 确保在初始化后调用
    #endif
#endif
}

void init_RGBLED(){
    FastLED.addLeds<WS2812B, RGB_PIN, GRB>(leds, NUM_LEDS);
    FastLED.setBrightness(50); // 亮度（0-255）
    fill_solid(leds, NUM_LEDS, CRGB::Blue);
    FastLED.show();
}

void init_BLE(){
    // 初始化蓝牙模块
    BLE::initBLE();

#if TEST_ENABLE_BLE_MAC_ADDR
    // 获取BLE MAC地址
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_BT);
    ESP_LOGI("DEVICE_ID", "BLE MAC: %02X:%02X:%02X:%02X:%02X:%02X",
    mac[0],mac[1], mac[2], mac[3], mac[4], mac[5]);
#endif
}

void init_Accel(){
    pinMode(1, INPUT_PULLUP);     // 启用上拉电阻
    pinMode(2, INPUT_PULLUP);     // 启用上拉电阻

    // 1. 初始化I2C总线 (您的配置基本正确)
    softWire.setTxBuffer(i2cTxBuffer, sizeof(i2cTxBuffer));
    softWire.setRxBuffer(i2cRxBuffer, sizeof(i2cRxBuffer));
    
    // 推荐使用外部上拉电阻，并将此项设为false。如果只用内部上拉，设为true。
    softWire.enablePullups(false); 

    softWire.setDelay_us(50); // 尝试放慢速度到50kHz，增加稳定性
    softWire.setTimeout(200);
    softWire.begin();

    // 初始化传感器
    if (!initializeSensor()) {
        printf("Failed to initialize SC7A22H!");
        printf("Please check:");
        printf("1. Wiring connections (SDA=GPIO21, SCL=GPIO22)");
        printf("2. I2C address (should be 0x19)");
        printf("3. Power supply (3.3V)");
        while(1) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
    
    printf("Ready to read sensor data...");
    printf("\n");


    printf("Initializing SC7A22H...");
    printf("SC7A22H Ready!");
    printf("Acceleration data (X, Y, Z in g):");

    vTaskDelay(pdMS_TO_TICKS(200));

    float accel_x, accel_y, accel_z;
    
    // 读取加速度数据
    readAcceleration(accel_x, accel_y, accel_z);

    printf("Accel (g): X=%.3f, Y=%.3f, Z=%.3f\n", accel_x, accel_y, accel_z);
}

void init_WiFiAP(){
    WiFiAP::bufferInit();          // 关键：确保缓冲区初始化在任务创建前完成
    WiFiAP::wifiInitSoftAP();
    WiFiAP::initWebsocketServer();

    // ADDED: 定时检查连接状态
    xTaskCreatePinnedToCore(
        [](void *pvParameters) {
            while (1) {
                if(WiFiAP::wifi_event_group){
                        ESP_LOGI("WIFI", "WiFiAP::wifi_event_group");
                    EventBits_t bits = xEventGroupWaitBits(WiFiAP::wifi_event_group,DEVICE_CONNECTED_AP_BIT|DEVICE_DISCONNECTED_AP_BIT|DEVICE_GET_IP_AP_BIT,pdTRUE,pdFALSE,portMAX_DELAY);
                    if (bits & DEVICE_CONNECTED_AP_BIT) {
                        ESP_LOGW("WIFI", "等待客户端连接AP...");
                        g_WiFiIsConnected.store(false);
                    } else {
                        ESP_LOGI("WIFI", "当前连接客户端");
                        g_WiFiIsConnected.store(true);
                    }
                }
                vTaskDelay(10000 / portTICK_PERIOD_MS); // 每10秒检查一次
            }
        },
        "AP_Status", 4096, NULL, 1, NULL, 0);
}
