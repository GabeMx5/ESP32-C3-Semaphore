#pragma once
#include <Arduino.h>
#include <functional>
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// ─── TeeSerial ─────────────────────────────────────────────────────────────
// Intercepts every write to Serial and forwards it to the real hardware serial
// AND buffers lines for the web console (WebSocket), draining one per loop tick
// to avoid flooding the AsyncWebSocket send buffer.
//
// The queue is a FIXED-CAPACITY ring buffer: when it is full the oldest line is
// overwritten and a drop counter is bumped. This is essential because loop()
// only drains the queue while at least one client is viewing the CON tab. With
// an unbounded queue, a device left running with no browser attached kept every
// log line it had ever printed: BambuLab alone logs "connect failed" every 10 s
// while the printer is unreachable (~40 KB/h), which exhausted the heap after a
// few hours and hung the device. Ring slots keep their String buffers between
// uses, so steady-state logging does not fragment the heap either.
//
// Serial is written from several FreeRTOS tasks (Arduino loop, BambuLab
// reconnect, weather/air-quality fetch) while the Arduino loop drains the
// queue, so the line buffer and the ring are protected by a recursive mutex.
// A caller that cannot take the lock quickly falls back to writing to the
// hardware serial only — logging must never block or deadlock the firmware.
//
// IMPORTANT: this header must be included BEFORE all other headers in main.cpp.
// The class definition appears first, so all Serial.xxx calls inside the class
// body still refer to the real hardware serial object. The #define at the bottom
// then redirects every subsequent Serial.xxx in the translation unit to teeSerial.

class TeeSerial : public Stream {
public:
    TeeSerial() { _lock = xSemaphoreCreateRecursiveMutex(); }

    // Returns the next queued console JSON message, or an empty String if none.
    String drainOne() {
        if (!_take()) return String();
        if (_dropped > 0) {
            uint32_t n = _dropped;
            _dropped = 0;
            _give();
            return _encode(String("--- ") + n + " console line(s) dropped (queue full) ---");
        }
        String msg;
        if (_count > 0) {
            msg   = _queue[_head];
            _head = (_head + 1) % MAX_QUEUE;
            _count--;
        }
        _give();
        return msg;
    }

    // Number of lines currently waiting to be pushed to the web console.
    size_t queued() {
        if (!_take()) return 0;
        size_t n = _count;
        _give();
        return n;
    }

    // Temporarily disable web broadcast (physical serial is unaffected)
    void setWebOutput(bool enabled) { _webOutput = enabled; }

    // ── Write ── (all output paths end here) ────────────────────────────────
    // NOTE: "Serial" below refers to the REAL hardware serial because this code
    // is compiled before the "#define Serial teeSerial" that follows the class.

    size_t write(uint8_t c) override {
        if (!_take()) return Serial.write(c);
        if (_lineStart) { _writeTimestamp(); _lineStart = false; }
        size_t r = Serial.write(c);
        _buf += (char)c;
        if (c == '\n' || _buf.length() >= MAX_LINE) {
            _flush();
            if (c == '\n') _lineStart = true;
        }
        _give();
        return r;
    }

    // Taking the lock once around the whole buffer keeps a single printf() from
    // being interleaved with output produced by another task.
    size_t write(const uint8_t* buf, size_t size) override {
        bool locked = _take();
        size_t r = 0;
        for (size_t i = 0; i < size; i++) r += write(buf[i]);
        if (locked) _give();
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
    static constexpr size_t MAX_LINE  = 512;  // max characters buffered per line
    static constexpr size_t MAX_QUEUE = 48;   // max lines held for the web console

    String            _buf;
    bool              _webOutput = true;
    bool              _lineStart = true;
    SemaphoreHandle_t _lock      = nullptr;

    // Ring buffer: _head is the oldest queued line, _count the number held.
    String   _queue[MAX_QUEUE];
    size_t   _head    = 0;
    size_t   _count   = 0;
    uint32_t _dropped = 0;

    bool _take() {
        if (!_lock) return false;
        return xSemaphoreTakeRecursive(_lock, pdMS_TO_TICKS(50)) == pdTRUE;
    }

    void _give() {
        if (_lock) xSemaphoreGiveRecursive(_lock);
    }

    static String _encode(const String& line) {
        JsonDocument doc;
        doc["type"] = "console";
        doc["text"] = line;
        String msg;
        serializeJson(doc, msg);
        return msg;
    }

    void _writeTimestamp() {
        char ts[16];
        snprintf(ts, sizeof(ts), "[%8lu] ", millis());
        Serial.print(ts);
        _buf += ts;
    }

    // Called with the lock held.
    void _flush() {
        _buf.trim();
        if (_buf.length() > 0 && _webOutput) {
            size_t tail = (_head + _count) % MAX_QUEUE;
            if (_count == MAX_QUEUE) {
                // Queue full: overwrite the oldest line and move the head on.
                _head = (_head + 1) % MAX_QUEUE;
                _dropped++;
            } else {
                _count++;
            }
            _queue[tail] = _encode(_buf);
        }
        // remove(0) resets the length but keeps the allocated capacity, so the
        // line buffer is not re-allocated on every single line.
        _buf.remove(0);
    }
};

// Global instance — defined in main.cpp
extern TeeSerial teeSerial;

// Redirect all Serial.xxx calls that appear after this point to teeSerial.
// Applies to every header included after this one in the same translation unit.
#undef  Serial
#define Serial teeSerial
