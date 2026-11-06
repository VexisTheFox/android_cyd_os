"""
Tier 1: Feature Tests F07 - F10 (App Framework, Dynamic Discovery, Launch, Exit)
Authoritative Source: PROJECT.md § Architecture, § Feature Inventory (F07..F10)
"""
import unittest
from test.cyd_simulator import DisplayHAL, TouchHAL, StorageHAL, App, AppRegistry, UIManager
from test.cyd_simulator.app_framework import SettingsApp, AboutApp, SU7App, DynamicScriptApp


class DummyApp(App):
    def __init__(self, name="Dummy"):
        super().__init__(name, "ICON_DUMMY")

    def onRender(self, display, vx, vy, vw, vh):
        display.fill_rect(vx, vy, vw, vh, 0x001F)


class TestF07AppFramework(unittest.TestCase):
    """F07: App Framework Base Class - Object-oriented App base class with lifecycle hooks"""

    def setUp(self):
        self.app = DummyApp()

    def test_f07_01_app_initial_state(self):
        """Verify App starts in UNINITIALIZED state"""
        self.assertEqual(self.app.state, "UNINITIALIZED")
        self.assertEqual(self.app.getName(), "Dummy")
        self.assertEqual(self.app.getIcon(), "ICON_DUMMY")

    def test_f07_02_app_on_create_lifecycle(self):
        """Verify onCreate advances state to CREATED and records event"""
        self.app.onCreate()
        self.assertEqual(self.app.state, "CREATED")
        self.assertIn("onCreate", self.app.lifecycle_log)

    def test_f07_03_app_on_resume_and_pause_lifecycle(self):
        """Verify onResume and onPause update state correctly"""
        self.app.onCreate()
        self.app.onResume()
        self.assertEqual(self.app.state, "RUNNING")
        self.app.onPause()
        self.assertEqual(self.app.state, "PAUSED")

    def test_f07_04_app_on_render_invocation(self):
        """Verify onRender draws to viewport within designated boundaries"""
        display = DisplayHAL()
        self.app.onRender(display, 0, 20, 320, 194)
        rendered_rect = any(op.get("op") == "fillRect" and op.get("y") == 20 and op.get("h") == 194 for op in display.draw_history)
        self.assertTrue(rendered_rect)

    def test_f07_05_app_on_touch_event_handling(self):
        """Verify onTouch receives coordinate data and press state"""
        self.app.onTouch(150, 100, True)
        self.assertIn("onTouch(150,100,True)", self.app.lifecycle_log)


class TestF08DynamicAppDiscovery(unittest.TestCase):
    """F08: Dynamic App Discovery - Discover apps from /apps/ on LittleFS and /sd/apps/ on SD"""

    def setUp(self):
        self.storage = StorageHAL()
        self.registry = AppRegistry()

    def test_f08_01_discovery_from_littlefs(self):
        """Verify discovery of .lua files from LittleFS /apps/"""
        self.storage.init_storage()  # Auto-provisions calc.lua, paint.lua, sysinfo.lua
        self.registry.scanFilesystemApps(self.storage)
        app_names = [self.registry.getApp(i).getName() for i in range(self.registry.getAppCount())]
        self.assertIn("Calc", app_names)
        self.assertIn("Paint", app_names)
        self.assertIn("Sysinfo", app_names)

    def test_f08_02_ignore_non_lua_files(self):
        """Verify non-.lua files (e.g. .txt, .json) are ignored by discovery"""
        self.storage.write_file("/apps/readme.txt", "Some text")
        self.storage.write_file("/apps/config.json", "{}")
        self.registry.scanFilesystemApps(self.storage)
        app_names = [self.registry.getApp(i).getName() for i in range(self.registry.getAppCount())]
        self.assertNotIn("Readme", app_names)
        self.assertNotIn("Config", app_names)

    def test_f08_03_discovery_from_microsd(self):
        """Verify discovery of apps from /sd/apps/ when SD is mounted"""
        self.storage.mount_sd_card(True)
        self.storage.write_file("/sd/apps/game.lua", "-- A game script")
        self.registry.scanFilesystemApps(self.storage)
        app_names = [self.registry.getApp(i).getName() for i in range(self.registry.getAppCount())]
        self.assertIn("SD-Game", app_names)

    def test_f08_04_sd_unmounted_ignores_sd_apps(self):
        """Verify SD apps are not discovered if SD card is not mounted"""
        self.storage.mount_sd_card(False)
        self.storage.write_file("/sd/apps/game.lua", "-- A game script")
        self.registry.scanFilesystemApps(self.storage)
        app_names = [self.registry.getApp(i).getName() for i in range(self.registry.getAppCount())]
        self.assertNotIn("SD-Game", app_names)

    def test_f08_05_idempotent_discovery(self):
        """Verify scanning multiple times does not register duplicate apps"""
        self.storage.init_storage()
        self.registry.scanFilesystemApps(self.storage)
        count_first = self.registry.getAppCount()
        self.registry.scanFilesystemApps(self.storage)
        count_second = self.registry.getAppCount()
        self.assertEqual(count_first, count_second)


