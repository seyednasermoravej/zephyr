#ifndef HUMIDIFIER_PIEZO_COMMAND_H_
#define HUMIDIFIER_PIEZO_COMMAND_H_

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

struct PiezoStatusUpdate {
    int32_t piezoNum;
    int32_t brightness;
    int32_t intensity;
    int32_t frequencyHz;
};

/* Decode the small, flat piezo command protocol without accepting truncated
 * numbers, repeated/unknown fields, or bytes after the JSON object. Field names
 * are literal ASCII and all values must be JSON integers. Return the presence
 * mask (piezoNum, brightness, intensity, frequencyHz), or a negative errno.
 * The caller validates each field's application range before touching hardware.
 */
inline int parsePiezoCommand(const char *buf, size_t len, PiezoStatusUpdate &command)
{
    if (buf == nullptr) {
        return -EINVAL;
    }
    size_t cursor = 0;
    const auto skipWhitespace = [&]() {
        while (cursor < len && (buf[cursor] == ' ' || buf[cursor] == '\t' ||
                                buf[cursor] == '\r' || buf[cursor] == '\n')) {
            ++cursor;
        }
    };
    skipWhitespace();
    if (cursor == len || buf[cursor++] != '{') {
        return -EINVAL;
    }

    PiezoStatusUpdate parsed{};
    int fields = 0;
    while (true) {
        skipWhitespace();
        if (cursor == len || buf[cursor++] != '"') {
            return -EINVAL;
        }
        const size_t keyStart = cursor;
        while (cursor < len && buf[cursor] != '"') {
            ++cursor;
        }
        if (cursor == len) {
            return -EINVAL;
        }
        const size_t keyLength = cursor++ - keyStart;
        const char *const names[] = {"piezoNum", "brightness", "intensity", "frequencyHz"};
        int index = -1;
        for (int i = 0; i < 4; ++i) {
            if (keyLength == strlen(names[i]) && memcmp(buf + keyStart, names[i], keyLength) == 0) {
                index = i;
                break;
            }
        }
        if (index < 0 || (fields & (1 << index))) {
            return -EINVAL;
        }
        skipWhitespace();
        if (cursor == len || buf[cursor++] != ':') {
            return -EINVAL;
        }
        skipWhitespace();
        bool negative = cursor < len && buf[cursor] == '-';
        if (negative) {
            ++cursor;
        }
        if (cursor == len || buf[cursor] < '0' || buf[cursor] > '9') {
            return -EINVAL;
        }
        const bool leadingZero = buf[cursor] == '0';
        const size_t numberStart = cursor;
        const uint32_t limit = negative ? uint32_t(INT32_MAX) + 1U : uint32_t(INT32_MAX);
        uint32_t magnitude = 0;
        while (cursor < len && buf[cursor] >= '0' && buf[cursor] <= '9') {
            if (leadingZero && cursor != numberStart) {
                return -EINVAL;
            }
            const uint32_t digit = buf[cursor++] - '0';
            if (magnitude > (limit - digit) / 10U) {
                return -ERANGE;
            }
            magnitude = magnitude * 10U + digit;
        }
        const int32_t value = negative ? static_cast<int32_t>(-static_cast<int64_t>(magnitude))
                                       : static_cast<int32_t>(magnitude);
        switch (index) {
        case 0: parsed.piezoNum = value; break;
        case 1: parsed.brightness = value; break;
        case 2: parsed.intensity = value; break;
        case 3: parsed.frequencyHz = value; break;
        }
        fields |= 1 << index;
        skipWhitespace();
        if (cursor == len) {
            return -EINVAL;
        }
        if (buf[cursor] == ',') {
            ++cursor;
            continue;
        }
        if (buf[cursor++] != '}') {
            return -EINVAL;
        }
        skipWhitespace();
        if (cursor != len || !(fields & 1)) {
            return -EINVAL;
        }
        command = parsed;
        return fields;
    }
}

#endif // HUMIDIFIER_PIEZO_COMMAND_H_
