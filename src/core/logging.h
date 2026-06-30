#pragma once

#ifndef PORKCHOP_LOG_ENABLED
#define PORKCHOP_LOG_ENABLED 1
#endif

#if !PORKCHOP_LOG_ENABLED
#ifdef __cplusplus
// Ensure core Serial is declared before we override it.
#ifdef ARDUINO
#include <Arduino.h>
#endif

#if !defined(ARDUINO_CORE_BUILD)
// Compile-time Serial sink to disable all logging with minimal overhead.
struct PorkchopNullSerial {
    void begin(unsigned long, uint8_t = 0) {}
    void end() {}
    void flush() {}
    void setTimeout(unsigned long) {}
    unsigned long getTimeout() { return 0; }
    void setDebugOutput(bool) {}
    int available() { return 0; }
    int read() { return -1; }
    int peek() { return -1; }

    template <typename... Args>
    size_t printf(const char*, Args...) { return 0; }

    template <typename T>
    size_t print(const T&) { return 0; }

    size_t print(const char*) { return 0; }
    size_t print(char) { return 0; }

    template <typename T>
    size_t println(const T&) { return 0; }

    size_t println() { return 0; }

    size_t write(uint8_t) { return 1; }
    size_t write(const uint8_t*, size_t n) { return n; }
    operator bool() const { return false; }
};

static PorkchopNullSerial PorkchopSerialSink;
#undef Serial
#define Serial PorkchopSerialSink
#endif  // !ARDUINO_CORE_BUILD
#endif  // __cplusplus
#endif
