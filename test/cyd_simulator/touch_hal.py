"""
Touch HAL Simulator for XPT2046 Resistive Touchscreen on dedicated SPI (VSPI @ 2.5MHz)
"""
from typing import Tuple

DEFAULT_X_MIN = 200
DEFAULT_X_MAX = 3800
DEFAULT_Y_MIN = 200
DEFAULT_Y_MAX = 3800

SCREEN_WIDTH = 320
SCREEN_HEIGHT = 240


class TouchHAL:
    def __init__(self):
        self.initialized = False
        self.x_min = DEFAULT_X_MIN
        self.x_max = DEFAULT_X_MAX
        self.y_min = DEFAULT_Y_MIN
        self.y_max = DEFAULT_Y_MAX
        self.pressed = False
        self.current_x = 0
        self.current_y = 0
        self.raw_z = 0
        self.touch_events = []

    def init_touch(self):
        self.initialized = True

    def set_calibration(self, x_min: int, x_max: int, y_min: int, y_max: int):
        self.x_min = x_min
        self.x_max = x_max
        self.y_min = y_min
        self.y_max = y_max

    def raw_to_screen(self, raw_x: int, raw_y: int) -> Tuple[int, int]:
        # Clamping raw
        clamped_x = max(self.x_min, min(raw_x, self.x_max))
        clamped_y = max(self.y_min, min(raw_y, self.y_max))
        # Map to 0..320, 0..240
        x = int((clamped_x - self.x_min) * (SCREEN_WIDTH - 1) / (self.x_max - self.x_min))
        y = int((clamped_y - self.y_min) * (SCREEN_HEIGHT - 1) / (self.y_max - self.y_min))
        return (x, y)

    def screen_to_raw(self, x: int, y: int) -> Tuple[int, int]:
        clamped_x = max(0, min(x, SCREEN_WIDTH - 1))
        clamped_y = max(0, min(y, SCREEN_HEIGHT - 1))
        raw_x = int(self.x_min + clamped_x * (self.x_max - self.x_min) / (SCREEN_WIDTH - 1))
        raw_y = int(self.y_min + clamped_y * (self.y_max - self.y_min) / (SCREEN_HEIGHT - 1))
        return (raw_x, raw_y)

    def simulate_touch(self, x: int, y: int, pressed: bool = True):
        # Clamping coordinates to display bounds
        clamped_x = max(0, min(x, SCREEN_WIDTH - 1))
        clamped_y = max(0, min(y, SCREEN_HEIGHT - 1))
        self.pressed = pressed
        self.current_x = clamped_x
        self.current_y = clamped_y
        self.raw_z = 1000 if pressed else 0
        self.touch_events.append({
            "type": "press" if pressed else "release",
            "x": clamped_x,
            "y": clamped_y,
            "raw_input": (x, y)
        })

    def simulate_raw_touch(self, raw_x: int, raw_y: int, z_pressure: int = 1000):
        if z_pressure > 400:
            x, y = self.raw_to_screen(raw_x, raw_y)
            self.simulate_touch(x, y, pressed=True)
        else:
            self.simulate_touch(self.current_x, self.current_y, pressed=False)

    def get_touch(self) -> Tuple[bool, int, int]:
        if not self.initialized:
            return (False, 0, 0)
        return (self.pressed, self.current_x, self.current_y)
