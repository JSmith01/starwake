# Hello colors (LVGL)

This Arduino sketch is for the LCDWiki ES3C35P / ES3C35P-NS ESP32-S3 board with a 320×480 ST77922 display, integrated touch controller, and ES8311 audio codec. LVGL draws the full-screen color and centered “Hello world” label. Each new screen press advances red → blue → green → yellow → cyan → magenta and plays a short 880 Hz beep. The screen stays on its current color until the next press. A held finger counts as one press.

## Arduino setup

Use Espressif ESP32 core **3.3.11** and board **ESP32S3 Dev Module** with **16 MB** flash, **QIO 80 MHz**, **OPI PSRAM**, USB mode **Hardware CDC and JTAG**, and **USB CDC On Boot: Enabled**.

Install these libraries from the board package's `1-示例程序_Demo/Arduino/Install libraries` directory into your Arduino sketchbook's `libraries` directory:

- `lvgl` **8.3.6** (the vendor's configured copy)
- `arduino-audio-driver-main` **0.1.2**

The vendor LVGL copy includes `lv_conf.h`. Set `LV_COLOR_DEPTH=16`, `LV_COLOR_16_SWAP=1`, `LV_TICK_CUSTOM=0`, and `LV_USE_PERF_MONITOR=0` there. The last setting removes the vendor's CPU/FPS rectangle. If installing LVGL from another source, use version 8.3.x and provide equivalent settings. `TFT_eSPI` is no longer needed by this sketch. The local `ST77922.cpp` remains the panel driver.

Open `hello_colors.ino` in Arduino IDE and upload. With Arduino CLI, use:

```powershell
arduino-cli compile -b 'esp32:esp32:esp32s3:FlashSize=16M,FlashMode=qio,PSRAM=opi,USBMode=hwcdc,CDCOnBoot=cdc' arduino/hello_colors
```

## Hardware notes

The existing startup sequence waits for power, resets touch on GPIO 48, and then initializes the display at a 40 MHz QSPI clock. LVGL uses a full-screen RGB565 draw buffer in PSRAM and redraws the whole panel when the color changes. Full-frame transfers avoid corruption of small partial rectangles on this panel. The configured LVGL library already swaps RGB565 bytes; the callback swaps them only when `LV_COLOR_16_SWAP=0`.

Touch is at I²C address `0x55` on GPIO 38/39. The sketch reads the controller's reported number of point records, even though it uses only the first valid point; reading the full report follows the vendor driver and allows later taps to register. It uses Arduino `Wire` so it can share the bus with the ES8311 codec at `0x18`; the vendor `ST77922_TOUCH` library creates another controller for that same bus and should not be added to this sketch. The ES8311 uses MCLK 17, BCLK 18, WS 21, DOUT 15, and active-low amplifier enable on GPIO 1. A beep lasts about 200 ms, during which the LVGL loop is briefly paused.

Set `DEBUG` to `1` in `debug_config.h` for serial diagnostics at 115200 baud. Touch presses, codec setup, and I²S writes are then logged. With debugging off, the sketch works without an open serial monitor.

The previous TFT_eSPI color-cycle version was verified on the connected board on 2026-09-27, including a cold power cycle, image, and sound. The first LVGL touch version flashed on COM7 on 2026-09-28, but the user reported missed taps and a distorted CPU/FPS overlay. The corrected version compiled and flashed with hash verification on the same day. Camera inspection showed a clean full-screen color with centered text and no overlay, both before and after touch changes. The user confirmed that repeated taps each changed the color and beeped.
