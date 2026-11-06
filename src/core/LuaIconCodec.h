/**
 * @file LuaIconCodec.h
 * @brief Lightweight Base64 Logo Decoder & Lua Script Metadata Parser (Zero-Heap, Low-Stack)
 *
 * Supports declaring custom app metadata in the top header of any .lua script:
 *   -- @name: Weather Live
 *   -- @bg: 0x0359
 *   -- @icon_b64: <base64 payload>
 * or via Lua globals:
 *   _APP_NAME = "Weather Live"
 *   _APP_ICON_B64 = "<base64 payload>"
 */

#pragma once

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <LittleFS.h>
#include <SD.h>
#include <cstring>
#include <cstdlib>

namespace core {

class LuaIconCodec {
public:
    static int8_t b64Val(char c) {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    }

    /**
     * @brief Decode a Base64 string into outBuf (stack-safe, zero heap allocation).
     */
    static size_t decodeBase64(const char* b64, uint8_t* outBuf, size_t maxOut) {
        if (!b64 || !outBuf || maxOut == 0) return 0;
        size_t outLen = 0;
        uint32_t accum = 0;
        int bits = 0;

        for (const char* p = b64; *p != '\0'; ++p) {
            if (*p == '=' || *p == '\r' || *p == '\n' || *p == '"' || *p == '\'') {
                if (*p == '=' || *p == '"' || *p == '\'') break;
                continue;
            }
            int8_t v = b64Val(*p);
            if (v < 0) continue;
            accum = (accum << 6) | (uint32_t)v;
            bits += 6;
            if (bits >= 8) {
                bits -= 8;
                if (outLen < maxOut) {
                    outBuf[outLen++] = (uint8_t)((accum >> bits) & 0xFF);
                } else {
                    break;
                }
            }
        }
        return outLen;
    }

    /**
     * @brief Convert 8-bit RGB332 byte into 16-bit RGB565.
     */
    static uint16_t rgb332To565(uint8_t c) {
        uint16_t r = ((c >> 5) & 0x07) * 31 / 7;
        uint16_t g = ((c >> 2) & 0x07) * 63 / 7;
        uint16_t b = (c & 0x03) * 31 / 3;
        return (r << 11) | (g << 5) | b;
    }

    /**
     * @brief Render a Base64-encoded icon centered at (cx, cy).
     */
    static bool drawBase64Icon(TFT_eSPI &canvas, const char* b64, int16_t cx, int16_t cy,
                               uint16_t fgColor = TFT_WHITE, uint8_t scale = 2) {
        if (!b64 || b64[0] == '\0') return false;
        if (scale == 0) scale = 1;

        uint8_t raw[260];
        size_t n = decodeBase64(b64, raw, sizeof(raw));
        if (n < 32) return false;

        int16_t w = 16 * scale;
        int16_t h = 16 * scale;
        int16_t x0 = cx - (w / 2);
        int16_t y0 = cy - (h / 2);

        // Format A: 32 bytes (16x16 1-bit mask) or 34 bytes (2-byte RGB565 + 32-byte 1-bit mask)
        if (n == 32 || n == 34) {
            const uint8_t* mask = raw;
            uint16_t drawCol = fgColor;
            if (n == 34) {
                drawCol = ((uint16_t)raw[0] << 8) | raw[1];
                mask = raw + 2;
            }
            for (int r = 0; r < 16; r++) {
                uint16_t rowBits = ((uint16_t)mask[r * 2] << 8) | mask[r * 2 + 1];
                for (int c = 0; c < 16; c++) {
                    if (rowBits & (0x8000 >> c)) {
                        if (scale == 1) canvas.drawPixel(x0 + c, y0 + r, drawCol);
                        else canvas.fillRect(x0 + c * scale, y0 + r * scale, scale, scale, drawCol);
                    }
                }
            }
            return true;
        }

        // Format B: 64 bytes (16x16 2-bit 4-color) or 68 bytes (4-byte palette + 64-byte 2-bit)
        if (n == 64 || n == 68) {
            uint16_t pal[4] = { 0x0000, TFT_WHITE, 0x07FF, 0xFFE0 };
            const uint8_t* data = raw;
            if (n == 68) {
                pal[2] = ((uint16_t)raw[0] << 8) | raw[1];
                pal[3] = ((uint16_t)raw[2] << 8) | raw[3];
                data = raw + 4;
            }
            for (int r = 0; r < 16; r++) {
                for (int c = 0; c < 16; c++) {
                    int pxIdx = r * 16 + c;
                    uint8_t byteVal = data[pxIdx >> 2];
                    uint8_t shift = (3 - (pxIdx & 3)) * 2;
                    uint8_t idx = (byteVal >> shift) & 0x03;
                    if (idx != 0) {
                        uint16_t col = pal[idx];
                        if (scale == 1) canvas.drawPixel(x0 + c, y0 + r, col);
                        else canvas.fillRect(x0 + c * scale, y0 + r * scale, scale, scale, col);
                    }
                }
            }
            return true;
        }

        // Format C: 256 bytes (16x16 RGB332)
        if (n >= 256) {
            for (int r = 0; r < 16; r++) {
                for (int c = 0; c < 16; c++) {
                    uint8_t v = raw[r * 16 + c];
                    if (v != 0) {
                        uint16_t col = rgb332To565(v);
                        if (scale == 1) canvas.drawPixel(x0 + c, y0 + r, col);
                        else canvas.fillRect(x0 + c * scale, y0 + r * scale, scale, scale, col);
                    }
                }
            }
            return true;
        }

        return false;
    }

