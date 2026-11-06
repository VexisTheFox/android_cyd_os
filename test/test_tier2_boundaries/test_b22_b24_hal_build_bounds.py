"""
Tier 2: Boundary & Corner Cases Tests B22 - B24 (Hardware HAL, Storage HAL, Build System Boundaries)
Authoritative Source: PROJECT.md § Architecture, § Feature Inventory (F22..F24)
"""
import unittest
from test.cyd_simulator import DisplayHAL, TouchHAL, BoardHAL, StorageHAL


class TestB22HardwareHALBounds(unittest.TestCase):
    """B22: Hardware Abstraction Layer Boundaries - Raw ADC extremes and bus parameters"""

    def setUp(self):
        self.touch = TouchHAL()
        self.touch.init_touch()
        self.display = DisplayHAL()

    def test_b22_01_touch_adc_clamping_below_min(self):
        """Verify raw ADC touch coordinates below X_MIN (200) are clamped to 0 screen pixel"""
        x, y = self.touch.raw_to_screen(50, 50)
        self.assertEqual(x, 0)
        self.assertEqual(y, 0)

    def test_b22_02_touch_adc_clamping_above_max(self):
        """Verify raw ADC touch coordinates above X_MAX (3800) are clamped to 319, 239"""
        x, y = self.touch.raw_to_screen(4500, 4500)
        self.assertEqual(x, 319)
        self.assertEqual(y, 239)

    def test_b22_03_custom_touch_calibration(self):
        """Verify custom touch calibration offsets transform correctly"""
        self.touch.set_calibration(300, 3700, 300, 3700)
        x, y = self.touch.raw_to_screen(2000, 2000)
        self.assertAlmostEqual(x, 160, delta=2)
        self.assertAlmostEqual(y, 120, delta=2)

    def test_b22_04_touch_pressure_threshold(self):
        """Verify low Z pressure (<400) is ignored as unpressed touch event"""
        self.touch.simulate_raw_touch(2000, 2000, z_pressure=150)
        self.assertFalse(self.touch.pressed)

    def test_b22_05_display_driver_and_inversion(self):
        """Verify display driver defaults to ST7789 with BGR color order"""
        self.assertEqual(self.display.driver, "ST7789")
        self.assertEqual(self.display.rgb_order, "BGR")


class TestB23StorageHALBounds(unittest.TestCase):
    """B23: Storage HAL Boundaries - Storage full, non-existent files, unmounted SD"""

    def setUp(self):
        # Create a small simulated filesystem of 500 bytes to test out-of-space
        self.storage = StorageHAL(total_bytes=500)

    def test_b23_01_storage_full_write_fails(self):
        """Verify writing beyond storage capacity returns False without corrupting data"""
        big_content = "X" * 600
        success = self.storage.write_file("/apps/large.lua", big_content)
        self.assertFalse(success)
        self.assertFalse(self.storage.file_exists("/apps/large.lua"))

    def test_b23_02_write_exactly_at_capacity(self):
        """Verify writing content that fits exactly up to storage capacity succeeds"""
        fit_content = "A" * 500
        success = self.storage.write_file("/apps/fit.lua", fit_content)
        self.assertTrue(success)
        self.assertEqual(self.storage.get_storage_used_bytes(), 500)

    def test_b23_03_delete_non_existent_file_returns_false(self):
        """Verify deleting a file that does not exist returns False cleanly"""
        self.assertFalse(self.storage.delete_file("/apps/ghost.lua"))

    def test_b23_04_sd_write_when_unmounted_fails(self):
        """Verify writing to /sd/ when SD is unmounted returns False"""
        self.storage.mount_sd_card(False)
        success = self.storage.write_file("/sd/apps/test.lua", "-- SD")
        self.assertFalse(success)

    def test_b23_05_idempotent_provisioning(self):
        """Verify multiple calls to provision_default_apps does not duplicate content or fail"""
        self.storage.provision_default_apps()
        used_first = self.storage.get_storage_used_bytes()
        self.storage.provision_default_apps()
        used_second = self.storage.get_storage_used_bytes()
        self.assertEqual(used_first, used_second)


class TestB24MultiTargetBuildBounds(unittest.TestCase):
    """B24: Multi-Target Build Boundaries - Configuration integrity and compiler flags"""

    def setUp(self):
        with open("platformio.ini", "r") as f:
            self.ini_content = f.read()

    def test_b24_01_spi_frequency_configuration(self):
        """Verify high-speed SPI display clock (55MHz) and touch clock (2.5MHz) definitions"""
        self.assertIn("-DSPI_FREQUENCY=55000000", self.ini_content)
        self.assertIn("-DSPI_TOUCH_FREQUENCY=2500000", self.ini_content)

    def test_b24_02_tft_driver_flags_per_target(self):
        """Verify ST7789 driver flag for cyd2usb and ILI9341_2 for cyd2usb_ili9341"""
        self.assertIn("-DST7789_DRIVER=1", self.ini_content)
        self.assertIn("-DILI9341_2_DRIVER=1", self.ini_content)

    def test_b24_03_font_loading_flags(self):
        """Verify standard font loading flags are enabled for UI rendering"""
        self.assertIn("-DLOAD_GLCD=1", self.ini_content)
        self.assertIn("-DLOAD_FONT2=1", self.ini_content)
        self.assertIn("-DLOAD_FONT4=1", self.ini_content)

    def test_b24_04_framework_arduino_specified(self):
        """Verify framework is arduino and platform is espressif32"""
        self.assertIn("platform = espressif32", self.ini_content)
        self.assertIn("framework = arduino", self.ini_content)

    def test_b24_05_baud_rate_and_monitor_filters(self):
        """Verify serial monitor speed is 115200 with exception decoder filter"""
        self.assertIn("monitor_speed = 115200", self.ini_content)
        self.assertIn("monitor_filters = esp32_exception_decoder", self.ini_content)


if __name__ == "__main__":
    unittest.main()
