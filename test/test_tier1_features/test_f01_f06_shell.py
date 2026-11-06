"""
Tier 1: Feature Tests F01 - F06 (UI Shell & Boot)
Authoritative Source: PROJECT.md § Architecture, § Feature Inventory (F01..F06)
"""
import unittest
from test.cyd_simulator import DisplayHAL, TouchHAL, AppRegistry, UIManager, BootView, StatusBar, NavBar, LauncherView


class TestF01BootSplash(unittest.TestCase):
    """F01: Boot Splash Screen - Graphical boot logo displayed on power-on/reset"""

    def setUp(self):
        self.display = DisplayHAL()
        self.boot_view = BootView(self.display)

    def test_f01_01_splash_renders_on_screen(self):
        """Verify boot splash renders Android OS text and branding"""
        self.boot_view.render()
        self.assertTrue(self.display.has_text("ANDROID OS"))
        self.assertTrue(self.display.has_text("ESP32 CYD Edition"))

    def test_f01_02_splash_clears_screen_to_black(self):
        """Verify boot splash clears previous buffer state to black (0x0000)"""
        self.display.fill_screen(0xFFFF)  # Pre-fill with white
        self.boot_view.render()
        self.assertEqual(self.display.get_pixel(0, 0), 0x0000)

    def test_f01_03_splash_color_branding(self):
        """Verify Android brand color (0x07E0 green) is used for title"""
        self.boot_view.render()
        text_entry = next((t for t in self.display.text_history if "ANDROID OS" in t["text"]), None)
        self.assertIsNotNone(text_entry)
        self.assertEqual(text_entry["color"], 0x07E0)

    def test_f01_04_splash_progress_frame_exists(self):
        """Verify progress bar frame outline is rendered at X=40, Y=140, W=240, H=14"""
        self.boot_view.render()
        rect_op = next((op for op in self.display.draw_history if op.get("op") == "drawRect" and op.get("w") == 240), None)
        self.assertIsNotNone(rect_op)
        self.assertEqual(rect_op["x"], 40)
        self.assertEqual(rect_op["y"], 140)

    def test_f01_05_splash_initial_status_text(self):
        """Verify initial boot status string is displayed"""
        self.boot_view.render()
        self.assertTrue(self.display.has_text("Initializing hardware..."))


class TestF02BootProgress(unittest.TestCase):
    """F02: Boot Progress Animation - Animated progress bar showing subsystem initialization"""

    def setUp(self):
        self.display = DisplayHAL()
        self.boot_view = BootView(self.display)

    def test_f02_01_progress_increments_percentage(self):
        """Verify progress updates percentage correctly"""
        self.boot_view.set_progress(25, "Mounting LittleFS...")
        self.assertEqual(self.boot_view.progress, 25)
        self.assertTrue(self.display.has_text("Mounting LittleFS..."))

    def test_f02_02_progress_fill_bar_dimensions(self):
        """Verify progress bar inner fill width scales with percentage"""
        self.boot_view.set_progress(50, "Initializing Touch...")
        fill_op = next((op for op in reversed(self.display.draw_history) if op.get("op") == "fillRect" and op.get("y") == 142), None)
        self.assertIsNotNone(fill_op)
        expected_width = int(236 * 0.5)
        self.assertEqual(fill_op["w"], expected_width)

    def test_f02_03_progress_100_percent_completion(self):
        """Verify 100% completion sets is_complete flag"""
        self.assertFalse(self.boot_view.is_complete)
        self.boot_view.set_progress(100, "Ready")
        self.assertTrue(self.boot_view.is_complete)

    def test_f02_04_progress_clamping_over_100(self):
        """Verify progress clamping prevents overflow > 100%"""
        self.boot_view.set_progress(150, "Overflow")
        self.assertEqual(self.boot_view.progress, 100)

    def test_f02_05_subsystem_labels_sequence(self):
        """Verify sequence of subsystem initialization text labels"""
        steps = ["Mounting LittleFS", "Starting Display HAL", "Calibrating Touch", "Loading Apps"]
        for pct, step in enumerate(steps, start=1):
            self.boot_view.set_progress(pct * 20, step)
            self.assertTrue(self.display.has_text(step))


