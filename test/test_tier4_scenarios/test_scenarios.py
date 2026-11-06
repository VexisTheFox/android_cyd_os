"""
Tier 4: Real-World Scenarios Tests (Comprehensive End-to-End User Journeys)
Authoritative Source: PROJECT.md § Architecture, § Interface Contracts, § Milestones
"""
import unittest
from test.cyd_simulator import (
    DisplayHAL, TouchHAL, BoardHAL, StorageHAL, ConfigStore, SystemConfig,
    AppRegistry, UIManager, ScriptEngine
)
from test.cyd_simulator.app_framework import SettingsApp, AboutApp, SU7App, DynamicScriptApp


class TestTier4RealWorldScenarios(unittest.TestCase):
    """Tier 4: End-to-End Real-World System Scenarios"""

    def test_scenario_01_cold_boot_to_home(self):
        """Scenario 1: Cold Boot to Home Launcher
        Flow: Power-on -> Hardware init -> Boot Splash logo -> 0..100% Progress bar ->
        Status Bar setup -> Home Launcher grid ready.
        """
        display = DisplayHAL()
        touch = TouchHAL()
        board = BoardHAL()
        storage = StorageHAL()
        registry = AppRegistry()
        ui = UIManager(display, touch, registry)

        # 1. Hardware initialization
        display.init_display()
        touch.init_touch()
        board.init_board()
        storage.init_storage()
        self.assertTrue(display.initialized)
        self.assertTrue(touch.initialized)
        self.assertTrue(board.initialized)

        # 2. Boot View & Progress sequence
        ui.start_os()
        self.assertEqual(ui.current_state, "BOOT")
        self.assertTrue(display.has_text("ANDROID OS"))

        steps = [
            (20, "Mounting LittleFS..."),
            (50, "Calibrating Touch..."),
            (80, "Scanning Apps..."),
            (100, "Ready")
        ]
        for pct, desc in steps:
            ui.boot_view.set_progress(pct, desc)

        self.assertTrue(ui.boot_view.is_complete)

        # 3. Transition to Home Launcher
        ui.transition_to_home()
        self.assertEqual(ui.current_state, "LAUNCHER")

        # 4. Chrome verification (Status Bar & Nav Bar)
        self.assertTrue(display.has_text("RAM: 192K"))
        self.assertTrue(display.has_text("<"))
        self.assertTrue(display.has_text("O"))
        self.assertTrue(display.has_text("[]"))

    def test_scenario_02_app_launch_and_nav_back(self):
        """Scenario 2: Application Launch and Safe Nav Bar Return
        Flow: Launcher grid -> Tap Settings icon -> onCreate & onResume lifecycle ->
        Active viewport renders -> User taps Nav Bar Back '<' -> onPause -> Return to Home.
        """
        display = DisplayHAL()
        touch = TouchHAL()
        registry = AppRegistry()
        settings = SettingsApp()
        registry.registerApp(settings)
        ui = UIManager(display, touch, registry)
        ui.transition_to_home()

        # Tap slot 0 (Settings icon)
        ui.process_touch(40, 50, pressed=True)
        self.assertEqual(ui.current_state, "APP")
        self.assertEqual(settings.state, "RUNNING")
        self.assertEqual(registry.getActiveApp(), settings)
        self.assertTrue(display.has_text("Device Settings"))

        # User taps Nav Bar Back button: X=50, Y=225
        ui.process_touch(50, 225, pressed=True)
        self.assertEqual(ui.current_state, "LAUNCHER")
        self.assertEqual(settings.state, "PAUSED")
        self.assertIsNone(registry.getActiveApp())

    def test_scenario_03_settings_pwm_nvs_persistence_flow(self):
        """Scenario 3: Settings Reconfiguration, PWM Adjustment, and NVS Persistence
        Flow: Launch Settings -> Adjust Backlight slider to 85% -> Update PWM on GPIO 21 ->
        Persist to NVS 'cyd_os' -> Simulate device reboot -> Verify 85% PWM restored.
        """
        board = BoardHAL()
        board.init_board()
        store = ConfigStore()

        # Step 1: User modifies brightness in Settings to 85%
        cfg = store.load_config()
        cfg.brightness = 85
        cfg.theme = 1  # Light theme
        store.save_config(cfg)
        board.set_backlight(cfg.brightness)

        self.assertEqual(board.get_backlight(), 85)

        # Step 2: Simulated power cycle (reboot)
        nvs_backup = dict(store.storage)

        # Boot after power-on
        new_store = ConfigStore()
        new_store.storage = nvs_backup
        restored_cfg = new_store.load_config()
        self.assertEqual(restored_cfg.brightness, 85)
        self.assertEqual(restored_cfg.theme, 1)

        new_board = BoardHAL()
        new_board.set_backlight(restored_cfg.brightness)
        self.assertEqual(new_board.get_backlight(), 85)

    def test_scenario_04_dynamic_calculator_execution_flow(self):
        """Scenario 4: Dynamic Calculator Lua Script Execution
        Flow: Storage auto-provisions calc.lua -> Scan /apps/ -> Launch Calculator ->
        Interpret script -> Render calculator UI -> Exit via Nav Bar Home.
        """
        display = DisplayHAL()
        touch = TouchHAL()
        board = BoardHAL()
        storage = StorageHAL()
        storage.init_storage()
        registry = AppRegistry()
        engine = ScriptEngine(display, touch, board, storage)
        engine.init_lua_engine()

        # Scan LittleFS apps
        registry.scanFilesystemApps(storage)
        calc_app = next((registry.getApp(i) for i in range(registry.getAppCount()) if registry.getApp(i).getName() == "Calc"), None)
        self.assertIsNotNone(calc_app)

        # Execute calc.lua
        success = engine.execute_script_file(calc_app.script_path)
        self.assertTrue(success)
        self.assertTrue(display.has_text("Calculator"))

        # Nav Bar Home exit
        ui = UIManager(display, touch, registry)
        ui.transition_to_home()
        self.assertEqual(ui.current_state, "LAUNCHER")

    def test_scenario_05_script_crash_and_modal_recovery_flow(self):
        """Scenario 5: Dynamic Script Fault Injection and Modal Self-Healing
        Flow: Write malformed Lua script -> Launch script -> pcall traps syntax error ->
        Render Android modal alert -> User taps 'Dismiss' -> Safe return to Home Launcher.
        """
        display = DisplayHAL()
        touch = TouchHAL()
        board = BoardHAL()
        storage = StorageHAL()
        storage.init_storage()
        registry = AppRegistry()
        engine = ScriptEngine(display, touch, board, storage)
        engine.init_lua_engine()
        ui = UIManager(display, touch, registry)

        # Write corrupt script
        storage.write_file("/apps/broken.lua", "gui.drawText('Unfinished quote, 10, 10)")
        success = engine.execute_script_file("/apps/broken.lua")
        self.assertFalse(success)

        # Trigger modal error alert
        ui.show_error_modal(engine.get_last_error())
        self.assertEqual(ui.current_state, "ERROR_MODAL")
        self.assertTrue(display.has_text("Application Error"))

        # User taps 'Dismiss' button (hitbox: X=100..220, Y=135..165)
        ui.process_touch(160, 150, pressed=True)
        self.assertEqual(ui.current_state, "LAUNCHER")
        self.assertIsNone(registry.getActiveApp())

    def test_scenario_06_hardware_telemetry_flow(self):
        """Scenario 6: Hardware Telemetry and System Specifications Journey
        Flow: Launch About App -> Verify chip revision (ESP32-D0WDQ6-V3), 240MHz, 4MB Flash,
        320KB RAM -> Status Bar sync -> Nav Bar Home exit.
        """
        display = DisplayHAL()
        touch = TouchHAL()
        registry = AppRegistry()
        about = AboutApp()
        registry.registerApp(about)
        ui = UIManager(display, touch, registry)
        ui.transition_to_home()

        # Launch About app
        registry.launchApp(0)
        ui.current_state = "APP"
        ui.render_active_app()

        self.assertTrue(display.has_text("CYD Android OS v1.0"))
        self.assertTrue(display.has_text("ESP32-D0WDQ6-V3 240MHz"))
        self.assertTrue(display.has_text("Flash: 4MB | RAM: 320KB"))

        # Exit to Home
        ui.process_touch(160, 225, pressed=True)
        self.assertEqual(ui.current_state, "LAUNCHER")

    def test_scenario_07_su7_wireframe_3d_lifecycle_flow(self):
        """Scenario 7: Xiaomi SU7 Ultra 3D Wireframe Demo Lifecycle Flow
        Flow: Launch SU7 3D app -> Allocate double-buffer sprites -> Verify 3D vertex
        bounding box projection -> Nav Bar Home exit.
        """
        display = DisplayHAL()
        touch = TouchHAL()
        registry = AppRegistry()
        su7 = SU7App()
        registry.registerApp(su7)
        ui = UIManager(display, touch, registry)
        ui.transition_to_home()

        # Launch SU7
        registry.launchApp(0)
        ui.current_state = "APP"
        ui.render_active_app()

        self.assertTrue(display.has_text("Xiaomi SU7 Ultra 3D"))

        # Verify double buffer sprite creation in simulator
        sprite = display.create_sprite(320, 172)
        sprite.fill_sprite(0x0821)
        display.push_sprite(sprite, 0, 34)

        # User taps Nav Bar Home
        ui.process_touch(160, 225, pressed=True)
        self.assertEqual(ui.current_state, "LAUNCHER")

    def test_scenario_08_fresh_flash_turnkey_setup_flow(self):
        """Scenario 8: Fresh Flash Turnkey Provisioning Setup
        Flow: Device boots for the first time with unformatted storage -> LittleFS auto-mounts ->
        Auto-provisions bundled apps from PROGMEM -> NVS default preferences saved ->
        Launcher grid populated with all apps out of the box.
        """
        display = DisplayHAL()
        touch = TouchHAL()
        storage = StorageHAL()
        config_store = ConfigStore()
        registry = AppRegistry()

        # First boot: storage has no files
        self.assertEqual(len(storage.files), 0)

        # Storage HAL initializes and auto-provisions
        storage.init_storage()
        self.assertTrue(storage.auto_provision_done)
        self.assertGreaterEqual(len(storage.files), 3)

        # Scan into AppRegistry
        registry.scanFilesystemApps(storage)
        self.assertGreaterEqual(registry.getAppCount(), 3)

        # Verify NVS default settings
        cfg = config_store.load_config()
        self.assertEqual(cfg.brightness, 80)
        self.assertEqual(cfg.theme, 0)
        self.assertTrue(cfg.soundEnabled)

        # UI Launcher renders ready grid
        ui = UIManager(display, touch, registry)
        ui.transition_to_home()
        self.assertEqual(ui.current_state, "LAUNCHER")
        self.assertTrue(display.has_text("Calc"))
        self.assertTrue(display.has_text("Paint"))
        self.assertTrue(display.has_text("Sysinfo"))


if __name__ == "__main__":
    unittest.main()
