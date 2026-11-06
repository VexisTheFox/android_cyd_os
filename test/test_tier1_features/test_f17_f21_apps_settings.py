"""
Tier 1: Feature Tests F17 - F21 (Bundled Apps, Settings UI, Live Hardware Control, NVS, About Page)
Authoritative Source: PROJECT.md § Architecture, § Feature Inventory (F17..F21)
"""
import unittest
from test.cyd_simulator import DisplayHAL, BoardHAL, ConfigStore, SystemConfig, StorageHAL
from test.cyd_simulator.app_framework import SettingsApp, AboutApp, SU7App, DynamicScriptApp


class TestF17BundledApplications(unittest.TestCase):
    """F17: Bundled Applications - Pre-installed apps: Calculator, Paint, SU7 3D Demo, SysInfo"""

    def setUp(self):
        self.storage = StorageHAL()
        self.storage.init_storage()
        self.display = DisplayHAL()

    def test_f17_01_bundled_calc_present(self):
        """Verify Calculator script exists in default storage"""
        self.assertTrue(self.storage.file_exists("/apps/calc.lua"))
        content = self.storage.read_file("/apps/calc.lua")
        self.assertIn("Calculator", content)

    def test_f17_02_bundled_paint_present(self):
        """Verify Paint script exists in default storage"""
        self.assertTrue(self.storage.file_exists("/apps/paint.lua"))
        content = self.storage.read_file("/apps/paint.lua")
        self.assertIn("Paint", content)

    def test_f17_03_bundled_sysinfo_present(self):
        """Verify SysInfo script exists in default storage"""
        self.assertTrue(self.storage.file_exists("/apps/sysinfo.lua"))
        content = self.storage.read_file("/apps/sysinfo.lua")
        self.assertIn("Telemetry", content)

    def test_f17_04_bundled_su7_native_app_renders(self):
        """Verify native SU7 3D App renders title to viewport"""
        app = SU7App()
        app.onRender(self.display, 0, 20, 320, 194)
        self.assertTrue(self.display.has_text("Xiaomi SU7 Ultra 3D"))

    def test_f17_05_bundled_apps_icons(self):
        """Verify distinct icons for bundled native applications"""
        settings = SettingsApp()
        about = AboutApp()
        su7 = SU7App()
        self.assertEqual(settings.getIcon(), "ICON_SETTINGS")
        self.assertEqual(about.getIcon(), "ICON_ABOUT")
        self.assertEqual(su7.getIcon(), "ICON_SU7")


class TestF18SettingsInterface(unittest.TestCase):
    """F18: Settings Interface - Settings UI: Brightness slider, Sleep timeout, Theme, Sound"""

    def setUp(self):
        self.display = DisplayHAL()
        self.app = SettingsApp()

    def test_f18_01_settings_app_name(self):
        """Verify Settings application identity"""
        self.assertEqual(self.app.getName(), "Settings")

    def test_f18_02_settings_renders_header(self):
        """Verify Settings view renders title"""
        self.app.onRender(self.display, 0, 20, 320, 194)
        self.assertTrue(self.display.has_text("Device Settings"))

    def test_f18_03_settings_renders_brightness_label(self):
        """Verify Settings renders brightness section"""
        self.app.onRender(self.display, 0, 20, 320, 194)
        self.assertTrue(self.display.has_text("Brightness:"))

    def test_f18_04_settings_background_color(self):
        """Verify Settings viewport background color is dark tech navy (0x10A2)"""
        self.app.onRender(self.display, 0, 20, 320, 194)
        fill_op = next((op for op in self.display.draw_history if op.get("op") == "fillRect" and op.get("color") == 0x10A2), None)
        self.assertIsNotNone(fill_op)

    def test_f18_05_settings_touch_interaction(self):
        """Verify Settings touch callback logs input"""
        self.app.onTouch(100, 80, True)
        self.assertIn("onTouch(100,80,True)", self.app.lifecycle_log)


