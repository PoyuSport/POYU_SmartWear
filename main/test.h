

/********************************************************************************* */
// 优化版本：使用更简洁的格式化方法
void printPaddedInt16b(int16_t val) {
  char buffer[7]; // 包括符号和结束符
  snprintf(buffer, sizeof(buffer), "%+06d", val);
  Serial.print(buffer);
}

// 更高效的版本，避免重复判断
void printPaddedInt16b_v2(int16_t val) {
  if (val < 0) {
    Serial.print("-");
    val = -val;
  } else {
    Serial.print(" ");
  }
  
  // 直接使用数字比较，避免多个if语句
  if (val < 10000) Serial.print("0");
  if (val < 1000) Serial.print("0");
  if (val < 100) Serial.print("0");
  if (val < 10) Serial.print("0");
  Serial.print(val);
}

// 优化后的原始数据打印函数
void printRawAGMT(ICM_20948_AGMT_t agmt) {
  printf("RAW. Acc [");
  printPaddedInt16b(agmt.acc.axes.x);
  printf(",");
  printPaddedInt16b(agmt.acc.axes.y);
  printf(",");
  printPaddedInt16b(agmt.acc.axes.z);
  printf("], Gyr [");
  printPaddedInt16b(agmt.gyr.axes.x);
  printf(",");
  printPaddedInt16b(agmt.gyr.axes.y);
  printf(",");
  printPaddedInt16b(agmt.gyr.axes.z);
  printf("], Mag [");
  printPaddedInt16b(agmt.mag.axes.x);
  printf(",");
  printPaddedInt16b(agmt.mag.axes.y);
  printf(",");
  printPaddedInt16b(agmt.mag.axes.z);
  printf("], Tmp [");
  printPaddedInt16b(agmt.tmp.val);
  printf("]");
}

// 更紧凑的版本，减少代码重复
void printRawAGMT_compact(ICM_20948_AGMT_t agmt) {
  const char* labels[] = {"Acc", "Gyr", "Mag", "Tmp"};
  int16_t* data[] = {
    &agmt.acc.axes.x, &agmt.acc.axes.y, &agmt.acc.axes.z,
    &agmt.gyr.axes.x, &agmt.gyr.axes.y, &agmt.gyr.axes.z,
    &agmt.mag.axes.x, &agmt.mag.axes.y, &agmt.mag.axes.z,
    &agmt.tmp.val
  };
  
  int data_index = 0;
  for (int i = 0; i < 4; i++) {
    printf("%s [", labels[i]);
    int elements = (i == 3) ? 1 : 3; // 温度只有1个值
    
    for (int j = 0; j < elements; j++) {
      printPaddedInt16b(*data[data_index++]);
      if (j < elements - 1) printf(",");
    }
    printf("]");
    if (i < 3) printf(", ");
  }
}

// 优化浮点数格式化
void printFormattedFloat(float val, uint8_t leading, uint8_t decimals) {
  char format[16];
  snprintf(format, sizeof(format), "%%%d.%df", leading + decimals + 1, decimals);
  
  if (val >= 0) {
    Serial.print(" ");
  }
  printf(format, fabs(val));
}

// 更高效的浮点数格式化版本
void printFormattedFloat_v2(float val, uint8_t leading, uint8_t decimals) {
  // 处理符号
  Serial.print(val >= 0 ? " " : "-");
  
  float abs_val = fabs(val);
  
  // 计算前导零的数量
  int int_part = (int)abs_val;
  int digits = 1;
  if (int_part > 0) {
    digits = (int)log10(int_part) + 1;
  }
  
  int leading_zeros = leading - digits;
  if (leading_zeros > 0) {
    for (int i = 0; i < leading_zeros; i++) {
      Serial.print("0");
    }
  }
  
  // 打印数值
  Serial.print(abs_val, decimals);
}

// 优化标量数据打印函数
#ifdef USE_SPI
void printScaledAGMT(ICM_20948_SPI *sensor) {
#else
void printScaledAGMT(ICM_20948_I2C *sensor) {
#endif
  printf("Scaled. Acc (mg) [");
  printFormattedFloat(sensor->accX(), 5, 2);
  printf(",");
  printFormattedFloat(sensor->accY(), 5, 2);
  printf(",");
  printFormattedFloat(sensor->accZ(), 5, 2);
  printf("], Gyr (DPS) [");
  printFormattedFloat(sensor->gyrX(), 5, 2);
  printf(",");
  printFormattedFloat(sensor->gyrY(), 5, 2);
  printf(",");
  printFormattedFloat(sensor->gyrZ(), 5, 2);
  printf("], Mag (uT) [");
  printFormattedFloat(sensor->magX(), 5, 2);
  printf(",");
  printFormattedFloat(sensor->magY(), 5, 2);
  printf(",");
  printFormattedFloat(sensor->magZ(), 5, 2);
  printf("], Tmp (C) [");
  printFormattedFloat(sensor->temp(), 5, 2);
  printf("]");
}
