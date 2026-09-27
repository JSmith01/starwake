#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "driver/spi_master.h"
#include "es8311_codec.h"
#include "esp_check.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_st77922.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>

namespace {
constexpr int W = 480, H = 320, BAR = 50, PLAY_H = H - BAR;
constexpr int AUDIO_SAMPLE_RATE = 24000;
constexpr int C_BG = 0x0861, C_CYAN = 0x8FFC, C_WHITE = 0xFFFF, C_RED = 0xF9B5, C_AMBER = 0xFDE8,
              C_PURPLE = 0xC91F, C_DARK = 0x2128;
esp_lcd_panel_handle_t panel = nullptr;
i2c_master_bus_handle_t i2c_bus = nullptr;
i2c_master_dev_handle_t touch_dev = nullptr;
i2s_chan_handle_t i2s_tx = nullptr;
esp_codec_dev_handle_t codec = nullptr;
QueueHandle_t audio_queue = nullptr;
SemaphoreHandle_t lcd_transfer_done = nullptr;
uint16_t *fb = nullptr;
uint16_t *physical_fb = nullptr;
uint32_t rng = 0x871A5EED;
int mode = 0, score = 0, lives = 3, wave = 0, charge = 0, ship_x = 78, ship_y = PLAY_H / 2;
bool muted = false, dragging = false, charging = false;
float elapsed = 0, fire_timer = 0, enemy_timer = 0, boss_hp = 180, boss_x = 550, boss_y = 130;
int boss_phase = 1;
bool boss_weak_open = true;
float invulnerable = 1.8f;
struct Cue {
    uint16_t hz, ms;
};
struct Obj {
    float x, y, vx, vy, hp, age;
    int kind, life;
};
bool IRAM_ATTR on_color_transfer_done(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t *,
                                      void *user_ctx) {
    BaseType_t higher_priority_task_woken = pdFALSE;
    xSemaphoreGiveFromISR(static_cast<SemaphoreHandle_t>(user_ctx), &higher_priority_task_woken);
    return higher_priority_task_woken == pdTRUE;
}
Obj enemies[14], shots[36], hostile[24], pickups[5];
st77922_lcd_init_cmd_t lcd_init_cmds[64];
uint8_t lcd_init_data[64][16];
size_t lcd_init_count = 0;
void add_init(uint8_t cmd, int delay, std::initializer_list<uint8_t> data) {
    size_t n = data.size();
    uint8_t *dst = lcd_init_data[lcd_init_count];
    size_t i = 0;
    for (uint8_t b : data)
        dst[i++] = b;
    lcd_init_cmds[lcd_init_count++] = {cmd, dst, uint8_t(n), uint16_t(delay)};
}
void build_lcd_init() {
    add_init(0xF1, 0, {0x00});
    add_init(0x60, 0, {0x00, 0x00, 0x00});
    add_init(0x65, 0, {0x80});
    add_init(0x79, 0, {0x06});
    add_init(0x7B, 0, {0x00, 0x08, 0x08});
    add_init(0x80, 0, {0x55, 0x62, 0x2F, 0x17, 0xF0, 0x52, 0x70, 0xD2, 0x52, 0x62, 0xEA});
    add_init(0x81, 0, {0x26, 0x52, 0x72, 0x27});
    add_init(0x84, 0, {0x92, 0x25});
    add_init(0x87, 0, {0x10, 0x10, 0x58, 0x00, 0x02, 0x3A});
    add_init(
        0x88, 0,
        {0x00, 0x00, 0x2C, 0x10, 0x04, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x00, 0x06});
    add_init(0x89, 0, {0x00, 0x00, 0x00});
    add_init(0x8A, 0, {0x13, 0x00, 0x2C, 0x00, 0x00, 0x2C, 0x10, 0x10, 0x00, 0x3E, 0x19});
    add_init(0x8B, 0, {0x15, 0xB1, 0xB1, 0x44, 0x96, 0x2C, 0x10, 0x97, 0x8E});
    add_init(0x8C, 0,
             {0x1D, 0xB1, 0xB1, 0x44, 0x96, 0x2C, 0x10, 0x50, 0x0F, 0x01, 0xC5, 0x12, 0x09});
    add_init(0x8D, 0, {0x0C});
    add_init(0x8E, 0, {0x33, 0x01, 0x0C, 0x13, 0x01, 0x01});
    add_init(0xB3, 0, {0x00, 0x30});
    add_init(0xF1, 0, {0x00});
    add_init(0x71, 0, {0xD0});
    add_init(0x66, 0, {0x02, 0x3F});
    add_init(0xBE, 0, {0x26, 0x00, 0x9D});
    add_init(0x70, 0, {0x01, 0xA0, 0x11, 0x40, 0xE0, 0x00, 0x11, 0x69, 0x11, 0x00, 0x00, 0x1A});
    add_init(0x90, 0, {0x04, 0x04, 0x55, 0x74, 0x00, 0x40, 0x43, 0x27, 0x27});
    add_init(0x91, 0, {0x04, 0x04, 0x55, 0x75, 0x00, 0x40, 0x42, 0x27, 0x27});
    add_init(0x92, 0, {0x04, 0x44, 0x55, 0xC0, 0x06, 0x00, 0x07, 0x05, 0x90, 0x27});
    add_init(0x93, 0, {0x04, 0x43, 0x11, 0x00, 0x00, 0x00, 0x00, 0x05, 0x90, 0x27});
    add_init(0x94, 0, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00});
    add_init(0x95, 0, {0x96, 0x16, 0x00, 0x00, 0xFF});
    add_init(0x96, 0, {0x44, 0x53, 0x03, 0x12, 0x23, 0x24, 0x06, 0x05, 0x94, 0x27, 0x00, 0x44});
    add_init(0x97, 0, {0x44, 0x53, 0x47, 0x56, 0x20, 0x20, 0x02, 0x01, 0x94, 0x27, 0x00, 0x44});
    add_init(0xBA, 0, {0x55, 0x94, 0x2D, 0x94, 0x27});
    add_init(0x9A, 0, {0x40, 0x00, 0x06, 0x00, 0x00, 0x00, 0x00});
    add_init(0x9B, 0, {0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x00});
    add_init(0x9C, 0,
             {0x5C, 0x12, 0x00, 0x00, 0x10, 0x12, 0x00, 0x00, 0x10, 0x02, 0x00, 0x00, 0x00});
    add_init(0x9D, 0, {0x8A, 0x51, 0x00, 0x00, 0x00, 0x80, 0x1E, 0x01});
    add_init(0x9E, 0, {0x51, 0x00, 0x00, 0x00, 0x80, 0x1E, 0x01});
    add_init(0xB4, 0, {0x1D, 0x1C, 0x1E, 0x0B, 0x14, 0x02, 0x13, 0x09, 0x1E, 0x00, 0x1E, 0x10});
    add_init(0xB5, 0, {0x1D, 0x1C, 0x1E, 0x0A, 0x15, 0x03, 0x11, 0x08, 0x1E, 0x01, 0x1E, 0x12});
    add_init(0xB6, 0, {0x77, 0x77, 0x00, 0x0A, 0xFF, 0x0A, 0xFF});
    add_init(0x86, 0,
             {0xCD, 0x04, 0xB1, 0x02, 0x58, 0x12, 0x58, 0x0C, 0x13, 0x01, 0xA5, 0x00, 0xA5, 0xA5});
    add_init(0xB7, 0,
             {0x07, 0x0A, 0x0E, 0x06, 0x05, 0x03, 0x2B, 0x03, 0x03, 0x42, 0x07, 0x10, 0x10, 0x2E,
              0x3F, 0x0D});
    add_init(0xB8, 0,
             {0x07, 0x0A, 0x0D, 0x05, 0x05, 0x02, 0x2B, 0x02, 0x03, 0x42, 0x06, 0x10, 0x0F, 0x2E,
              0x3F, 0x0D});
    add_init(0xB9, 0, {0x23, 0x23});
    add_init(0xBF, 0, {0x10, 0x14, 0x14, 0x0B, 0x0B, 0x0B});
    add_init(0xF2, 0, {0x00});
    add_init(0x73, 0, {0x04, 0xDA, 0x12, 0x54, 0x47});
    add_init(0x77, 0, {0x6B, 0x5B, 0xFD, 0xC3, 0xC5});
    add_init(0x7A, 0, {0x15, 0x27});
    add_init(0x7B, 0, {0x04, 0x57});
    add_init(0x7E, 0, {0x01, 0x0E});
    add_init(0xBF, 0, {0x36});
    add_init(0xE3, 0, {0x40, 0x40});
    add_init(0xF0, 0, {0x00});
    add_init(0xD0, 0, {0x00});
    add_init(0x2A, 0, {0x00, 0x00, 0x01, 0x3F});
    add_init(0x2B, 0, {0x00, 0x00, 0x01, 0xDF});
    add_init(0x21, 0, {});
    add_init(0x11, 120, {});
    add_init(0x29, 0, {});
    add_init(0x2C, 0, {});
    add_init(0x3A, 0, {0x01});
    add_init(0x36, 0, {0x00});
    add_init(0x35, 20, {0x01});
}
uint32_t random32() {
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}
int rnd(int n) {
    return int(random32() % uint32_t(n));
}
void clear(uint16_t c) {
    std::fill(fb, fb + W * H, c);
}
void px(int x, int y, uint16_t c) {
    if ((unsigned)x < W && (unsigned)y < H)
        fb[y * W + x] = c;
}
void rect(int x, int y, int w, int h, uint16_t c) {
    int x0 = std::max(0, x), x1 = std::min(W, x + w), y0 = std::max(0, y), y1 = std::min(H, y + h);
    for (int yy = y0; yy < y1; yy++)
        std::fill(fb + yy * W + x0, fb + yy * W + x1, c);
}
void circle(int cx, int cy, int r, uint16_t c) {
    for (int y = -r; y <= r; y++)
        for (int x = -r; x <= r; x++)
            if (x * x + y * y <= r * r)
                px(cx + x, cy + y, c);
}
// Compact 3x5 uppercase glyphs keep the firmware independent of external font assets.
const uint16_t glyphs[38] = {0x7B6F, 0x2A4A, 0x73E7, 0x73CF, 0x5BC9, 0x79CF, 0x79EF, 0x7249,
                             0x7BEF, 0x7BCF, 0x7BED, 0x6B6E, 0x4927, 0x6B6B, 0x5B6D, 0x7B6F,
                             0x7BED, 0x7B6F, 0x79ED, 0x724F, 0x5B6B, 0x5B6A, 0x5B6B, 0x5AAD,
                             0x5AAB, 0x72A7, 0x0000, 0x2A49, 0x0000, 0x0000, 0x0000, 0x0000,
                             0x7B6F, 0x5B6B, 0x4927, 0x6B6B, 0x0000, 0x0000};
