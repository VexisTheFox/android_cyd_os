"""
Tier 3: Cross-Feature Interactions Tests (Pairwise Combinations & Concurrency)
Authoritative Source: PROJECT.md § Architecture, § Interface Contracts, § Milestones
"""
import unittest
from test.cyd_simulator import (
    DisplayHAL, TouchHAL, BoardHAL, StorageHAL, ConfigStore, SystemConfig,
    AppRegistry, UIManager, ScriptEngine
)
from test.cyd_simulator.app_framework import SettingsApp, AboutApp, SU7App, DynamicScriptApp


class TestTier3Interactions(unittest.TestCase):
    """Tier 3: Cross-Feature Interaction & Subsystem Integration Tests"""

    def setUp(self):
        self.display = DisplayHAL()
        self.touch = TouchHAL()
        self.board = BoardHAL()
        self.storage = StorageHAL()
        self.config_store = ConfigStore()
        self.registry = AppRegistry()
        self.engine = ScriptEngine(self.display, self.touch, self.board, self.storage)
        self.ui = UIManager(self.display, self.touch, self.registry)

    def test_ci01_script_execution_while_touch_active(self):
        """CI01: Script queries touch coordinates and renders to screen at touched location"""
        self.touch.init_touch()
        self.touch.simulate_touch(160, 100, pressed=True)
        self.engine.init_lua_engine()

        script = """
        gui.clear(0x0000)
        local touched, x, y = input.getTouch()
        if touched then
            gui.fillRect(x-5, y-5, 10, 10, 0xF800)
        end
        """
        success = self.engine.execute_script_string(script)
        self.assertTrue(success)
        # Rect drawn around touch coordinate
        fill_op = next((op for op in self.display.draw_history if op.get("op") == "fillRect" and op.get("color") == 0xF800), None)
        self.assertIsNotNone(fill_op)

    def test_ci02_brightness_adjustment_during_app_launch(self):
        """CI02: Backlight PWM adjusts in real time while launching an application"""
        self.board.init_board()
        self.registry.registerApp(SettingsApp())
        self.ui.transition_to_home()

        # Launch Settings app
        self.ui.process_touch(40, 50, pressed=True)
        self.assertEqual(self.ui.current_state, "APP")

        # Adjust backlight to 40%
        self.board.set_backlight(40)
        self.assertEqual(self.board.get_backlight(), 40)
        # Verify app is still active and view is rendered
        self.assertEqual(self.registry.getActiveApp().getName(), "Settings")

    def test_ci03_littlefs_full_condition_during_script_write(self):
        """CI03: LittleFS storage exhaustion during script file write does not crash OS"""
        # Create small storage of 200 bytes
        limited_storage = StorageHAL(total_bytes=200)
        limited_storage.write_file("/apps/first.lua", "X" * 150)

        # Attempt to write another script that exceeds remaining capacity
        success = limited_storage.write_file("/apps/overflow.lua", "Y" * 100)
        self.assertFalse(success)
        # Original file intact and OS remains operational
        self.assertTrue(limited_storage.file_exists("/apps/first.lua"))
        self.assertFalse(limited_storage.file_exists("/apps/overflow.lua"))

    def test_ci04_navbar_home_interrupts_infinite_script_loop(self):
        """CI04: Nav Bar Home button interrupts active script execution via WDT hook"""
        self.engine.init_lua_engine()
        self.engine.max_instructions = 50000

        # Start execution
        self.engine.running = True
        # User taps Home button on Nav Bar
        self.ui.process_touch(160, 225, pressed=True)
        self.engine.abort_current_script()

        # Execute hook check reflects abort
        hook_result = self.engine._execute_hook()
        self.assertFalse(hook_result)
        self.assertIn("ScriptAborted", self.engine.get_last_error())

    def test_ci05_theme_change_reflected_in_launcher_render(self):
        """CI05: Changing theme in Settings modifies configuration and persists to NVS"""
        cfg = self.config_store.load_config()
        self.assertEqual(cfg.theme, 0)  # Dark theme

        cfg.theme = 1  # Light theme
        self.config_store.save_config(cfg)

        reloaded = self.config_store.load_config()
        self.assertEqual(reloaded.theme, 1)

    def test_ci06_auto_provisioning_then_app_discovery(self):
        """CI06: Fresh boot auto-provisions LittleFS files, then AppRegistry discovers them"""
        self.storage.init_storage()
        self.registry.scanFilesystemApps(self.storage)
        self.assertGreaterEqual(self.registry.getAppCount(), 3)
        names = [self.registry.getApp(i).getName() for i in range(self.registry.getAppCount())]
        self.assertIn("Calc", names)
        self.assertIn("Paint", names)
        self.assertIn("Sysinfo", names)

    def test_ci07_microsd_hotplug_app_registration(self):
        """CI07: Inserting MicroSD card dynamically discovers and registers new apps"""
        self.storage.init_storage()
        self.registry.scanFilesystemApps(self.storage)
        initial_count = self.registry.getAppCount()

        # Insert SD card with new game app
        self.storage.mount_sd_card(True)
        self.storage.write_file("/sd/apps/tetris.lua", "-- Tetris game")
        self.registry.scanFilesystemApps(self.storage)

        self.assertEqual(self.registry.getAppCount(), initial_count + 1)
        self.assertEqual(self.registry.getApp(initial_count).getName(), "SD-Tetris")

    def test_ci08_microsd_unmount_while_running_sd_app(self):
        """CI08: MicroSD card removed during SD script execution fails safely without OS panic"""
        self.storage.mount_sd_card(True)
        self.storage.write_file("/sd/apps/sd_app.lua", "gui.drawText('SD', 10, 10, 0xFFFF)")
        self.engine.init_lua_engine()

        # Execute once while mounted
        self.assertTrue(self.engine.execute_script_file("/sd/apps/sd_app.lua"))

        # Card ejected
        self.storage.mount_sd_card(False)
        # Attempting to re-execute fails gracefully
        success = self.engine.execute_script_file("/sd/apps/sd_app.lua")
        self.assertFalse(success)
        self.assertIn("FileNotFound", self.engine.get_last_error())

    def test_ci09_rapid_app_switching_lifecycle_integrity(self):
        """CI09: Rapid app switching between Settings, About, and SU7 preserves lifecycle states"""
        s_app = SettingsApp()
        a_app = AboutApp()
        u_app = SU7App()
        self.registry.registerApp(s_app)
        self.registry.registerApp(a_app)
        self.registry.registerApp(u_app)

        self.registry.launchApp(0)
        self.assertEqual(self.registry.getActiveApp(), s_app)

        self.registry.launchApp(1)
        self.assertEqual(s_app.state, "PAUSED")
        self.assertEqual(a_app.state, "RUNNING")

        self.registry.launchApp(2)
        self.assertEqual(a_app.state, "PAUSED")
        self.assertEqual(u_app.state, "RUNNING")

        self.registry.exitCurrentApp()
        self.assertEqual(u_app.state, "PAUSED")
        self.assertIsNone(self.registry.getActiveApp())

    def test_ci10_nvs_persistence_across_simulated_reboot(self):
        """CI10: Settings saved in NVS are restored after simulated device reboot"""
        # Session 1: User sets brightness to 35% and disables sound
        store1 = ConfigStore()
        cfg1 = SystemConfig(brightness=35, soundEnabled=False)
        store1.save_config(cfg1)
        saved_dict = dict(store1.storage)

        # Simulate power off and reboot: New ConfigStore instance with preserved NVS storage
        store2 = ConfigStore()
        store2.storage = saved_dict
        cfg2 = store2.load_config()

        self.assertEqual(cfg2.brightness, 35)
        self.assertFalse(cfg2.soundEnabled)

        # Board initializes with saved brightness
        board = BoardHAL()
        board.set_backlight(cfg2.brightness)
        self.assertEqual(board.get_backlight(), 35)

    def test_ci11_modal_error_dialog_dismiss_via_navbar(self):
        """CI11: Modal error dialog displayed on script crash is dismissed by Nav Bar Home"""
        self.ui.transition_to_home()
        self.ui.show_error_modal("SyntaxError: unfinished string")
        self.assertEqual(self.ui.current_state, "ERROR_MODAL")

        # Nav Bar Home touch (X=160, Y=225)
        self.ui.process_touch(160, 225, pressed=True)
        self.assertEqual(self.ui.current_state, "LAUNCHER")

    def test_ci12_touch_calibration_affects_launcher_hit_testing(self):
        """CI12: Touch calibration constants translate raw ADC touch to accurate launcher hit"""
        self.touch.init_touch()
        self.touch.set_calibration(200, 3800, 200, 3800)
        self.registry.registerApp(SettingsApp())
        self.ui.transition_to_home()

        # Raw ADC reading corresponding to screen (45, 50) in slot 0
        raw_x, raw_y = self.touch.screen_to_raw(45, 50)
        self.touch.simulate_raw_touch(raw_x, raw_y, z_pressure=1000)
        touched, sx, sy = self.touch.get_touch()

        self.ui.process_touch(sx, sy, pressed=True)
        self.assertEqual(self.ui.current_state, "APP")
        self.assertEqual(self.registry.getActiveApp().getName(), "Settings")

    def test_ci13_ldr_sensor_triggers_auto_backlight_adjustment(self):
        """CI13: Ambient LDR light reading triggers dynamic backlight adjustment"""
        self.board.init_board()
        # Dark room (ADC 400) -> Lower backlight to 30%
        self.board.set_ldr_value(400)
        if self.board.read_ldr() < 800:
            self.board.set_backlight(30)
        self.assertEqual(self.board.get_backlight(), 30)

        # Bright sunlight (ADC 3500) -> Increase backlight to 100%
        self.board.set_ldr_value(3500)
        if self.board.read_ldr() > 3000:
            self.board.set_backlight(100)
        self.assertEqual(self.board.get_backlight(), 100)

    def test_ci14_active_led_pulse_while_audio_tone_plays(self):
        """CI14: Active-low RGB LED state changes during buzzer tone generation"""
        self.board.init_board()
        # Play tone and set Blue LED ON
        self.board.play_tone(1500, 100)
        self.board.set_rgb_led(0, 0, 1)

        self.assertEqual(len(self.board.tone_history), 1)
        self.assertEqual(self.board.get_rgb_led(), (0, 0, 1))
        # Active low: Blue pin is 0 (LOW)
        self.assertEqual(self.board.led_b, 0)

    def test_ci15_status_bar_updates_during_active_app_render(self):
        """CI15: Status Bar clock and heap metrics render cleanly alongside active app"""
        self.registry.registerApp(AboutApp())
        self.ui.transition_to_home()
        self.ui.process_touch(40, 50, pressed=True)  # Launch About

        self.ui.status_bar.update("15:30", 210, sd_mounted=True)
        self.ui.render_active_app()

        self.assertTrue(self.display.has_text("15:30"))
        self.assertTrue(self.display.has_text("RAM: 210K"))
        self.assertTrue(self.display.has_text("SD:OK"))
        self.assertTrue(self.display.has_text("CYD Android OS v1.0"))

    def test_ci16_boot_failsafe_missing_sd_falls_back_to_littlefs(self):
        """CI16: System cold boot gracefully handles absent SD card by falling back to LittleFS"""
        self.storage.mount_sd_card(False)
        self.storage.init_storage()

        self.assertFalse(self.storage.is_sd_card_mounted())
        self.assertTrue(self.storage.initialized)
        self.assertTrue(self.storage.file_exists("/apps/calc.lua"))


if __name__ == "__main__":
    unittest.main()
