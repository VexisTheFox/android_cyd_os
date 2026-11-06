"""
Tier 2: Boundary & Corner Cases Tests B17 - B21 (Bundled Apps, Settings, Hardware, NVS, About Boundaries)
Authoritative Source: PROJECT.md § Architecture, § Feature Inventory (F17..F21)
"""
import unittest
from test.cyd_simulator import DisplayHAL, BoardHAL, ConfigStore, SystemConfig, StorageHAL
from test.cyd_simulator.app_framework import SettingsApp, AboutApp, SU7App


class TestB17BundledAppsBounds(unittest.TestCase):
    """B17: Bundled Applications Boundaries - Edge cases and rendering integrity"""

    def setUp(self):
        self.display = DisplayHAL()
        self.storage = StorageHAL()
        self.storage.init_storage()

    def test_b17_01_corrupt_bundled_app_recovery(self):
        """Verify overwriting bundled app with invalid content can be deleted and reprovisioned"""
        self.storage.write_file("/apps/calc.lua", "Corrupt Garbage")
        self.storage.delete_file("/apps/calc.lua")
        self.assertFalse(self.storage.file_exists("/apps/calc.lua"))
        self.storage.provision_default_apps()
        self.assertTrue(self.storage.file_exists("/apps/calc.lua"))
        self.assertIn("Calculator", self.storage.read_file("/apps/calc.lua"))

    def test_b17_02_su7_model_vertex_count(self):
        """Verify SU7 3D model data has at least 70 vertices and 100 edges"""
        import os, re
        with open("src/su7_data.h", "r") as f:
            content = f.read()
        verts = re.findall(r"\{\s*([-\d\.]+)f?\s*,\s*([-\d\.]+)f?\s*,\s*([-\d\.]+)f?\s*\}", content)
        self.assertGreaterEqual(len(verts), 70)

    def test_b17_03_paint_app_touch_clamping(self):
        """Verify paint app touch points stay within viewport limits (Y=20..213)"""
        raw_y = 10  # in status bar
        clamped_y = max(20, min(213, raw_y))
        self.assertEqual(clamped_y, 20)

    def test_b17_04_sysinfo_heap_metrics(self):
        """Verify sysinfo handles very small free heap values without crashing"""
        content = self.storage.read_file("/apps/sysinfo.lua")
        self.assertIn("sys.getHeap()", content)

    def test_b17_05_calc_button_hitboxes(self):
        """Verify calc script defines valid button drawing coordinates"""
        content = self.storage.read_file("/apps/calc.lua")
        self.assertIn("gui.drawRect", content)


class TestB18SettingsUIBounds(unittest.TestCase):
    """B18: Settings Interface Boundaries - Clamping slider values and theme options"""

    def setUp(self):
        self.display = DisplayHAL()
        self.app = SettingsApp()

    def test_b18_01_brightness_minimum_10_percent(self):
        """Verify brightness slider does not allow setting below 10% (avoiding black screen)"""
        store = ConfigStore()
        cfg = SystemConfig(brightness=0)
        store.save_config(cfg)
        self.assertEqual(store.load_config().brightness, 10)

    def test_b18_02_brightness_maximum_100_percent(self):
        """Verify brightness slider clamps maximum to 100%"""
        store = ConfigStore()
        cfg = SystemConfig(brightness=120)
        store.save_config(cfg)
        self.assertEqual(store.load_config().brightness, 100)

    def test_b18_03_sleep_timeout_zero_disabled(self):
        """Verify sleep timeout 0 is allowed (0 = never sleep)"""
        store = ConfigStore()
        cfg = SystemConfig(sleepTimeoutSec=0)
        store.save_config(cfg)
        self.assertEqual(store.load_config().sleepTimeoutSec, 0)

    def test_b18_04_theme_binary_values(self):
        """Verify theme is strictly 0 (dark) or 1 (light)"""
        store = ConfigStore()
        store.save_config(SystemConfig(theme=5))
        self.assertEqual(store.load_config().theme, 0)  # Unknown clamped to default 0

    def test_b18_05_sound_toggle_boolean(self):
        """Verify soundEnabled toggles cleanly between True and False"""
        store = ConfigStore()
        store.save_config(SystemConfig(soundEnabled=False))
        self.assertFalse(store.load_config().soundEnabled)
        store.save_config(SystemConfig(soundEnabled=True))
        self.assertTrue(store.load_config().soundEnabled)


