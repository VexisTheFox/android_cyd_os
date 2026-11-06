"""
Tier 1: Feature Tests F22 - F24 (Hardware HAL, Storage HAL & Provisioning, Multi-Target Clean Build)
Authoritative Source: PROJECT.md § Architecture, § Feature Inventory (F22..F24)
"""
import os
import unittest
import subprocess
from test.cyd_simulator import DisplayHAL, TouchHAL, BoardHAL, StorageHAL


class TestF22HardwareHAL(unittest.TestCase):
    """F22: Hardware Abstraction Layer - ST7789 display, XPT2046 touch on dedicated SPI, RGB LED, LDR"""

    def setUp(self):
        self.display = DisplayHAL()
        self.touch = TouchHAL()
        self.board = BoardHAL()

    def test_f22_01_display_hal_init(self):
        """Verify Display HAL initializes ST7789 controller with landscape 320x240 orientation"""
        self.display.init_display()
        self.assertTrue(self.display.initialized)
        self.assertEqual(self.display.width, 320)
        self.assertEqual(self.display.height, 240)
        self.assertEqual(self.display.rotation, 1)

    def test_f22_02_touch_hal_dedicated_spi_coordinates(self):
        """Verify Touch HAL accurately transforms ADC raw coordinates to screen pixels"""
        self.touch.init_touch()
        # Raw center: (X_MIN+X_MAX)/2 = 2000, (Y_MIN+Y_MAX)/2 = 2000
        x, y = self.touch.raw_to_screen(2000, 2000)
        self.assertAlmostEqual(x, 160, delta=2)
        self.assertAlmostEqual(y, 120, delta=2)

    def test_f22_03_touch_hal_screen_to_raw_roundtrip(self):
        """Verify screen coordinate to raw ADC coordinate mapping roundtrips cleanly"""
        self.touch.init_touch()
        raw_x, raw_y = self.touch.screen_to_raw(160, 120)
        scr_x, scr_y = self.touch.raw_to_screen(raw_x, raw_y)
        self.assertAlmostEqual(scr_x, 160, delta=2)
        self.assertAlmostEqual(scr_y, 120, delta=2)

    def test_f22_04_rgb_led_pins_active_low_inversion(self):
        """Verify RGB LED outputs invert logical boolean states for active-low hardware"""
        self.board.init_board()
        # All OFF logically
        self.board.set_rgb_led(0, 0, 0)
        self.assertEqual((self.board.led_r, self.board.led_g, self.board.led_b), (1, 1, 1))
        # All ON logically
        self.board.set_rgb_led(1, 1, 1)
        self.assertEqual((self.board.led_r, self.board.led_g, self.board.led_b), (0, 0, 0))

    def test_f22_05_ldr_adc_photoresistor_reading(self):
        """Verify LDR photoresistor reads 12-bit ADC range 0..4095"""
        self.board.set_ldr_value(1024)
        self.assertEqual(self.board.read_ldr(), 1024)
        self.board.set_ldr_value(4095)
        self.assertEqual(self.board.read_ldr(), 4095)


class TestF23StorageHAL(unittest.TestCase):
    """F23: Storage HAL & Provisioning - LittleFS auto-mount, SD support, PROGMEM auto-provisioning"""

    def setUp(self):
        self.storage = StorageHAL()

    def test_f23_01_littlefs_auto_mount(self):
        """Verify LittleFS mounts successfully and marks initialized"""
        success = self.storage.init_storage()
        self.assertTrue(success)
        self.assertTrue(self.storage.initialized)

    def test_f23_02_default_apps_auto_provisioned(self):
        """Verify auto-provisioning writes bundled apps when directory is empty"""
        self.storage.init_storage()
        self.assertTrue(self.storage.auto_provision_done)
        self.assertTrue(self.storage.file_exists("/apps/calc.lua"))
        self.assertTrue(self.storage.file_exists("/apps/paint.lua"))
        self.assertTrue(self.storage.file_exists("/apps/sysinfo.lua"))

    def test_f23_03_sd_card_detection(self):
        """Verify SD card mount detection and unmount fallback"""
        self.assertFalse(self.storage.is_sd_card_mounted())
        self.storage.mount_sd_card(True)
        self.assertTrue(self.storage.is_sd_card_mounted())
        self.storage.mount_sd_card(False)
        self.assertFalse(self.storage.is_sd_card_mounted())

    def test_f23_04_storage_capacity_reporting(self):
        """Verify total and used storage byte calculations"""
        self.storage.init_storage()
        self.assertEqual(self.storage.get_storage_total_bytes(), 1974272)
        used = self.storage.get_storage_used_bytes()
        self.assertGreater(used, 100)
        self.assertLess(used, 1974272)

    def test_f23_05_file_write_and_read(self):
        """Verify writing and reading a new file via Storage HAL"""
        self.storage.init_storage()
        self.storage.write_file("/apps/my_test.lua", "-- Test Content")
        content = self.storage.read_file("/apps/my_test.lua")
        self.assertEqual(content, "-- Test Content")


class TestF24MultiTargetBuild(unittest.TestCase):
    """F24: Multi-Target Clean Build - Compile cleanly via PlatformIO for cyd2usb, cyd2usb_ili9341, cyd"""

    def setUp(self):
        self.ini_path = "platformio.ini"
        with open(self.ini_path, "r") as f:
            self.ini_content = f.read()

    def test_f24_01_platformio_ini_exists(self):
        """Verify platformio.ini is present in workspace root"""
        self.assertTrue(os.path.exists(self.ini_path))

    def test_f24_02_all_three_targets_defined(self):
        """Verify all three targets: cyd2usb, cyd2usb_ili9341, and cyd are configured"""
        self.assertIn("[env:cyd2usb]", self.ini_content)
        self.assertIn("[env:cyd2usb_ili9341]", self.ini_content)
        self.assertIn("[env:cyd]", self.ini_content)

    def test_f24_03_default_env_is_cyd2usb(self):
        """Verify default_envs is cyd2usb"""
        self.assertIn("default_envs = cyd2usb", self.ini_content)

    def test_f24_04_hardware_pin_declarations(self):
        """Verify all display, touch, and peripheral hardware pins are declared in build_flags"""
        required_pins = [
            "-DTFT_MOSI=13", "-DTFT_MISO=12", "-DTFT_SCLK=14", "-DTFT_CS=15",
            "-DTFT_DC=2", "-DTFT_BL=21",
            "-DXPT2046_MOSI=32", "-DXPT2046_MISO=39", "-DXPT2046_CLK=25", "-DXPT2046_CS=33",
            "-DCYD_LED_RED=4", "-DCYD_LED_GREEN=16", "-DCYD_LED_BLUE=17",
            "-DCYD_LDR=34", "-DCYD_SPEAKER=26"
        ]
        for pin in required_pins:
            self.assertIn(pin, self.ini_content, f"Missing build flag: {pin}")

    def test_f24_05_platformio_build_clean(self):
        """Verify PlatformIO compiles cleanly for default target with zero errors"""
        res = subprocess.run(["pio", "run"], shell=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        self.assertEqual(res.returncode, 0, f"pio run failed:\n{res.stderr}\n{res.stdout}")
        self.assertTrue(os.path.exists(".pio/build/cyd2usb/firmware.bin"))


if __name__ == "__main__":
    unittest.main()
