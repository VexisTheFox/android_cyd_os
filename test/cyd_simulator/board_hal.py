"""
Board HAL Simulator for CYD Peripherals (PWM Backlight, RGB LED, LDR, Speaker)
"""
from typing import Tuple


class BoardHAL:
    def __init__(self):
        self.initialized = False
        self.backlight_percent = 80
        # Active-low LEDs: 0 is ON, 1 is OFF
        self.led_r = 1  # OFF
        self.led_g = 1  # OFF
        self.led_b = 1  # OFF
        self.ldr_value = 2048  # Nominal mid-scale ADC reading (0..4095)
        self.tone_history = []
        self.backlight_history = []

    def init_board(self):
        self.initialized = True
        self.set_backlight(self.backlight_percent)
        self.set_rgb_led(0, 0, 0)  # LEDs off

    def set_backlight(self, percent: int):
        # Clamped to 0..100
        clamped = max(0, min(100, percent))
        self.backlight_percent = clamped
        self.backlight_history.append(clamped)

    def get_backlight(self) -> int:
        return self.backlight_percent

    def set_rgb_led(self, r: int, g: int, b: int):
        # Active-low: 1 = LED on -> pin LOW (0), 0 = LED off -> pin HIGH (1)
        self.led_r = 0 if r else 1
        self.led_g = 0 if g else 1
        self.led_b = 0 if b else 1

    def get_rgb_led(self) -> Tuple[int, int, int]:
        # Return logical state (1 for on, 0 for off)
        return (1 if self.led_r == 0 else 0,
                1 if self.led_g == 0 else 0,
                1 if self.led_b == 0 else 0)

    def read_ldr(self) -> int:
        return self.ldr_value

    def set_ldr_value(self, val: int):
        self.ldr_value = max(0, min(4095, val))

    def play_tone(self, freq: int, duration_ms: int):
        self.tone_history.append({"freq": freq, "duration": duration_ms})
