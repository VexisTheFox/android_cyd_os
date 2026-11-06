# Xiaomi SU7 Ultra 3D Real-Time Telemetry - ESP32 Cheap Yellow Display (CYD)

A production-ready PlatformIO project rendering a real-time 50-60 FPS 3D wireframe animation of the **Xiaomi SU7 Ultra** with live telemetry overlay for the **ESP32 Cheap Yellow Display (CYD)**, specifically configured for the **2 USB Type-C hardware variant** (`ESP32-2432S028`, ST7789 controller, 320x240 landscape).

Reference repository: [witnessmenow/ESP32-Cheap-Yellow-Display](https://github.com/witnessmenow/ESP32-Cheap-Yellow-Display)

---

## Hardware Details & 2 USB Type-C Variant Specifications

The 2 USB Type-C CYD variant features both a Micro-USB and a USB-C port and uses the **ST7789** display controller instead of the ILI9341 chip found on early single Micro-USB models.

| Component | Specification / Pin Assignment |
| :--- | :--- |
| **MCU** | ESP32-WROOM-32 (240MHz, 320KB SRAM, 4MB Flash) |
| **Display Panel** | 2.8" TFT LCD, 320 x 240 Resolution |
| **Display Controller** | ST7789 (or ILI9341 with inversion) |
| **Color Order** | BGR (`TFT_RGB_ORDER TFT_BGR`) |
| **Inversion** | Inversion OFF for ST7789 (`TFT_INVERSION_OFF`) |
| **Backlight Pin** | GPIO 21 (`TFT_BL`), Active HIGH |
| **SPI Interface (Display)** | HSPI |
| **TFT MOSI** | GPIO 13 |
| **TFT MISO** | GPIO 12 |
| **TFT SCLK** | GPIO 14 |
| **TFT CS** | GPIO 15 |
| **TFT DC** | GPIO 2 |
| **TFT RST** | -1 (Tied to EN/MCU Reset) |
| **Touch Controller** | XPT2046 Resistive Touch (Separate SPI / VSPI) |
| **Touch Pins** | CLK: 25, MOSI: 32, MISO: 39, CS: 33, IRQ: 36 |
| **Onboard RGB LED** | Red: GPIO 4, Green: GPIO 16, Blue: GPIO 17 (Active LOW) |
| **LDR (Light Sensor)** | GPIO 34 |
| **Audio Amp / Speaker** | GPIO 26 |

> **Important Hardware Note on the USB-C Port:**
> The USB-C port on the 2 USB variant lacks CC (Configuration Channel) resistors. Standard USB-C to USB-C cables connected to modern USB-C ports on laptops/chargers will not negotiate power or data. Always use a **USB-A to USB-C cable** (or a USB-C to USB-A adapter), or the onboard Micro-USB port for power and programming.

---

## Project Structure

```
CYD/
├── platformio.ini              # Multi-environment PlatformIO build configuration
├── src/
│   └── main.cpp               # Hello World display initialization (Black text on White)
├── DisplayConfig/
│   ├── User_Setup.h           # Standard 1-USB CYD setup for Arduino IDE
│   └── CYD2USB/
│       └── User_Setup.h       # 2 USB Type-C CYD setup for Arduino IDE
├── .gitignore
└── README.md
```

---

## Build Environments (`platformio.ini`)

This project provides pre-configured environments targeting the specific CYD hardware variations:

* **`cyd2usb` (Default):** Targets the 2 USB Type-C variant using the native `ST7789_DRIVER` with `TFT_RGB_ORDER=TFT_BGR` and `TFT_INVERSION_OFF=1`, strictly matching `DisplayConfig/CYD2USB/User_Setup.h` from the reference repository.
* **`cyd2usb_ili9341`:** Alternative configuration for 2 USB boards using the `ILI9341_2_DRIVER` with `-DTFT_INVERSION_ON=1` (as used in companion projects such as *Spotify-Diy-Thing*).
* **`cyd`:** For standard original 1 USB (Micro-USB) CYD boards using `ILI9341_2_DRIVER` with `-DTFT_INVERSION_OFF=1`.

All TFT driver settings and pin configurations are injected via PlatformIO `build_flags` with `-DUSER_SETUP_LOADED=1`, eliminating the need to modify global library files.

---

## How to Build and Upload

### 1. Build the Project
To compile the default `cyd2usb` environment:
```bash
pio run
```

To compile a specific environment:
```bash
pio run -e cyd2usb
pio run -e cyd2usb_ili9341
pio run -e cyd
```

### 2. Upload to Device
Connect the CYD board to your computer using a USB-A to USB-C cable (or Micro-USB cable) and run:
```bash
pio run --target upload
```

### 3. Monitor Serial Output
To monitor serial logs at 115200 baud:
```bash
pio device monitor -b 115200
```

---

## Display Output & 3D Telemetry Behavior

When powered on:
1. The TFT controller initializes and configures landscape orientation (`rotation 1`, 320x240) with SPI clock at 55MHz.
2. Backlight control (GPIO 21) is driven HIGH for instant, flicker-free illumination.
3. Static racing cockpit HUD is drawn:
   - Header: **XIAOMI SU7 ULTRA** with triple-motor power rating (`1548 PS | 3-MOTOR`).
   - Footer: Performance statistics (`0-100: 1.98s`, `TOP: 350 km/h`) and dynamic telemetry panel.
4. Real-time 3D rotation engine renders the Xiaomi SU7 Ultra wireframe at **50-60 FPS**:
   - 3D perspective projection with turntable ground ring and orientation axis.
   - Dual-buffer RAM sprites (`sprite` for 3D viewport, `hudSprite` for telemetry readout) deliver 100% flicker-free rendering.
   - Live telemetry updates continuously with the vehicle yaw angle (0° - 359°) and live FPS readout.
