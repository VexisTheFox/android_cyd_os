"""
Tier 2: Boundary & Corner Cases Tests B07 - B10 (App Framework & Lifecycle Boundaries)
Authoritative Source: PROJECT.md § Architecture, § Feature Inventory (F07..F10)
"""
import unittest
from test.cyd_simulator import DisplayHAL, TouchHAL, StorageHAL, App, AppRegistry
from test.cyd_simulator.app_framework import SettingsApp, AboutApp, DynamicScriptApp


class MinimalApp(App):
    def __init__(self, name="Min"):
        super().__init__(name, "ICON_MIN")

    def onRender(self, display, vx, vy, vw, vh):
        pass


class TestB07AppFrameworkBounds(unittest.TestCase):
    """B07: App Framework Base Class Boundaries - Lifecycle edge conditions"""

    def setUp(self):
        self.app = MinimalApp()

    def test_b07_01_double_on_create_call(self):
        """Verify calling onCreate twice maintains CREATED state and logs both calls"""
        self.app.onCreate()
        self.app.onCreate()
        self.assertEqual(self.app.state, "CREATED")
        self.assertEqual(self.app.lifecycle_log.count("onCreate"), 2)

    def test_b07_02_double_on_destroy_call(self):
        """Verify calling onDestroy twice ends in DESTROYED state"""
        self.app.onDestroy()
        self.app.onDestroy()
        self.assertEqual(self.app.state, "DESTROYED")

    def test_b07_03_pause_without_resume(self):
        """Verify calling onPause on uninitialized app sets state to PAUSED safely"""
        self.app.onPause()
        self.assertEqual(self.app.state, "PAUSED")

    def test_b07_04_touch_event_negative_coordinates(self):
        """Verify onTouch handles negative coordinates without exceptions"""
        self.app.onTouch(-15, -30, False)
        self.assertIn("onTouch(-15,-30,False)", self.app.lifecycle_log)

    def test_b07_05_empty_app_name_and_icon(self):
        """Verify app can be constructed with empty name and icon strings"""
        blank_app = MinimalApp(name="")
        self.assertEqual(blank_app.getName(), "")


class TestB08DynamicDiscoveryBounds(unittest.TestCase):
    """B08: Dynamic App Discovery Boundaries - Filesystem edge conditions"""

    def setUp(self):
        self.storage = StorageHAL()
        self.registry = AppRegistry()

    def test_b08_01_empty_apps_directory(self):
        """Verify scanning when /apps/ directory is empty finds zero script apps"""
        self.registry.scanFilesystemApps(self.storage)
        self.assertEqual(self.registry.getAppCount(), 0)

    def test_b08_02_zero_byte_script_file_discovery(self):
        """Verify a 0-byte .lua file is still discovered as an app entry"""
        self.storage.write_file("/apps/empty.lua", "")
        self.registry.scanFilesystemApps(self.storage)
        self.assertEqual(self.registry.getAppCount(), 1)
        self.assertEqual(self.registry.getApp(0).getName(), "Empty")

    def test_b08_03_special_characters_in_filename(self):
        """Verify filenames with dashes and underscores are discovered cleanly"""
        self.storage.write_file("/apps/my-app_2.lua", "-- Script")
        self.registry.scanFilesystemApps(self.storage)
        self.assertEqual(self.registry.getAppCount(), 1)
        self.assertEqual(self.registry.getApp(0).getName(), "My-app_2")

    def test_b08_04_deep_nested_paths_ignored(self):
        """Verify files in subdirectories (e.g. /apps/lib/sub.lua) are handled or isolated"""
        self.storage.write_file("/apps/lib/sub.lua", "-- Nested")
        self.registry.scanFilesystemApps(self.storage)
        # Should not register "lib" as an app
        app_names = [self.registry.getApp(i).getName() for i in range(self.registry.getAppCount())]
        self.assertNotIn("Lib", app_names)

    def test_b08_05_sd_card_toggled_during_scan(self):
        """Verify scan consistency when SD is unmounted mid-operation"""
        self.storage.mount_sd_card(True)
        self.storage.write_file("/sd/apps/sd_tool.lua", "-- Tool")
        self.storage.mount_sd_card(False)
        self.registry.scanFilesystemApps(self.storage)
        self.assertEqual(self.registry.getAppCount(), 0)


