# Third-party notices

This repository vendors third-party libraries under `components/`. They are **not**
covered by this project's Apache-2.0 license; each retains its original license.
See the `LICENSE`, `COPYING`, `license.txt` or `License.md` file inside each
component directory.

| Component | Notes |
|---|---|
| Adafruit_BusIO | Adafruit library |
| Adafruit_GFX_Library | Adafruit library |
| Adafruit_SSD1306 | Adafruit library |
| Adafruit_Unified_Sensor | Adafruit library |
| ArduinoEigen | Eigen (MPL2) |
| ArxTypeTraits | type traits helper |
| AsyncDelay | timing helper |
| AsyncTCP | async TCP library |
| esp-tflite-micro | TensorFlow Lite Micro port (Apache-2.0) |
| ESPAsyncWebServer | async web server |
| esp_websocket_client | Espressif component (Apache-2.0) |
| FastLED | LED library |
| ICM20948 | IMU driver |
| MS5611 | barometer driver |
| sd_card | SD card helper |
| SoftWire | software I2C |

ESP-IDF itself, Arduino-ESP32, ESP-SR, ESP-DSP and other managed components are
fetched by the IDF component manager at build time and are not vendored here.
Their licenses are distributed with the corresponding packages.

If you redistribute a firmware image built from this repository, keep the
license notices of the components you link and comply with their terms.
