#include <Arduino.h>
#include <Wire.h>
#include <esp_heap_caps.h>
#include <lvgl.h>
#include "ST77922.h"
#include "audio_beep.h"
#include "debug_config.h"

#if LV_COLOR_DEPTH != 16
#error "hello_colors requires LV_COLOR_DEPTH 16 in lv_conf.h"
#endif

namespace {
constexpr uint8_t TOUCH_ADDRESS = 0x55;
constexpr uint16_t TOUCH_INFO = 0x0010;
constexpr uint16_t TOUCH_POINT0 = 0x0014;
constexpr uint8_t MAX_TOUCH_POINTS = 10;

ST77922 *panel = nullptr;
lv_color_t *drawPixels = nullptr;
lv_disp_draw_buf_t drawBuffer;
lv_disp_drv_t displayDriver;
lv_indev_drv_t touchDriver;
bool beepPending = false;
size_t colorIndex = 0;
uint8_t maxTouchPoints = 1;

const uint32_t colors[] = {
    0xFF0000, 0x0000FF, 0x00FF00, 0xFFFF00, 0x00FFFF, 0xFF00FF
};

bool readTouchRegister(uint16_t reg, uint8_t *bytes, size_t length) {
    Wire.beginTransmission(TOUCH_ADDRESS);
    Wire.write(uint8_t(reg >> 8));
    Wire.write(uint8_t(reg));
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom(TOUCH_ADDRESS, uint8_t(length)) != length) return false;
    for (size_t i = 0; i < length; ++i) bytes[i] = Wire.read();
    return true;
}

void flushDisplay(lv_disp_drv_t *driver, const lv_area_t *area, lv_color_t *pixels) {
    const uint16_t width = area->x2 - area->x1 + 1;
    const uint16_t height = area->y2 - area->y1 + 1;
    uint16_t *rgb565 = reinterpret_cast<uint16_t *>(pixels);
#if LV_COLOR_16_SWAP == 0
    // The ST77922 expects the high RGB565 byte first on the wire.
    for (size_t i = 0; i < size_t(width) * height; ++i) {
        rgb565[i] = uint16_t((rgb565[i] << 8) | (rgb565[i] >> 8));
    }
#endif
    panel->Fill_Colors(area->x1, area->y1, width, height, rgb565);
    lv_disp_flush_ready(driver);
}

void readTouch(lv_indev_drv_t *, lv_indev_data_t *data) {
    static lv_point_t lastPoint = {0, 0};
    uint8_t info = 0;
    uint8_t points[7 * MAX_TOUCH_POINTS] = {};
    data->state = LV_INDEV_STATE_REL;
    data->point = lastPoint;
    if (!readTouchRegister(TOUCH_INFO, &info, 1) || !(info & 0x08) ||
        !readTouchRegister(TOUCH_POINT0, points, 7 * maxTouchPoints)) {
        return;
    }
    for (uint8_t i = 0; i < maxTouchPoints; ++i) {
        const uint8_t *point = points + 7 * i;
        if (!(point[0] & 0x80)) continue;
        const uint16_t x = (uint16_t(point[0] & 0x3F) << 8) | point[1];
        const uint16_t y = (uint16_t(point[2] & 0x3F) << 8) | point[3];
        if (x >= panel->Get_Width() || y >= panel->Get_Height()) continue;
        lastPoint = {lv_coord_t(x), lv_coord_t(y)};
        data->point = lastPoint;
        data->state = LV_INDEV_STATE_PR;
        break;
    }
}

void onScreenPressed(lv_event_t *event) {
    if (lv_event_get_code(event) != LV_EVENT_PRESSED) return;
    colorIndex = (colorIndex + 1) % (sizeof(colors) / sizeof(colors[0]));
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(colors[colorIndex]), 0);
    beepPending = true;
    DEBUG_PRINTF("Touch: color %u at %lu ms\n", unsigned(colorIndex),
                 static_cast<unsigned long>(millis()));
}

void setupLvgl() {
    lv_init();
    const size_t pixelCount = size_t(panel->Get_Width()) * panel->Get_Height();
    drawPixels = static_cast<lv_color_t *>(
        heap_caps_malloc(pixelCount * sizeof(lv_color_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!drawPixels) {
        DEBUG_PRINTLN("LVGL draw buffer allocation failed");
        while (true) delay(1000);
    }
    lv_disp_draw_buf_init(&drawBuffer, drawPixels, nullptr, pixelCount);
    lv_disp_drv_init(&displayDriver);
    displayDriver.hor_res = panel->Get_Width();
    displayDriver.ver_res = panel->Get_Height();
    displayDriver.draw_buf = &drawBuffer;
    displayDriver.flush_cb = flushDisplay;
    displayDriver.full_refresh = 1;
    lv_disp_drv_register(&displayDriver);

    lv_indev_drv_init(&touchDriver);
    touchDriver.type = LV_INDEV_TYPE_POINTER;
    touchDriver.read_cb = readTouch;
    lv_indev_drv_register(&touchDriver);

    lv_obj_t *screen = lv_scr_act();
    lv_obj_set_style_bg_color(screen, lv_color_hex(colors[colorIndex]), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_add_flag(screen, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(screen, onScreenPressed, LV_EVENT_PRESSED, nullptr);
    lv_obj_t *title = lv_label_create(screen);
    lv_label_set_text(title, "Hello world");
#if LV_FONT_MONTSERRAT_28
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
#endif
    lv_obj_set_style_text_color(title, lv_color_black(), 0);
    lv_obj_clear_flag(title, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_center(title);
}
} // namespace

void setup() {
#if DEBUG
    Serial.begin(115200);
    Serial.setTxTimeoutMs(0);
#endif
    delay(1000);
    DEBUG_PRINTLN("Hello colors LVGL touch + ES8311 beep diagnostic");
    DEBUG_PRINTF("Startup: reset reason=%d, free PSRAM=%u\n", int(esp_reset_reason()),
                 unsigned(ESP.getFreePsram()));
    pinMode(41, OUTPUT);
    digitalWrite(41, LOW);
    pinMode(48, OUTPUT);
    digitalWrite(48, LOW);
    delay(100);
    digitalWrite(48, HIGH);
    delay(200);
    static ST77922 display;
    panel = &display;
    digitalWrite(41, LOW);
    panel->Write_Reg(0x01, nullptr, 0);
    delay(150);
    panel->Init();
    panel->Set_Rotation(0);
    DEBUG_PRINTF("DISPLAY: initialized, QSPI=%u Hz\n", unsigned(QSPI_FREQUENCY));

    initBeepAudio(); // Initializes Wire on GPIO 38/39 for both audio and touch.
    pinMode(47, INPUT_PULLUP);
    uint8_t reportedMaxTouches = 0;
    const bool touchResponding = readTouchRegister(0x0009, &reportedMaxTouches, 1);
    if (touchResponding && reportedMaxTouches >= 1 && reportedMaxTouches <= MAX_TOUCH_POINTS)
        maxTouchPoints = reportedMaxTouches;
    DEBUG_PRINTF("TOUCH: controller %s, max points=%u\n",
                 touchResponding ? "ready" : "not responding", unsigned(maxTouchPoints));
    setupLvgl();
}

void loop() {
    static uint32_t lastTick = millis();
    const uint32_t now = millis();
    lv_tick_inc(now - lastTick);
    lastTick = now;
    lv_timer_handler();
    if (beepPending) {
        beepPending = false;
        playBeep();
    }
    delay(5);
}