class TestB09DynamicAppLaunchBounds(unittest.TestCase):
    """B09: Dynamic App Launch Boundaries - Index out of bounds and state conflicts"""

    def setUp(self):
        self.registry = AppRegistry()
        self.app0 = SettingsApp()
        self.registry.registerApp(self.app0)

    def test_b09_01_negative_index_rejected(self):
        """Verify launching index -1 fails and leaves active app None"""
        self.assertFalse(self.registry.launchApp(-1))
        self.assertIsNone(self.registry.getActiveApp())

    def test_b09_02_large_index_rejected(self):
        """Verify launching index 9999 fails cleanly"""
        self.assertFalse(self.registry.launchApp(9999))
        self.assertIsNone(self.registry.getActiveApp())

    def test_b09_03_launch_already_active_app(self):
        """Verify launching the currently active app succeeds and remains active"""
        self.registry.launchApp(0)
        self.assertTrue(self.registry.launchApp(0))
        self.assertEqual(self.registry.getActiveApp(), self.app0)

    def test_b09_04_rapid_launch_toggling(self):
        """Verify rapidly launching between two apps leaves the last one active"""
        app1 = AboutApp()
        self.registry.registerApp(app1)
        for _ in range(5):
            self.registry.launchApp(0)
            self.registry.launchApp(1)
        self.assertEqual(self.registry.getActiveApp(), app1)
        self.assertEqual(self.app0.state, "PAUSED")
        self.assertEqual(app1.state, "RUNNING")

    def test_b09_05_launch_with_empty_registry(self):
        """Verify launching index 0 on empty registry returns False"""
        empty_reg = AppRegistry()
        self.assertFalse(empty_reg.launchApp(0))


class TestB10DynamicAppExitBounds(unittest.TestCase):
    """B10: Dynamic App Exit Boundaries - Edge conditions on app exit"""

    def setUp(self):
        self.registry = AppRegistry()
        self.app0 = SettingsApp()
        self.registry.registerApp(self.app0)

    def test_b10_01_exit_when_no_app_active(self):
        """Verify exitCurrentApp returns False when no app is active"""
        self.assertIsNone(self.registry.getActiveApp())
        self.assertFalse(self.registry.exitCurrentApp())

    def test_b10_02_double_exit_call(self):
        """Verify first exit returns True, second exit returns False"""
        self.registry.launchApp(0)
        self.assertTrue(self.registry.exitCurrentApp())
        self.assertFalse(self.registry.exitCurrentApp())

    def test_b10_03_state_after_exit_is_paused(self):
        """Verify exited app remains in PAUSED state until destroyed or relaunched"""
        self.registry.launchApp(0)
        self.registry.exitCurrentApp()
        self.assertEqual(self.app0.state, "PAUSED")

    def test_b10_04_relaunch_after_exit(self):
        """Verify an exited app can be cleanly relaunched"""
        self.registry.launchApp(0)
        self.registry.exitCurrentApp()
        self.assertTrue(self.registry.launchApp(0))
        self.assertEqual(self.app0.state, "RUNNING")

    def test_b10_05_exit_all_lifecycle_log(self):
        """Verify lifecycle callback order onCreate -> onResume -> onPause on launch and exit"""
        self.registry.launchApp(0)
        self.registry.exitCurrentApp()
        self.assertEqual(self.app0.lifecycle_log, ["onCreate", "onResume", "onPause"])


if __name__ == "__main__":
    unittest.main()