class TestF03BootTransition(unittest.TestCase):
    """F03: Boot-to-Home Transition - Smooth transition from boot screen to Home Launcher"""

    def setUp(self):
        self.display = DisplayHAL()
        self.touch = TouchHAL()
        self.registry = AppRegistry()
        self.ui = UIManager(self.display, self.touch, self.registry)

    def test_f03_01_initial_state_is_boot(self):
        """Verify UI starts in BOOT state"""
        self.ui.start_os()
        self.assertEqual(self.ui.current_state, "BOOT")

    def test_f03_02_transition_switches_state_to_launcher(self):
        """Verify transition_to_home changes state to LAUNCHER"""
        self.ui.transition_to_home()
        self.assertEqual(self.ui.current_state, "LAUNCHER")

    def test_f03_03_transition_renders_chrome(self):
        """Verify transition renders top status bar and bottom nav bar"""
        self.ui.transition_to_home()
        # Status bar is at Y=0..19
        status_bar_fill = any(op.get("op") == "fillRect" and op.get("y") == 0 and op.get("h") == 20 for op in self.display.draw_history)
        self.assertTrue(status_bar_fill)
        # Nav bar is at Y=214..239
        nav_bar_fill = any(op.get("op") == "fillRect" and op.get("y") == 214 and op.get("h") == 26 for op in self.display.draw_history)
        self.assertTrue(nav_bar_fill)

    def test_f03_04_transition_clears_boot_logo(self):
        """Verify transition redraws screen clearing boot logo artifacts"""
        self.ui.start_os()
        self.display.draw_history.clear()
        self.ui.transition_to_home()
        self.assertTrue(any(op.get("op") == "fillScreen" for op in self.display.draw_history))

    def test_f03_05_transition_readies_touch_handling(self):
        """Verify touch handling is ready after transition"""
        self.ui.transition_to_home()
        # Touching home icon on nav bar (X=160, Y=225)
        self.ui.process_touch(160, 225, pressed=True)
        self.assertEqual(self.ui.last_nav_action, "HOME")


class TestF04StatusBar(unittest.TestCase):
    """F04: Top Status Bar - 20px status bar: live clock, heap meter, storage icon, battery"""

    def setUp(self):
        self.display = DisplayHAL()
        self.status_bar = StatusBar(self.display)

    def test_f04_01_status_bar_dimensions(self):
        """Verify status bar is exactly 20px high at top of screen (Y=0..19)"""
        self.status_bar.render()
        op = next((op for op in self.display.draw_history if op.get("op") == "fillRect" and op.get("y") == 0), None)
        self.assertIsNotNone(op)
        self.assertEqual(op["h"], 20)
        self.assertEqual(op["w"], 320)

    def test_f04_02_status_bar_live_clock(self):
        """Verify clock string renders at top left"""
        self.status_bar.update("14:35", 190, False)
        self.status_bar.render()
        self.assertTrue(self.display.has_text("14:35"))

    def test_f04_03_status_bar_heap_meter(self):
        """Verify heap metric renders in status bar"""
        self.status_bar.update("12:00", 215, False)
        self.status_bar.render()
        self.assertTrue(self.display.has_text("RAM: 215K"))

    def test_f04_04_status_bar_storage_indicator(self):
        """Verify storage indicator displays LFS:OK or SD:OK"""
        self.status_bar.update("12:00", 200, sd_mounted=False)
        self.status_bar.render()
        self.assertTrue(self.display.has_text("LFS:OK"))

        self.display.text_history.clear()
        self.status_bar.update("12:00", 200, sd_mounted=True)
        self.status_bar.render()
        self.assertTrue(self.display.has_text("SD:OK"))

    def test_f04_05_status_bar_battery_icon(self):
        """Verify battery level indicator is rendered"""
        self.status_bar.render()
        self.assertTrue(self.display.has_text("100%"))