class TestF19LiveHardwareControl(unittest.TestCase):
    """F19: Live Hardware Control - Real-time PWM backlight adjustment (GPIO 21) & RGB LED control"""

    def setUp(self):
        self.board = BoardHAL()
        self.board.init_board()

    def test_f19_01_pwm_backlight_default(self):
        """Verify initial backlight defaults to 80%"""
        self.assertEqual(self.board.get_backlight(), 80)

    def test_f19_02_pwm_backlight_adjustment(self):
        """Verify setting backlight adjusts PWM duty cycle"""
        self.board.set_backlight(95)
        self.assertEqual(self.board.get_backlight(), 95)
        self.assertIn(95, self.board.backlight_history)

    def test_f19_03_active_low_rgb_led_control(self):
        """Verify active-low RGB LED control logic"""
        # Turn Red ON, Green OFF, Blue ON
        self.board.set_rgb_led(1, 0, 1)
        self.assertEqual(self.board.get_rgb_led(), (1, 0, 1))
        # Active low: pin state for Red is 0 (LOW), Green is 1 (HIGH), Blue is 0 (LOW)
        self.assertEqual(self.board.led_r, 0)
        self.assertEqual(self.board.led_g, 1)
        self.assertEqual(self.board.led_b, 0)

    def test_f19_04_read_ldr_sensor(self):
        """Verify reading analog photoresistor value"""
        self.board.set_ldr_value(3200)
        self.assertEqual(self.board.read_ldr(), 3200)

    def test_f19_05_piezo_buzzer_tone(self):
        """Verify buzzer plays tone with given frequency and duration"""
        self.board.play_tone(440, 200)  # A4 440Hz for 200ms
        self.assertEqual(len(self.board.tone_history), 1)
        self.assertEqual(self.board.tone_history[0]["freq"], 440)
        self.assertEqual(self.board.tone_history[0]["duration"], 200)


class TestF20NVSPersistence(unittest.TestCase):
    """F20: NVS Settings Persistence - Persist device configuration in NVS Preferences ("cyd_os")"""

    def setUp(self):
        self.store = ConfigStore()

    def test_f20_01_default_config_values(self):
        """Verify default configuration values"""
        cfg = self.store.load_config()
        self.assertEqual(cfg.brightness, 80)
        self.assertEqual(cfg.sleepTimeoutSec, 60)
        self.assertEqual(cfg.theme, 0)
        self.assertTrue(cfg.soundEnabled)

    def test_f20_02_save_and_reload_config(self):
        """Verify saved configuration persists across reloads"""
        new_cfg = SystemConfig(brightness=45, sleepTimeoutSec=120, theme=1, soundEnabled=False)
        self.store.save_config(new_cfg)

        loaded = self.store.load_config()
        self.assertEqual(loaded.brightness, 45)
        self.assertEqual(loaded.sleepTimeoutSec, 120)
        self.assertEqual(loaded.theme, 1)
        self.assertFalse(loaded.soundEnabled)

    def test_f20_03_namespace_is_cyd_os(self):
        """Verify NVS Preferences namespace is 'cyd_os'"""
        self.assertEqual(self.store.namespace, "cyd_os")

    def test_f20_04_brightness_clamping_on_save(self):
        """Verify brightness is clamped to range 10..100% on save"""
        cfg = SystemConfig(brightness=150)
        self.store.save_config(cfg)
        self.assertEqual(self.store.load_config().brightness, 100)

        cfg_low = SystemConfig(brightness=2)
        self.store.save_config(cfg_low)
        self.assertEqual(self.store.load_config().brightness, 10)

    def test_f20_05_sleep_timeout_bounds(self):
        """Verify sleep timeout is clamped within 0..3600 seconds"""
        cfg = SystemConfig(sleepTimeoutSec=9999)
        self.store.save_config(cfg)
        self.assertEqual(self.store.load_config().sleepTimeoutSec, 3600)


class TestF21AboutSystemPage(unittest.TestCase):
    """F21: About System Page - Display OS version, chip revision, CPU freq, flash, free heap"""

    def setUp(self):
        self.display = DisplayHAL()
        self.app = AboutApp()

    def test_f21_01_about_app_name(self):
        """Verify About application name"""
        self.assertEqual(self.app.getName(), "About")

    def test_f21_02_about_renders_os_version(self):
        """Verify About page renders OS version"""
        self.app.onRender(self.display, 0, 20, 320, 194)
        self.assertTrue(self.display.has_text("CYD Android OS v1.0"))

    def test_f21_03_about_renders_cpu_specs(self):
        """Verify About page renders ESP32 chip specs and clock (240MHz)"""
        self.app.onRender(self.display, 0, 20, 320, 194)
        self.assertTrue(self.display.has_text("ESP32-D0WDQ6-V3 240MHz"))

    def test_f21_04_about_renders_memory_specs(self):
        """Verify About page renders 4MB Flash and 320KB RAM specs"""
        self.app.onRender(self.display, 0, 20, 320, 194)
        self.assertTrue(self.display.has_text("Flash: 4MB | RAM: 320KB"))

    def test_f21_05_about_background_fill(self):
        """Verify About page fills viewport with dark slate background (0x0821)"""
        self.app.onRender(self.display, 0, 20, 320, 194)
        fill_op = next((op for op in self.display.draw_history if op.get("op") == "fillRect" and op.get("color") == 0x0821), None)
        self.assertIsNotNone(fill_op)


if __name__ == "__main__":
    unittest.main()