class TestF09DynamicAppLaunch(unittest.TestCase):
    """F09: Dynamic App Launch - Launch native/script apps into active viewport from launcher"""

    def setUp(self):
        self.registry = AppRegistry()
        self.app0 = SettingsApp()
        self.app1 = AboutApp()
        self.registry.registerApp(self.app0)
        self.registry.registerApp(self.app1)

    def test_f09_01_launch_sets_active_app(self):
        """Verify launchApp sets active app and index"""
        self.assertIsNone(self.registry.getActiveApp())
        success = self.registry.launchApp(0)
        self.assertTrue(success)
        self.assertEqual(self.registry.getActiveApp(), self.app0)

    def test_f09_02_launch_triggers_create_and_resume(self):
        """Verify launching invokes onCreate then onResume"""
        self.registry.launchApp(1)
        self.assertEqual(self.app1.state, "RUNNING")
        self.assertEqual(self.app1.lifecycle_log, ["onCreate", "onResume"])

    def test_f09_03_launch_switches_running_apps(self):
        """Verify launching new app pauses previously running app"""
        self.registry.launchApp(0)
        self.assertEqual(self.app0.state, "RUNNING")
        self.registry.launchApp(1)
        self.assertEqual(self.app0.state, "PAUSED")
        self.assertEqual(self.app1.state, "RUNNING")

    def test_f09_04_ui_manager_launch_updates_state(self):
        """Verify UIManager state changes to APP upon launch"""
        display = DisplayHAL()
        touch = TouchHAL()
        ui = UIManager(display, touch, self.registry)
        ui.transition_to_home()
        # Touch app 0 at slot (40, 50)
        ui.process_touch(40, 50, pressed=True)
        self.assertEqual(ui.current_state, "APP")
        self.assertEqual(self.registry.getActiveApp(), self.app0)

    def test_f09_05_invalid_index_launch_fails(self):
        """Verify launching non-existent index returns False"""
        self.assertFalse(self.registry.launchApp(99))
        self.assertFalse(self.registry.launchApp(-1))


class TestF10DynamicAppExit(unittest.TestCase):
    """F10: Dynamic App Exit - Return safely to Home Launcher via Nav Bar or script exit"""

    def setUp(self):
        self.registry = AppRegistry()
        self.app0 = SettingsApp()
        self.registry.registerApp(self.app0)
        self.display = DisplayHAL()
        self.touch = TouchHAL()
        self.ui = UIManager(self.display, self.touch, self.registry)

    def test_f10_01_exit_clears_active_app(self):
        """Verify exitCurrentApp clears active app reference"""
        self.registry.launchApp(0)
        self.assertIsNotNone(self.registry.getActiveApp())
        success = self.registry.exitCurrentApp()
        self.assertTrue(success)
        self.assertIsNone(self.registry.getActiveApp())

    def test_f10_02_exit_pauses_app(self):
        """Verify exit pauses the running app"""
        self.registry.launchApp(0)
        self.registry.exitCurrentApp()
        self.assertEqual(self.app0.state, "PAUSED")

    def test_f10_03_nav_home_triggers_exit(self):
        """Verify touching Nav Bar Home icon exits app and returns to LAUNCHER"""
        self.ui.transition_to_home()
        self.ui.process_touch(40, 50, pressed=True)  # Launch app 0
        self.assertEqual(self.ui.current_state, "APP")

        # Touch Home button on Nav Bar: X=160, Y=225
        self.ui.process_touch(160, 225, pressed=True)
        self.assertEqual(self.ui.current_state, "LAUNCHER")
        self.assertIsNone(self.registry.getActiveApp())

    def test_f10_04_nav_back_triggers_exit(self):
        """Verify touching Nav Bar Back icon exits app and returns to LAUNCHER"""
        self.ui.transition_to_home()
        self.ui.process_touch(40, 50, pressed=True)  # Launch app 0
        self.assertEqual(self.ui.current_state, "APP")

        # Touch Back button on Nav Bar: X=50, Y=225
        self.ui.process_touch(50, 225, pressed=True)
        self.assertEqual(self.ui.current_state, "LAUNCHER")
        self.assertIsNone(self.registry.getActiveApp())

    def test_f10_05_exit_restores_launcher_view(self):
        """Verify exiting app redraws launcher icons on screen"""
        self.ui.transition_to_home()
        self.ui.process_touch(40, 50, pressed=True)
        self.display.text_history.clear()
        self.ui.process_touch(160, 225, pressed=True)  # Exit
        self.assertTrue(self.display.has_text("Settings"))


if __name__ == "__main__":
    unittest.main()