    /**
     * @brief Parse header metadata (-- @name, -- @bg, -- @icon_b64) from the first 480 bytes of a Lua file.
     * Uses a compact 480B buffer so it never stresses the FreeRTOS task stack.
     */
    static bool parseScriptHeader(const char* path,
                                  char* outName, size_t maxName,
                                  uint16_t* outBg,
                                  char* outIconB64, size_t maxIconB64) {
        if (!path || path[0] == '\0') return false;

        File f;
        if (strncmp(path, "/sd/", 4) == 0) {
            if (!SD.exists(path + 3)) return false;
            f = SD.open(path + 3, FILE_READ);
        } else {
            if (!LittleFS.exists(path)) return false;
            f = LittleFS.open(path, "r");
        }
        if (!f) return false;

        char buf[480];
        size_t nRead = f.readBytes(buf, sizeof(buf) - 1);
        f.close();
        buf[nRead] = '\0';

        bool foundAny = false;

        // 1. Parse -- @name: or -- @title: or _APP_NAME =
        if (outName && maxName > 1) {
            const char* p = strstr(buf, "@name:");
            if (!p) p = strstr(buf, "@title:");
            if (p) {
                p = strchr(p, ':') + 1;
                while (*p == ' ' || *p == '\t' || *p == '"') p++;
                size_t i = 0;
                while (*p && *p != '\r' && *p != '\n' && *p != '"' && i + 1 < maxName) {
                    outName[i++] = *p++;
                }
                while (i > 0 && (outName[i - 1] == ' ' || outName[i - 1] == '\t')) i--;
                outName[i] = '\0';
                if (i > 0) foundAny = true;
            } else {
                p = strstr(buf, "_APP_NAME");
                if (p) {
                    p = strchr(p, '=');
                    if (p) {
                        p++;
                        while (*p == ' ' || *p == '\t') p++;
                        if (*p == '"' || *p == '\'') {
                            char q = *p++;
                            size_t i = 0;
                            while (*p && *p != q && *p != '\r' && *p != '\n' && i + 1 < maxName) {
                                outName[i++] = *p++;
                            }
                            outName[i] = '\0';
                            if (i > 0) foundAny = true;
                        }
                    }
                }
            }
        }

        // 2. Parse -- @bg: 0xXXXX
        if (outBg) {
            const char* p = strstr(buf, "@bg:");
            if (p) {
                p += 4;
                while (*p == ' ' || *p == '\t') p++;
                unsigned long val = strtoul(p, nullptr, 0);
                if (val > 0 && val <= 0xFFFF) {
                    *outBg = (uint16_t)val;
                    foundAny = true;
                }
            }
        }

        // 3. Parse -- @icon_b64: or _APP_ICON_B64 =
        if (outIconB64 && maxIconB64 > 1) {
            const char* p = strstr(buf, "@icon_b64:");
            if (p) {
                p += 10;
                while (*p == ' ' || *p == '\t' || *p == '"') p++;
                size_t i = 0;
                while (*p && *p != '\r' && *p != '\n' && *p != '"' && *p != ' ' && i + 1 < maxIconB64) {
                    outIconB64[i++] = *p++;
                }
                outIconB64[i] = '\0';
                if (i >= 40) foundAny = true;
            } else {
                p = strstr(buf, "_APP_ICON_B64");
                if (p) {
                    p = strchr(p, '=');
                    if (p) {
                        p++;
                        while (*p == ' ' || *p == '\t') p++;
                        if (*p == '"' || *p == '\'') {
                            char q = *p++;
                            size_t i = 0;
                            while (*p && *p != q && *p != '\r' && *p != '\n' && i + 1 < maxIconB64) {
                                outIconB64[i++] = *p++;
                            }
                            outIconB64[i] = '\0';
                            if (i >= 40) foundAny = true;
                        }
                    }
                }
            }
        }

        return foundAny;
    }

    /**
     * @brief Read and render a custom Base64 icon directly from a .lua script file at (cx, cy).
     */
    static bool drawCustomScriptIcon(TFT_eSPI &canvas, const char* path, int16_t cx, int16_t cy,
                                     uint16_t fgColor = TFT_WHITE, uint8_t scale = 2) {
        char b64[360] = {0};
        if (!parseScriptHeader(path, nullptr, 0, nullptr, b64, sizeof(b64))) {
            return false;
        }
        if (b64[0] == '\0') return false;
        return drawBase64Icon(canvas, b64, cx, cy, fgColor, scale);
    }
};

} // namespace core
