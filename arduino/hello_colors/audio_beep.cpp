#include "audio_beep.h"
#include "debug_config.h"
#include <Arduino.h>
#include <Wire.h>
#include <AudioBoard.h>
#include <driver/i2s_std.h>
#include <cmath>

namespace {
constexpr uint8_t CODEC_ADDRESS = 0x18; // Seven-bit I2C address.
constexpr int SAMPLE_RATE = 24000;
constexpr int BEEP_SAMPLES = SAMPLE_RATE / 5; // 200 ms at 880 Hz.
constexpr int VOLUME_PERCENT = 45;
audio_driver::DriverPins codecPins;
audio_driver::AudioDriverES8311Class codecDriver;
i2s_chan_handle_t audioTx = nullptr;
bool audioReady = false;

#if DEBUG
bool readRegister(uint8_t reg, uint8_t &value) {
    Wire.beginTransmission(CODEC_ADDRESS);
    Wire.write(reg);
    const uint8_t status = Wire.endTransmission(false);
    if (status != 0) {
        DEBUG_PRINTF("AUDIO ERROR: register 0x%02X address ACK failed: %u\n", reg, status);
        return false;
    }
    if (Wire.requestFrom(CODEC_ADDRESS, uint8_t(1)) != 1) {
        DEBUG_PRINTF("AUDIO ERROR: register 0x%02X read failed\n", reg);
        return false;
    }
    value = Wire.read();
    return true;
}

bool checkRegister(uint8_t reg, uint8_t expected, uint8_t mask = 0xFF) {
    uint8_t value = 0;
    if (!readRegister(reg, value)) return false;
    const bool ok = (value & mask) == (expected & mask);
    DEBUG_PRINTF("AUDIO readback: reg=0x%02X value=0x%02X expected=0x%02X mask=0x%02X %s\n",
                  reg, value, expected, mask, ok ? "PASS" : "FAIL");
    return ok;
}
#endif

bool checkResult(const char *operation, esp_err_t result) {
    DEBUG_PRINTF("AUDIO %s: %s\n", operation, esp_err_to_name(result));
    return result == ESP_OK;
}

bool writeAudio(const int16_t *samples, size_t bytes, size_t &totalWritten) {
    size_t written = 0;
    const esp_err_t result = i2s_channel_write(audioTx, samples, bytes, &written,
                                             1000);
#if DEBUG
    totalWritten += written;
#endif
    if (result != ESP_OK || written != bytes) {
        DEBUG_PRINTF("AUDIO ERROR: I2S write %s, bytes=%u/%u\n", esp_err_to_name(result),
                      unsigned(written), unsigned(bytes));
        return false;
    }
    return true;
}
} // namespace

bool initBeepAudio() {
    // LCDWiki's amplifier enable is active LOW. Keep it off while configuring.
    pinMode(1, OUTPUT);
    digitalWrite(1, HIGH);
    if (!Wire.begin(38, 39, 100000)) {
        DEBUG_PRINTLN("AUDIO ERROR: I2C setup failed");
        return false;
    }
    Wire.setTimeOut(100);
    // Provide clocks before even probing the codec, including on a cold boot.
    i2s_chan_config_t channel = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
    channel.auto_clear = true;
    if (!checkResult("create I2S", i2s_new_channel(&channel, &audioTx, nullptr))) return false;
    i2s_std_config_t config{};
    config.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE);
    config.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
    config.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                        I2S_SLOT_MODE_STEREO);
    config.gpio_cfg.mclk = GPIO_NUM_17;
    config.gpio_cfg.bclk = GPIO_NUM_18;
    config.gpio_cfg.ws = GPIO_NUM_21;
    config.gpio_cfg.dout = GPIO_NUM_15;
    config.gpio_cfg.din = GPIO_NUM_NC;
    if (!checkResult("configure I2S", i2s_channel_init_std_mode(audioTx, &config)) ||
        !checkResult("enable I2S", i2s_channel_enable(audioTx))) return false;

    delay(100);
    uint8_t ack = 2;
    for (int attempt = 1; attempt <= 10; ++attempt) {
        Wire.beginTransmission(CODEC_ADDRESS);
        ack = Wire.endTransmission();
        DEBUG_PRINTF("AUDIO ES8311 probe at 0x18, attempt %d: %s (status=%u), SDA=%d SCL=%d\n",
                      attempt, ack == 0 ? "ACK" : "FAIL", ack, digitalRead(38), digitalRead(39));
        if (ack == 0) break;
        delay(100);
    }
    if (ack != 0) {
#if DEBUG
        DEBUG_PRINT("AUDIO I2C responding addresses:");
        for (uint8_t address = 1; address < 127; ++address) {
            Wire.beginTransmission(address);
            if (Wire.endTransmission() == 0) DEBUG_PRINTF(" 0x%02X", address);
        }
        DEBUG_PRINTLN();
#endif
        return false;
    }