int gi(char c) {
    if (c >= 'A' && c <= 'Z')
        return c - 'A';
    if (c >= '0' && c <= '9')
        return 26 + c - '0';
    if (c == '-')
        return 36;
    if (c == ':')
        return 37;
    return 27;
}
void text(const char *s, int x, int y, int scale, uint16_t color) {
    int start = x;
    for (; *s; s++) {
        if (*s == '\n') {
            y += 6 * scale;
            x = start;
            continue;
        }
        int g = gi(*s);
        uint16_t bits = glyphs[g];
        for (int row = 0; row < 5; row++)
            for (int col = 0; col < 3; col++)
                if (bits & (1u << (14 - (row * 3 + col))))
                    rect(x + col * scale, y + row * scale, scale, scale, color);
        x += 4 * scale;
    }
}
void ship() {
    circle(ship_x, ship_y, 8, C_CYAN);
    rect(ship_x - 2, ship_y - 3, 12, 6, C_WHITE);
    rect(ship_x - 12, ship_y - 2, 5, 4, C_AMBER);
}
void cue(int hz, int ms) {
    if (audio_queue && !muted) {
        Cue q{uint16_t(hz), uint16_t(ms)};
        xQueueSend(audio_queue, &q, 0);
    }
}
void audio_task(void *) {
    Cue c;
    int16_t pcm[512 * 2];
    for (;;) {
        if (xQueueReceive(audio_queue, &c, portMAX_DELAY) != pdTRUE)
            continue;
        int total = AUDIO_SAMPLE_RATE * c.ms / 1000, at = 0;
        float phase = 0;
        while (at < total) {
            int n = std::min(512, total - at);
            for (int i = 0; i < n; i++) {
                float env = std::min(1.0f, std::min((at + i) / 240.0f, (total - at - i) / 480.0f));
                int16_t v = int16_t(std::sin(phase) * env * 2400);
                phase += 6.2831853f * c.hz / AUDIO_SAMPLE_RATE;
                pcm[i * 2] = pcm[i * 2 + 1] = v;
            }
            size_t written = 0;
            i2s_channel_write(i2s_tx, pcm, n * 4, &written, pdMS_TO_TICKS(80));
            at += n;
        }
    }
}
void reset_game() {
    score = 0;
    lives = 3;
    elapsed = 0;
    enemy_timer = .5f;
    fire_timer = 0;
    charge = 0;
    boss_hp = 180;
    boss_x = 550;
    boss_y = 130;
    boss_phase = 1;
    boss_weak_open = true;
    wave = 0;
    invulnerable = 1.8f;
    memset(enemies, 0, sizeof(enemies));
    memset(shots, 0, sizeof(shots));
    memset(hostile, 0, sizeof(hostile));
    memset(pickups, 0, sizeof(pickups));
    ship_x = 75;
    ship_y = PLAY_H / 2;
    mode = 1;
    cue(330, 160);
}
void add_shot(float x, float y, float vx, float vy, int damage, int life) {
    for (auto &s : shots)
        if (!s.life) {
            s = {x, y, vx, vy, float(damage), 0, 0, life};
            return;
        }
}
void spawn_enemy() {
    for (auto &e : enemies)
        if (!e.life) {
            int k = (elapsed < 24) ? 0 : rnd(3);
            e = {float(W + 15),
                 float(38 + rnd(PLAY_H - 65)),
                 0,
                 0,
                 float(k == 2 ? 4 : k == 1 ? 2 : 1),
                 0,
                 k,
                 1};
            return;
        }
}
void emit_hostile(float x, float y, float vx, float vy) {
    for (auto &s : hostile)
        if (!s.life) {
            s = {x, y, vx, vy, 0, 0, 0, 1};
            return;
        }
}
void update(float dt) {
    if (mode != 1)
        return;
    elapsed += dt;
    invulnerable = std::max(0.0f, invulnerable - dt);
    if (dragging) {
        ship_x = std::clamp(ship_x, 18, W - 24);
        ship_y = std::clamp(ship_y, 24, PLAY_H - 20);
    }
    fire_timer -= dt;
    enemy_timer -= dt;
    if (fire_timer <= 0) {
        add_shot(ship_x + 12, ship_y, 370, 0, 1, 1);
        fire_timer = .22f;
        cue(640, 35);
    }
    if (!wave) {
        if (elapsed < 160 && enemy_timer <= 0) {
            spawn_enemy();
            enemy_timer = elapsed < 30 ? 1.0f : 1.35f;
        }
        if (elapsed >= 160)
            wave = 1;
    }
    if (wave) {
        boss_x = std::max(382.0f, boss_x - 38 * dt);
        boss_y = PLAY_H / 2 + std::sin(elapsed * 1.1f) * 54;
        boss_phase = boss_hp > 120 ? 1 : boss_hp > 55 ? 2 : 3;
        boss_weak_open = std::sin(elapsed * (boss_phase == 1 ? 1.3f : 2.0f)) > .1f;
        if (rnd(boss_phase == 1 ? 30 : boss_phase == 2 ? 19 : 12) == 0) {
            for (int i = 0; i < boss_phase * 2 + 1; i++)
                emit_hostile(boss_x - 25, boss_y, -145, (i - boss_phase) * 48);
            cue(150, 55);
        }
    }
    for (auto &e : enemies)
        if (e.life) {
            e.x -= (80 + e.kind * 25) * dt;
            e.age += dt;
            e.y += std::sin(e.age * 2.6f) * 25 * dt;
            if (rnd(50) == 0)
                emit_hostile(e.x, e.y, -120, 0);
            if (e.x < 0)
                e.life = 0;
            if (std::abs(e.x - ship_x) < 16 && std::abs(e.y - ship_y) < 16 && invulnerable <= 0) {
                lives--;
                invulnerable = 1.8f;
                cue(145, 300);
                if (!lives)
                    mode = 3;
            }
        }
    for (auto &s : shots)
        if (s.life) {
            s.x += s.vx * dt;
            if (wave && std::abs(s.x - (boss_x - 26)) < 35 && std::abs(s.y - boss_y) < 34) {
                if (boss_weak_open) {
                    boss_hp -= s.hp;
                    score += 10;
                }
                s.life = 0;
                if (boss_hp <= 0) {
                    score += 5000;
                    mode = 4;
                    cue(520, 600);
                }
            } else
                for (auto &e : enemies)
                    if (e.life && std::abs(e.x - s.x) < 18 && std::abs(e.y - s.y) < 17) {
                        e.hp -= s.hp;
                        s.life = 0;
                        if (e.hp <= 0) {
                            e.life = 0;
                            score += 100 * (e.kind + 1);
                            cue(390, 75);
                            if (rnd(10) == 0)
                                for (auto &p : pickups)
                                    if (!p.life) {
                                        p = {e.x, e.y, -52, 0, 0, 0, 0, 1};
                                        break;
                                    }
                        }
                    }
            if (s.x > W)
                s.life = 0;
        }
    for (auto &s : hostile)
        if (s.life) {
            s.x += s.vx * dt;
            s.y += s.vy * dt;
            if (std::abs(s.x - ship_x) < 12 && std::abs(s.y - ship_y) < 12) {
                s.life = 0;
                if (invulnerable <= 0 && lives > 0) {
                    lives--;
                    invulnerable = 1.8f;
                    cue(145, 300);
                    if (!lives)
                        mode = 3;
                }
            }
            if (s.x < 0 || s.y < 0 || s.y > PLAY_H)
                s.life = 0;
        }
    for (auto &p : pickups)
        if (p.life) {
            p.x += p.vx * dt;
            if (std::abs(p.x - ship_x) < 20 && std::abs(p.y - ship_y) < 20) {
                p.life = 0;
                lives = std::min(5, lives + 1);
                score += 500;
                cue(750, 110);
            } else if (p.x < 0)
                p.life = 0;
        }
    if (wave && boss_hp <= 0)
        mode = 4;
}
void draw() {
    clear(C_BG);
    for (int i = 0; i < 64; i++) {
        int x = (i * 79 + int(elapsed * (5 + i % 4))) % W, y = (i * 47) % PLAY_H;
        px(x, y, (i % 3) ? 0x4B4D : 0xB79E);
    }
    rect(0, 0, W, 24, 0x1083);
    text("STARWAKE", 8, 7, 2, C_CYAN);
    text("SCORE", 175, 8, 1, C_WHITE);
    char buf[20];
    snprintf(buf, sizeof(buf), "%07d", score);
    text(buf, 210, 8, 1, C_AMBER);
    text("HULL", 365, 8, 1, C_WHITE);
    for (int i = 0; i < lives; i++)
        rect(398 + i * 15, 8, 10, 8, i < 3 ? C_RED : C_CYAN);
    for (auto &e : enemies)
        if (e.life) {
            uint16_t col = e.kind == 0 ? C_RED : e.kind == 1 ? C_PURPLE : C_AMBER;
            circle(int(e.x), int(e.y), e.kind == 2 ? 12 : 8, col);
            rect(int(e.x) - 4, int(e.y) - 2, 10, 4, C_DARK);
        }
    if (wave) {
        circle(int(boss_x), int(boss_y), 31, C_PURPLE);
        circle(int(boss_x) - 17, int(boss_y), 13, boss_weak_open ? C_CYAN : C_RED);
        rect(130, 28, 220, 5, 0x310B);
        rect(130, 28, int(220 * std::max(0.0f, boss_hp) / 180), 5, C_RED);
        text("RIFT WARDEN", 184, 36, 1, C_AMBER);
        text(boss_phase == 1   ? "PHASE 1"
             : boss_phase == 2 ? "PHASE 2"
                               : "PHASE 3",
             330, 36, 1, C_AMBER);
    }
    for (auto &s : shots)
        if (s.life)
            rect(int(s.x), int(s.y) - 2, 8, 4, C_CYAN);
    for (auto &s : hostile)
        if (s.life)
            circle(int(s.x), int(s.y), 4, C_RED);
    for (auto &p : pickups)
        if (p.life) {
            circle(int(p.x), int(p.y), 8, 0xA7F5);
            rect(int(p.x) - 2, int(p.y) - 5, 4, 10, C_WHITE);
            rect(int(p.x) - 5, int(p.y) - 2, 10, 4, C_WHITE);
        }
    if ((mode == 1 || mode == 2 || mode == 3) && (invulnerable <= 0 || int(elapsed * 14) % 2 == 0))
        ship();
    rect(0, PLAY_H, W, BAR, 0x10A3);
    rect(0, PLAY_H, W, 1, 0x52F0);
    rect(6, PLAY_H + 6, 80, 38, 0x2148);
    text(mode == 1 ? "PAUSE" : "RESUME", 16, PLAY_H + 17, 1, C_WHITE);
    rect(94, PLAY_H + 6, 80, 38, 0x2148);
    text(muted ? "SOUND OFF" : "SOUND", 111, PLAY_H + 17, 1, C_WHITE);
    rect(350, PLAY_H + 5, 124, 40, charging ? 0x7E8C : 0x2130);
    text("CHARGE", 379, PLAY_H + 10, 1, C_WHITE);
    rect(360, PLAY_H + 29, 100, 7, 0x0861);
    rect(360, PLAY_H + 29, charge, 7, C_CYAN);
    if (mode == 0 || mode >= 2) {
        rect(50, 75, 380, 152, 0x0861);
        rect(50, 75, 380, 1, C_CYAN);
        text(mode == 0   ? "THE RIFT IS OPEN"
             : mode == 2 ? "FLIGHT PAUSED"
             : mode == 3 ? "SHIP LOST"
                         : "RIFT SEALED",
             110, 99, 2, C_WHITE);
        text(mode == 0 ? "TOUCH TO LAUNCH" : "TOUCH TO CONTINUE", 145, 142, 1, C_AMBER);
        text("DRAG TO MOVE  AUTO FIRE", 113, 167, 1, C_CYAN);
    }
}
void init_display() {
    build_lcd_init();
    spi_bus_config_t bus{};
    bus.sclk_io_num = 12;
    bus.data0_io_num = 11;
    bus.data1_io_num = 13;
    bus.data2_io_num = 14;
    bus.data3_io_num = 9;
    bus.max_transfer_sz = 320 * 16 * 2;
    bus.flags = SPICOMMON_BUSFLAG_QUAD;
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO));
    esp_lcd_panel_io_handle_t io = nullptr;
    esp_lcd_panel_io_spi_config_t ioc{};
    ioc.cs_gpio_num = 10;
    ioc.dc_gpio_num = GPIO_NUM_NC;
    ioc.spi_mode = 0;
    ioc.pclk_hz = 80000000;
    ioc.trans_queue_depth = 1;
    ioc.lcd_cmd_bits = 32;
    ioc.lcd_param_bits = 8;
    ioc.flags.quad_mode = true;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &ioc, &io));
    lcd_transfer_done = xSemaphoreCreateBinary();
    assert(lcd_transfer_done);
    esp_lcd_panel_io_callbacks_t io_callbacks{};
    io_callbacks.on_color_trans_done = on_color_transfer_done;
    ESP_ERROR_CHECK(
        esp_lcd_panel_io_register_event_callbacks(io, &io_callbacks, lcd_transfer_done));
    st77922_vendor_config_t vc{};
    vc.init_cmds = lcd_init_cmds;
    vc.init_cmds_size = lcd_init_count;
    vc.flags.use_qspi_interface = 1;
    esp_lcd_panel_dev_config_t pc{};
    pc.reset_gpio_num = GPIO_NUM_NC;
    pc.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR;
    pc.bits_per_pixel = 16;
    pc.vendor_config = &vc;
    ESP_ERROR_CHECK(esp_lcd_new_panel_st77922(io, &pc, &panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel, true));
    gpio_config_t bl{};
    bl.pin_bit_mask = 1ULL << 41;
    bl.mode = GPIO_MODE_OUTPUT;
    ESP_ERROR_CHECK(gpio_config(&bl));
    gpio_set_level((gpio_num_t)41, 1);
    fb =
        (uint16_t *)heap_caps_malloc(W * H * sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!fb)
        fb = (uint16_t *)heap_caps_malloc(W * H * sizeof(uint16_t),
                                          MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    assert(fb);
    physical_fb = (uint16_t *)heap_caps_malloc(320 * 16 * sizeof(uint16_t),
                                               MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    assert(physical_fb);
}
void init_touch() {
    i2c_master_bus_config_t b{};
    b.i2c_port = I2C_NUM_0;
    b.sda_io_num = (gpio_num_t)38;
    b.scl_io_num = (gpio_num_t)39;
    b.clk_source = I2C_CLK_SRC_DEFAULT;
    b.glitch_ignore_cnt = 7;
    b.flags.enable_internal_pullup = true;
    ESP_ERROR_CHECK(i2c_new_master_bus(&b, &i2c_bus));
    i2c_device_config_t dev{};
    dev.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev.device_address = 0x55;
    dev.scl_speed_hz = 400000;
    ESP_ERROR_CHECK(i2c_master_bus_add_device(i2c_bus, &dev, &touch_dev));
    gpio_config_t tr{};
    tr.pin_bit_mask = 1ULL << 48;
    tr.mode = GPIO_MODE_OUTPUT;
    ESP_ERROR_CHECK(gpio_config(&tr));
    gpio_set_level((gpio_num_t)48, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level((gpio_num_t)48, 1);
    vTaskDelay(pdMS_TO_TICKS(100));
    gpio_config_t boot{};
    boot.pin_bit_mask = 1ULL;
    boot.mode = GPIO_MODE_INPUT;
    boot.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_config(&boot);
}
bool read_touch(uint16_t &x, uint16_t &y) {
    uint8_t reg[2] = {0x00, 0x10}, info = 0;
    if (i2c_master_transmit_receive(touch_dev, reg, 2, &info, 1, 30) != ESP_OK || !(info & 0x08))
        return false;
    uint8_t p[35] = {}, addr[2] = {0, 0x14};
    if (i2c_master_transmit_receive(touch_dev, addr, 2, p, sizeof(p), 30) != ESP_OK ||
        !(p[0] & 0x80))
        return false;
    uint16_t rx = ((p[0] & 0x3f) << 8) | p[1], ry = ((p[2] & 0x3f) << 8) | p[3];
    x = ry;
    y = uint16_t(std::clamp(319 - int(rx), 0, 319));
    return true;
}
void flush() {
    for (int py = 0; py < W; py += 16) {
        for (int r = 0; r < 16; r++)
            for (int px = 0; px < 320; px++) {
                uint16_t c = fb[(319 - px) * W + (py + r)];
                physical_fb[r * 320 + px] = uint16_t((c << 8) | (c >> 8));
            }
        ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel, 0, py, 320, py + 16, physical_fb));
        // The QSPI/DMA transfer reads this staging buffer asynchronously. Reuse it only
        // after the panel IO completion callback reports that DMA has finished.
        xSemaphoreTake(lcd_transfer_done, portMAX_DELAY);
    }
}
void init_audio() {
    i2s_chan_config_t ch = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
    if (i2s_new_channel(&ch, &i2s_tx, nullptr) != ESP_OK)
        return;
    i2s_std_config_t std{};
    std.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_SAMPLE_RATE);
    std.slot_cfg =
        I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
    std.gpio_cfg.mclk = (gpio_num_t)17;
    std.gpio_cfg.bclk = (gpio_num_t)18;
    std.gpio_cfg.ws = (gpio_num_t)21;
    std.gpio_cfg.dout = (gpio_num_t)15;
    std.gpio_cfg.din = GPIO_NUM_NC;
    if (i2s_channel_init_std_mode(i2s_tx, &std) != ESP_OK || i2s_channel_enable(i2s_tx) != ESP_OK)
        return;
    audio_codec_i2c_cfg_t ic{};
    ic.addr = 0x18;
    ic.bus_handle = i2c_bus;
    const audio_codec_ctrl_if_t *ctrl = audio_codec_new_i2c_ctrl(&ic);
    const audio_codec_gpio_if_t *gpio = audio_codec_new_gpio();
    if (!ctrl || !gpio)
        return;
    es8311_codec_cfg_t ec{};
    ec.ctrl_if = ctrl;
    ec.gpio_if = gpio;
    ec.codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC;
    ec.master_mode = false;
    ec.use_mclk = true;
    ec.pa_pin = 1;
    ec.pa_reverted = true;
    ec.mclk_div = I2S_MCLK_MULTIPLE_256;
    const audio_codec_if_t *cif = es8311_codec_new(&ec);
    audio_codec_i2s_cfg_t dc{};
    dc.port = I2S_NUM_1;
    dc.tx_handle = i2s_tx;
    const audio_codec_data_if_t *dif = audio_codec_new_i2s_data(&dc);
    esp_codec_dev_cfg_t config{};
    config.dev_type = ESP_CODEC_DEV_TYPE_OUT;
    config.codec_if = cif;
    config.data_if = dif;
    codec = esp_codec_dev_new(&config);
    if (!codec)
        return;
    esp_codec_dev_sample_info_t sample{};
    sample.sample_rate = AUDIO_SAMPLE_RATE;
    sample.channel = 2;
    sample.bits_per_sample = 16;
    sample.channel_mask = 0x03;
    sample.mclk_multiple = I2S_MCLK_MULTIPLE_256;
    if (esp_codec_dev_open(codec, &sample) != ESP_CODEC_DEV_OK) {
        codec = nullptr;
        return;
    }
    esp_codec_dev_set_out_vol(codec, 45);
    audio_queue = xQueueCreate(8, sizeof(Cue));
    if (audio_queue)
        xTaskCreate(audio_task, "audio", 4096, nullptr, 4, nullptr);
}
void app() {
    init_display();
    init_touch();
    init_audio();
    bool was_down = false;
    while (true) {
        uint16_t tx = 0, ty = 0;
        bool down = read_touch(tx, ty);
        if (down) {
            int x = std::clamp(int(tx), 0, W - 1), y = std::clamp(int(ty), 0, H - 1);
            if (y >= PLAY_H) {
                if (!was_down && x < 90) {
                    mode = mode == 1 ? 2 : (mode == 2 ? 1 : mode);
                } else if (!was_down && x < 185) {
                    muted = !muted;
                    if (codec)
                        esp_codec_dev_set_out_mute(codec, muted);
                } else if (x > 340) {
                    charging = true;
                    charge = std::min(100, charge + 4);
                }
            } else {
                if (mode == 0)
                    reset_game();
                else if (mode == 2)
                    mode = 1;
                else if (mode >= 3)
                    reset_game();
                else {
                    dragging = true;
                    ship_x = x;
                    ship_y = y;
                }
            }
        } else {
            dragging = false;
            if (charging) {
                charging = false;
                add_shot(ship_x + 10, ship_y, 430, 0, 5 + charge / 20, 1);
                charge = 0;
            }
        }
        was_down = down;
        static int prev = 1;
        int bp = gpio_get_level((gpio_num_t)0);
        if (!bp && prev) {
            mode = mode == 1 ? 2 : (mode == 2 ? 1 : mode);
        }
        prev = bp;
        update(.04f);
        draw();
        flush();
        vTaskDelay(pdMS_TO_TICKS(40));
    }
}
} // namespace
extern "C" void app_main() {
    app();
}