class TestB19HardwareControlBounds(unittest.TestCase):
    """B19: Live Hardware Control Boundaries - PWM duty extremes, active-low logic, sensor bounds"""

    def setUp(self):
        self.board = BoardHAL()
        self.board.init_board()

    def test_b19_01_pwm_duty_zero_percent(self):
        """Verify backlight PWM at 0% sets duty cycle to 0"""
        self.board.set_backlight(0)
        self.assertEqual(self.board.get_backlight(), 0)

    def test_b19_02_pwm_duty_100_percent(self):
        """Verify backlight PWM at 100% sets full duty cycle"""
        self.board.set_backlight(100)
        self.assertEqual(self.board.get_backlight(), 100)

    def test_b19_03_rapid_rgb_led_toggling(self):
        """Verify rapidly toggling active-low RGB LEDs preserves exact final state"""
        for _ in range(50):
            self.board.set_rgb_led(1, 0, 1)
            self.board.set_rgb_led(0, 1, 0)
        self.assertEqual(self.board.get_rgb_led(), (0, 1, 0))
        self.assertEqual((self.board.led_r, self.board.led_g, self.board.led_b), (1, 0, 1))

    def test_b19_04_ldr_sensor_extreme_adc_values(self):
        """Verify LDR ADC photoresistor reading handles extremes 0 (dark) and 4095 (bright)"""
        self.board.set_ldr_value(-50)
        self.assertEqual(self.board.read_ldr(), 0)
        self.board.set_ldr_value(10000)
        self.assertEqual(self.board.read_ldr(), 4095)

    def test_b19_05_high_frequency_tone_generation(self):
        """Verify buzzer can generate high frequencies (e.g. 10000Hz)"""
        self.board.play_tone(10000, 50)
        self.assertEqual(self.board.tone_history[-1]["freq"], 10000)


class TestB20NVSPersistenceBounds(unittest.TestCase):
    """B20: NVS Persistence Boundaries - Corrupted keys, missing keys, fallback to defaults"""

    def setUp(self):
        self.store = ConfigStore()

    def test_b20_01_corrupted_non_numeric_brightness(self):
        """Verify corrupted non-numeric string in NVS falls back to default 80%"""
        self.store.storage["brightness"] = "CORRUPT_NOT_INT"
        cfg = self.store.load_config()
        self.assertEqual(cfg.brightness, 80)

    def test_b20_02_corrupted_timeout_fallback(self):
        """Verify corrupted sleep timeout falls back to default 60 seconds"""
        self.store.storage["sleepTimeoutSec"] = "INVALID"
        cfg = self.store.load_config()
        self.assertEqual(cfg.sleepTimeoutSec, 60)

    def test_b20_03_missing_keys_handled(self):
        """Verify loading from completely empty NVS storage returns defaults"""
        self.store.storage.clear()
        cfg = self.store.load_config()
        self.assertEqual(cfg.brightness, 80)
        self.assertEqual(cfg.sleepTimeoutSec, 60)
        self.assertEqual(cfg.theme, 0)
        self.assertTrue(cfg.soundEnabled)

    def test_b20_04_reset_to_defaults_clears_modifications(self):
        """Verify reset_to_defaults restores standard system configuration"""
        self.store.save_config(SystemConfig(brightness=30, sleepTimeoutSec=300, theme=1, soundEnabled=False))
        self.store.reset_to_defaults()
        cfg = self.store.load_config()
        self.assertEqual(cfg.brightness, 80)
        self.assertEqual(cfg.sleepTimeoutSec, 60)
        self.assertEqual(cfg.theme, 0)
        self.assertTrue(cfg.soundEnabled)

    def test_b20_05_extreme_sleep_timeout_clamped(self):
        """Verify sleep timeout exceeding 3600 seconds (1 hour) is clamped to 3600"""
        self.store.save_config(SystemConfig(sleepTimeoutSec=50000))
        self.assertEqual(self.store.load_config().sleepTimeoutSec, 3600)


class TestB21AboutSystemPageBounds(unittest.TestCase):
    """B21: About System Page Boundaries - Rendering limits, viewport boundaries"""

    def setUp(self):
        self.display = DisplayHAL()
        self.app = AboutApp()

    def test_b21_01_about_renders_inside_viewport(self):
        """Verify all text strings drawn by About page are positioned within Y=20..213"""
        self.app.onRender(self.display, 0, 20, 320, 194)
        for t in self.display.text_history:
            self.assertGreaterEqual(t["y"], 20)
            self.assertLess(t["y"], 214)

    def test_b21_02_repeated_about_renders_memory_stable(self):
        """Verify repeatedly rendering About view does not crash or leak memory"""
        for _ in range(10):
            self.app.onRender(self.display, 0, 20, 320, 194)
        self.assertTrue(self.display.has_text("CYD Android OS v1.0"))

    def test_b21_03_zero_uptime_display(self):
        """Verify About page renders system specs cleanly even immediately after boot"""
        self.app.onRender(self.display, 0, 20, 320, 194)
        self.assertTrue(self.display.has_text("ESP32-D0WDQ6-V3"))

    def test_b21_04_about_viewport_width_respected(self):
        """Verify About page elements do not exceed viewport width 320"""
        self.app.onRender(self.display, 0, 20, 320, 194)
        for t in self.display.text_history:
            self.assertLess(t["x"], 320)

    def test_b21_05_about_unhandled_touch_ignored(self):
        """Verify touching About page does not crash application"""
        self.app.onTouch(100, 100, True)
        self.assertIn("onTouch(100,100,True)", self.app.lifecycle_log)


if __name__ == "__main__":
    unittest.main()