#if DEBUG
    uint8_t id1 = 0, id2 = 0, revision = 0;
    if (!readRegister(0xFD, id1) || !readRegister(0xFE, id2) || !readRegister(0xFF, revision))
        return false;
    DEBUG_PRINTF("AUDIO chip ID: %02X %02X, revision=%02X\n", id1, id2, revision);

    AudioDriverLogger.begin(Serial, AudioDriverLogLevel::Info);
#else
    // The bundled library is compiled separately; disable every log level and
    // detach its output in the non-debug branch.
    LOGLEVEL_AUDIODRIVER = 4;
    AudioDriverLogger.end();
#endif
    // Wire is already initialized. No PA pin is registered with the library,
    // because its default PA polarity is opposite to this board's amplifier.
    codecPins.addI2C(audio_driver::PinsI2C(audio_driver::PinFunction::CODEC,
                                        39, 38, CODEC_ADDRESS, 100000, &Wire, false));
    codecPins.addI2S(audio_driver::PinFunction::CODEC, 17, 18, 21, 15, -1, 1);
    audio_driver::CodecConfig codecConfig;
    codecConfig.input_device = ADC_INPUT_NONE;
    codecConfig.output_device = DAC_OUTPUT_ALL;
    codecConfig.i2s.rate = RATE_24K;
    codecConfig.i2s.bits = BIT_LENGTH_16BITS;
    codecConfig.i2s.channels = CHANNELS2;
    codecConfig.i2s.fmt = I2S_NORMAL;
    codecConfig.i2s.mode = MODE_SLAVE;
    codecConfig.sd_active = false;
    const bool initialized = codecDriver.begin(codecConfig, codecPins);
    DEBUG_PRINTF("AUDIO codec initialization: %s\n", initialized ? "PASS" : "FAIL");
    if (!initialized) return false;

    // Exercise register readback only in diagnostic builds.
#if DEBUG
    bool commandsOk = codecDriver.setVolume(25);
    commandsOk = checkRegister(0x32, 25 * 2550 / 1000) && commandsOk;
    commandsOk = codecDriver.setMute(true) && commandsOk;
    commandsOk = checkRegister(0x31, 0x60, 0x60) && commandsOk;
    commandsOk = codecDriver.setMute(false) && commandsOk;
    commandsOk = checkRegister(0x31, 0, 0x60) && commandsOk;
    commandsOk = codecDriver.setVolume(VOLUME_PERCENT) && commandsOk;
    commandsOk = checkRegister(0x32, VOLUME_PERCENT * 2550 / 1000) && commandsOk;
#else
    bool commandsOk = codecDriver.setMute(false);
    commandsOk = codecDriver.setVolume(VOLUME_PERCENT) && commandsOk;
#endif
    audioReady = commandsOk;
    digitalWrite(1, audioReady ? LOW : HIGH);
    DEBUG_PRINTF("AUDIO ready=%s, amplifier GPIO1=%s, 24000 Hz / 16-bit stereo / MCLK=6144000 Hz\n",
                  audioReady ? "YES" : "NO", audioReady ? "LOW (enabled)" : "HIGH (disabled)");
    return audioReady;
}

void playBeep() {
    if (!audioReady) {
        DEBUG_PRINTLN("AUDIO beep skipped: initialization or codec readback failed");
        return;
    }
#if DEBUG
    const bool volumeOk = checkRegister(0x32, VOLUME_PERCENT * 2550 / 1000);
    const bool muteOk = checkRegister(0x31, 0, 0x60);
    if (!volumeOk || !muteOk) {
        DEBUG_PRINTLN("AUDIO beep skipped: codec state check failed");
        return;
    }
#endif

    int16_t samples[256 * 2];
    size_t totalWritten = 0;
    for (int offset = 0; offset < BEEP_SAMPLES; offset += 256) {
        const int count = min(256, BEEP_SAMPLES - offset);
        for (int i = 0; i < count; ++i) {
            const int n = offset + i;
            // Brief fade-in/out avoids sharp clicks at the beep boundaries.
            const float envelope = min(1.0f, min(n / 120.0f, (BEEP_SAMPLES - 1 - n) / 120.0f));
            const int16_t value = int16_t(6000.0f * envelope *
                                         sinf(2.0f * PI * 880.0f * n / SAMPLE_RATE));
            samples[2 * i] = samples[2 * i + 1] = value;
        }
        if (!writeAudio(samples, count * 2 * sizeof(int16_t), totalWritten)) return;
    }
    memset(samples, 0, sizeof(samples));
    // Flush with silence and let queued samples leave the peripheral.
    for (int i = 0; i < 4; ++i)
        if (!writeAudio(samples, sizeof(samples), totalWritten)) return;
    delay(50);
    DEBUG_PRINTF("AUDIO beep: 880 Hz / 200 ms, I2S bytes=%u PASS, free heap=%u\n",
                  unsigned(totalWritten), unsigned(ESP.getFreeHeap()));
}
