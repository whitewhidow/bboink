// Stub: SDLog — header-only no-op logger (Serial passthrough).
// The original wrote a rolling log to SD; tembed-oink only needs Serial output
// so the copied engine/config code compiles and is debuggable over USB.
#pragma once

#include <Arduino.h>
#include <cstdarg>

class SDLog {
public:
    static void init() {}
    static void setEnabled(bool) {}
    static bool isEnabled() { return false; }

    static void log(const char* tag, const char* format, ...) {
        char buf[160];
        va_list args;
        va_start(args, format);
        vsnprintf(buf, sizeof(buf), format, args);
        va_end(args);
        Serial.printf("[%s] %s\n", tag ? tag : "LOG", buf);
    }
    static void logRaw(const char* message) { Serial.println(message ? message : ""); }
    static void flush() {}
    static void close() {}
};

#define SDLOG(tag, fmt, ...) do { \
    Serial.printf("[%s] " fmt "\n", tag, ##__VA_ARGS__); \
} while(0)