class TestF05HomeLauncher(unittest.TestCase):
    """F05: Home App Launcher - 2x4 app grid with 44x44 icons, labels, and touch press feedback"""

    def setUp(self):
        self.display = DisplayHAL()
        self.registry = AppRegistry()
        from test.cyd_simulator.app_framework import SettingsApp, AboutApp, SU7App
        self.registry.registerApp(SettingsApp())
        self.registry.registerApp(AboutApp())
        self.registry.registerApp(SU7App())
        self.launcher = LauncherView(self.display, self.registry)

    def test_f05_01_grid_renders_registered_apps(self):
        """Verify launcher renders registered app icons and labels"""
        self.launcher.render()
        self.assertTrue(self.display.has_text("Settings"))
        self.assertTrue(self.display.has_text("About"))
        self.assertTrue(self.display.has_text("SU7 Ultr"))

    def test_f05_02_grid_icon_dimensions(self):
        """Verify icon boxes are rendered with 44x44 outline"""
        self.launcher.render()
        icon_boxes = [op for op in self.display.draw_history if op.get("op") == "drawRect" and op.get("w") == 44 and op.get("h") == 44]
        self.assertEqual(len(icon_boxes), 3)

    def test_f05_03_grid_hitbox_touch_slot_0(self):
        """Verify touching slot 0 returns app index 0 (Settings)"""
        # Slot 0 hitbox is (15, 30, 75, 100). Touch at (40, 50)
        app_idx = self.launcher.handle_touch(40, 50)
        self.assertEqual(app_idx, 0)

    def test_f05_04_grid_hitbox_touch_slot_1(self):
        """Verify touching slot 1 returns app index 1 (About)"""
        # Slot 1 hitbox is (90, 30, 150, 100). Touch at (110, 50)
        app_idx = self.launcher.handle_touch(110, 50)
        self.assertEqual(app_idx, 1)

    def test_f05_05_touch_outside_slots_returns_none(self):
        """Verify touching outside any slot returns None"""
        # Touch between rows at (50, 110)
        app_idx = self.launcher.handle_touch(50, 110)
        self.assertIsNone(app_idx)


class TestF06NavBar(unittest.TestCase):
    """F06: Bottom Navigation Bar - 26px nav bar: Back (<), Home (O), Tasks ([]) with touch priority"""

    def setUp(self):
        self.display = DisplayHAL()
        self.nav_bar = NavBar(self.display)

    def test_f06_01_nav_bar_dimensions(self):
        """Verify nav bar is 26px high at bottom of screen (Y=214..239)"""
        self.assertEqual(self.nav_bar.height, 26)
        self.assertEqual(self.nav_bar.y, 214)
        self.nav_bar.render()
        op = next((op for op in self.display.draw_history if op.get("op") == "fillRect" and op.get("y") == 214), None)
        self.assertIsNotNone(op)
        self.assertEqual(op["h"], 26)

    def test_f06_02_nav_bar_renders_three_icons(self):
        """Verify Back (<), Home (O), and Tasks ([]) are rendered"""
        self.nav_bar.render()
        self.assertTrue(self.display.has_text("<"))
        self.assertTrue(self.display.has_text("O"))
        self.assertTrue(self.display.has_text("[]"))

    def test_f06_03_back_touch_hitbox(self):
        """Verify Back button responds in range X=0..106, Y=214..239"""
        action = self.nav_bar.handle_touch(50, 220)
        self.assertEqual(action, "BACK")

    def test_f06_04_home_touch_hitbox(self):
        """Verify Home button responds in range X=107..213, Y=214..239"""
        action = self.nav_bar.handle_touch(160, 220)
        self.assertEqual(action, "HOME")

    def test_f06_05_tasks_touch_hitbox(self):
        """Verify Tasks button responds in range X=214..320, Y=214..239"""
        action = self.nav_bar.handle_touch(270, 220)
        self.assertEqual(action, "TASKS")


if __name__ == "__main__":
    unittest.main()
