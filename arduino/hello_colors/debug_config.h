#pragma once

// Set to 1 to compile serial diagnostics and codec diagnostic tests.
#ifndef DEBUG
#define DEBUG 0
#endif

#if DEBUG
#define DEBUG_PRINT(...) Serial.print(__VA_ARGS__)
#define DEBUG_PRINTLN(...) Serial.println(__VA_ARGS__)
#define DEBUG_PRINTF(...) Serial.printf(__VA_ARGS__)
#else
#define DEBUG_PRINT(...) do {} while (0)
#define DEBUG_PRINTLN(...) do {} while (0)
#define DEBUG_PRINTF(...) do {} while (0)
#endif
