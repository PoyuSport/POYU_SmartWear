# Third-party notices

This repository vendors third-party libraries under `components/` and downloads
managed components at build time. They are **not** covered by this project's
Apache-2.0 license; each retains its original license.

## Vendored components

| Component | License | Notes |
|---|---|---|
| Adafruit_BusIO | MIT | keep `LICENSE` |
| Adafruit_GFX_Library | BSD | keep `license.txt` |
| Adafruit_SSD1306 | BSD | keep `license.txt` |
| Adafruit_Unified_Sensor | Apache-2.0 | keep `LICENSE.txt` |
| ArduinoEigen | MPL-2.0 | file-level copyleft, keep headers |
| ArxTypeTraits | MIT | keep `LICENSE` |
| AsyncDelay | LGPL-2.1 | see LGPL section |
| AsyncTCP | LGPL-3.0 | see LGPL section |
| esp-tflite-micro | Apache-2.0 | keep `LICENSE` |
| ESPAsyncWebServer | LGPL-2.1-or-later | license text added as `LICENSE` |
| esp_websocket_client | Apache-2.0 | keep `LICENSE` |
| FastLED | MIT | keep `LICENSE` |
| ICM20948 | MIT (code) | hardware is CC BY-SA 4.0; only code is vendored |
| MS5611 | MIT | keep `LICENSE` |
| sd_card | Unlicense OR CC0-1.0 | Espressif `sd_test_io` |
| SoftWire | LGPL-2.1 | see LGPL section |

## Fetched at build time

| Component | License |
|---|---|
| ESP-IDF | Apache-2.0 |
| Arduino-ESP32 | LGPL-2.1 |
| esp_tinyusb | Apache-2.0 |
| esp-sr | see the package license |
| esp-dsp | Apache-2.0 |

## LGPL components

`AsyncDelay`, `SoftWire`, `ESPAsyncWebServer`, `AsyncTCP` and Arduino-ESP32 are
under LGPL terms. For source-only distribution, keep the license texts and
notices. If you distribute **firmware binaries** (including devices you sell),
you must also comply with the LGPL requirements for that version, which
typically include:

- providing the corresponding source code (or a written offer);
- allowing the user to relink the application with a modified version of the
  LGPL library;
- for LGPL-3.0 components (AsyncTCP), providing the "Installation Information"
  required for User Products.

This notice is not legal advice; review the obligations with your own counsel
before shipping a commercial product.

## Hardware design files

`hardware/` contains the Altium project for the ESP32-S3 board and is licensed
under **CERN-OHL-W-2.0** (see `hardware/LICENSE`), separately from the
Apache-2.0 firmware.

Only the two project-referenced libraries (`0402`, `SC7A22H`) are bundled.
Other component libraries, 3D models and vendor documents are not included.
If you redistribute the hardware files or a derived board, verify the license
of every footprint/symbol you add and keep the CERN-OHL notices.

Altium Designer is proprietary software; the files are provided in its native
format and no Altium license is granted.

## Notes

- Do not delete or replace the license files inside `components/`.
- `docs/protocol.md` and the project documentation are covered by the
  repository's Apache-2.0 license unless stated otherwise.
- The model weights are distributed under Apache-2.0; the training data is not
  included.
