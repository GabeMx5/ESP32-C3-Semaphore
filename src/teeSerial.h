#pragma once
#include <Arduino.h>
#include <functional>

// ─── TeeSerial ─────────────────────────────────────────────────────────────
// Intercepts every write to Serial and forwards it to the real hardware serial
// AND buffers lines for the onLine callback (used to stream output to the web
// console via WebSocket).
//
// IMPORTANT: this header must be included BEFORE all other headers in main.cpp.
// The class definition appears first, so all Serial.xxx calls inside the class
// body still refer to the real hardware serial object. The #define at the bottom
// then redirects every subsequent Serial.xxx in the translation unit to teeSerial.

class TeeSerial : public Stream {
public:
    // Called with each complete output line (trimmed, without newline)
    std::function<void(const String&)> onLine;

    // ── Write ── (all output paths end here) ────────────────────────────────
    // NOTE: "Serial" below refers to the REAL hardware serial because this code
    // is compiled before the "#define Serial teeSerial" that follows the class.

    size_t write(uint8_t c) override {
        size_t r = Serial.write(c);
        _buf += (char)c;
        if (c == '\n') _flush();
        return r;
    }

    size_t write(const uint8_t* buf, size_t size) override {
        size_t r = Serial.write(buf, size);
        for (size_t i = 0; i < size; i++) {
            _buf += (char)buf[i];
            if (buf[i] == '\n') _flush();
        }
        return r;
    }

    // ── Read (forwarded to real serial, used by SerialConsole::loop) ─────────
    int  available() override { return Serial.available(); }
    int  read()      override { return Serial.read();      }
    int  peek()      override { return Serial.peek();      }
    void flush()     override { Serial.flush();            }

    // ── Init ─────────────────────────────────────────────────────────────────
    void begin(unsigned long baud) { Serial.begin(baud); }

private:
    String _buf;

    void _flush() {
        String line = _buf;
        _buf = "";
        line.trim();
        if (line.length() > 0 && onLine) onLine(line);
    }
};

// Global instance — defined in main.cpp
extern TeeSerial teeSerial;

// Redirect all Serial.xxx calls that appear after this point to teeSerial.
// Applies to every header included after this one in the same translation unit.
#undef  Serial
#define Serial teeSerial
