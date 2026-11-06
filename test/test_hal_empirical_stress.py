#!/usr/bin/env python3
"""
Milestone 1 HAL Empirical Stress Testing Harness
Tests:
  1. TouchHAL coordinate boundary math & uint16 overflow/underflow prevention.
  2. StorageHAL VFS path routing, edge cases, deep paths, SD fallback, extension filtering.
  3. DisplayHAL backlight PWM clamping & signed vs unsigned semantic analysis.

Author: teamwork_challenger_m1_1
"""
import sys
import os
import unittest
import ctypes
from typing import Tuple, Dict, Any, List

# Ensure project root in sys.path
PROJECT_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
if PROJECT_ROOT not in sys.path:
    sys.path.insert(0, PROJECT_ROOT)

from test.cyd_simulator import TouchHAL, DisplayHAL, BoardHAL, StorageHAL


# ============================================================================
# C++ Exact Bit-Level Simulation Functions (via ctypes)
# ============================================================================

def cpp_calibrate_touch_point(raw_x: int, raw_y: int, cal: Dict[str, Any],
                              screen_w: int = 320, screen_h: int = 240) -> Tuple[int, int]:
    """
    Bit-for-bit C++ reproduction of hal::calibrateTouchPoint() from TouchHAL.cpp:
    Lines 242-273:
        int32_t width  = SCREEN_WIDTH;
        int32_t height = SCREEN_HEIGHT;
        int32_t xRange = (int32_t)s_calibration.maxRawX - (int32_t)s_calibration.minRawX;
        int32_t yRange = (int32_t)s_calibration.maxRawY - (int32_t)s_calibration.minRawY;
        if (xRange == 0) xRange = 1;
        if (yRange == 0) yRange = 1;
        int32_t mappedX = ((int32_t)rawX - (int32_t)s_calibration.minRawX) * (width - 1) / xRange;
        int32_t mappedY = ((int32_t)rawY - (int32_t)s_calibration.minRawY) * (height - 1) / yRange;
        if (s_calibration.invertX) mappedX = (width - 1) - mappedX;
        if (s_calibration.invertY) mappedY = (height - 1) - mappedY;
        if (mappedX < 0) mappedX = 0;
        else if (mappedX >= width) mappedX = width - 1;
        if (mappedY < 0) mappedY = 0;
        else if (mappedY >= height) mappedY = height - 1;
        screenX = (uint16_t)mappedX;
        screenY = (uint16_t)mappedY;
    """
    rawX = ctypes.c_int16(raw_x).value
    rawY = ctypes.c_int16(raw_y).value

    minRawX = ctypes.c_uint16(cal.get("minRawX", 200)).value
    maxRawX = ctypes.c_uint16(cal.get("maxRawX", 3700)).value
    minRawY = ctypes.c_uint16(cal.get("minRawY", 240)).value
    maxRawY = ctypes.c_uint16(cal.get("maxRawY", 3800)).value

    width = ctypes.c_int32(screen_w).value
    height = ctypes.c_int32(screen_h).value

    xRange = ctypes.c_int32(maxRawX - minRawX).value
    yRange = ctypes.c_int32(maxRawY - minRawY).value

    if xRange == 0:
        xRange = ctypes.c_int32(1).value
    if yRange == 0:
        yRange = ctypes.c_int32(1).value

    # C++ integer division truncates towards zero: int(a / b)
    numX = ctypes.c_int32((rawX - minRawX) * (width - 1)).value
    mappedX = ctypes.c_int32(int(numX / xRange)).value

    numY = ctypes.c_int32((rawY - minRawY) * (height - 1)).value
    mappedY = ctypes.c_int32(int(numY / yRange)).value

    if cal.get("invertX", False):
        mappedX = ctypes.c_int32((width - 1) - mappedX).value
    if cal.get("invertY", False):
        mappedY = ctypes.c_int32((height - 1) - mappedY).value

    if mappedX < 0:
        mappedX = 0
    elif mappedX >= width:
        mappedX = width - 1

    if mappedY < 0:
        mappedY = 0
    elif mappedY >= height:
        mappedY = height - 1

    screenX = ctypes.c_uint16(mappedX).value
    screenY = ctypes.c_uint16(mappedY).value

    return (screenX, screenY)


def cpp_set_backlight_pwm(percent_input: int) -> Tuple[int, int]:
    """
    Bit-for-bit C++ reproduction of hal::setBacklight(uint8_t percent):
    Lines 47-68 in DisplayHAL.cpp:
        void setBacklight(uint8_t percent) {
            if (percent > 100) percent = 100;
            s_backlightPercent = percent;
            uint32_t duty = ((uint32_t)percent * 255 + 50) / 100;
        }
    """
    # In C++, a caller passing an int to uint8_t undergoes unsigned conversion
    u8_percent = ctypes.c_uint8(percent_input).value
    if u8_percent > 100:
        u8_percent = 100
    duty = ctypes.c_uint32(((u8_percent * 255) + 50) // 100).value
    return (u8_percent, duty)


def cpp_is_sd_path(path: str) -> bool:
    """Reproduction of StorageHAL::isSDPath(path)"""
    if path is None:
        return False
    return path.startswith("/sd/") or path == "/sd"


def cpp_strip_sd_prefix(path: str) -> str:
    """Reproduction of StorageHAL::stripSDPrefix(path)"""
    if path is None:
        return ""
    if path.startswith("/sd/"):
        return path[3:]  # retains leading slash e.g. "/sd/apps/calc.lua" -> "/apps/calc.lua"
    if path == "/sd":
        return "/"
    return path


def cpp_has_extension(filename: str, ext: str) -> bool:
    """Reproduction of StorageHAL::hasExtension(filename, ext)"""
    if filename is None or ext is None:
        return False
    fn_len = len(filename)
    ext_len = len(ext)
    if fn_len < ext_len:
        return False
    return filename[-ext_len:].lower() == ext.lower()


# ============================================================================
# Test Suite 1: TouchHAL Coordinate Boundary Math & uint16 Overflow Stress
# ============================================================================

class TestTouchHALStress(unittest.TestCase):
    """Stress tests TouchHAL coordinate calibration under extreme ADC readings"""

    def setUp(self):
        self.default_cal = {
            "minRawX": 200,
            "maxRawX": 3700,
            "minRawY": 240,
            "maxRawY": 3800,
            "invertX": False,
            "invertY": False
        }
        self.sim_touch = TouchHAL()
        self.sim_touch.init_touch()

    def test_extreme_coordinate_0(self):
        """Test coordinate raw 0 (extreme low digitizer reading)"""
        sx, sy = cpp_calibrate_touch_point(0, 0, self.default_cal)
        self.assertEqual(sx, 0, "X should clamp to 0 screen pixel")
        self.assertEqual(sy, 0, "Y should clamp to 0 screen pixel")
        self.assertTrue(0 <= sx <= 319)
        self.assertTrue(0 <= sy <= 239)

    def test_extreme_coordinate_negative_50(self):
        """Test coordinate raw -50 (negative value, verify NO uint16 underflow to 65535 occurs)"""
        sx, sy = cpp_calibrate_touch_point(-50, -50, self.default_cal)
        self.assertEqual(sx, 0, "Negative raw coordinate -50 must clamp to 0, not underflow!")
        self.assertEqual(sy, 0, "Negative raw coordinate -50 must clamp to 0, not underflow!")
        self.assertLess(sx, 320)
        self.assertLess(sy, 240)

    def test_extreme_coordinate_4095(self):
        """Test coordinate raw 4095 (maximum 12-bit ADC value)"""
        sx, sy = cpp_calibrate_touch_point(4095, 4095, self.default_cal)
        self.assertEqual(sx, 319, "4095 exceeds maxRawX (3700) and must clamp to 319")
        self.assertEqual(sy, 239, "4095 exceeds maxRawY (3800) and must clamp to 239")
        self.assertTrue(0 <= sx <= 319)
        self.assertTrue(0 <= sy <= 239)

    def test_extreme_coordinate_5000(self):
        """Test coordinate raw 5000 (out-of-range ADC reading, verify NO uint16 overflow)"""
        sx, sy = cpp_calibrate_touch_point(5000, 5000, self.default_cal)
        self.assertEqual(sx, 319, "Out-of-range 5000 must clamp to 319")
        self.assertEqual(sy, 239, "Out-of-range 5000 must clamp to 239")
        self.assertTrue(0 <= sx <= 319)
        self.assertTrue(0 <= sy <= 239)

    def test_extreme_int16_boundaries(self):
        """Test minimum int16 (-32768) and maximum int16 (+32767)"""
        sx_min, sy_min = cpp_calibrate_touch_point(-32768, -32768, self.default_cal)
        self.assertEqual(sx_min, 0, "int16 min (-32768) must clamp safely to 0")
        self.assertEqual(sy_min, 0, "int16 min (-32768) must clamp safely to 0")

        sx_max, sy_max = cpp_calibrate_touch_point(32767, 32767, self.default_cal)
        self.assertEqual(sx_max, 319, "int16 max (+32767) must clamp safely to 319")
        self.assertEqual(sy_max, 239, "int16 max (+32767) must clamp safely to 239")

    def test_exact_calibration_boundaries(self):
        """Test exact calibration boundary values (minRaw -> 0, maxRaw -> 319/239)"""
        sx_min, sy_min = cpp_calibrate_touch_point(200, 240, self.default_cal)
        self.assertEqual(sx_min, 0, "minRawX (200) should map exactly to 0")
        self.assertEqual(sy_min, 0, "minRawY (240) should map exactly to 0")

        sx_max, sy_max = cpp_calibrate_touch_point(3700, 3800, self.default_cal)
        self.assertEqual(sx_max, 319, "maxRawX (3700) should map exactly to 319")
        self.assertEqual(sy_max, 239, "maxRawY (3800) should map exactly to 239")

    def test_off_by_one_calibration_boundaries(self):
        """Test off-by-one points adjacent to calibration limits"""
        # Just below minRaw
        sx, sy = cpp_calibrate_touch_point(199, 239, self.default_cal)
        self.assertEqual(sx, 0)
        self.assertEqual(sy, 0)

        # Just above minRaw
        sx, sy = cpp_calibrate_touch_point(201, 241, self.default_cal)
        self.assertEqual(sx, 0)
        self.assertEqual(sy, 0)

        # Just below maxRaw
        sx, sy = cpp_calibrate_touch_point(3699, 3799, self.default_cal)
        self.assertTrue(317 <= sx <= 319)
        self.assertTrue(237 <= sy <= 239)

        # Just above maxRaw
        sx, sy = cpp_calibrate_touch_point(3701, 3801, self.default_cal)
        self.assertEqual(sx, 319)
        self.assertEqual(sy, 239)

    def test_axis_inversion_with_extremes(self):
        """Test axis inversion (invertX=True, invertY=True) with extreme coordinates"""
        inverted_cal = dict(self.default_cal)
        inverted_cal["invertX"] = True
        inverted_cal["invertY"] = True

        # Coordinate 0: inverted, should map to max screen pixel (319, 239)
        sx, sy = cpp_calibrate_touch_point(0, 0, inverted_cal)
        self.assertEqual(sx, 319, "Inverted X=0 should map to 319")
        self.assertEqual(sy, 239, "Inverted Y=0 should map to 239")

        # Coordinate -50: inverted, must clamp to 319, NOT overflow
        sx, sy = cpp_calibrate_touch_point(-50, -50, inverted_cal)
        self.assertEqual(sx, 319, "Inverted X=-50 must clamp to 319")
        self.assertEqual(sy, 239, "Inverted Y=-50 must clamp to 239")

        # Coordinate 5000: inverted, must clamp to 0, NOT underflow
        sx, sy = cpp_calibrate_touch_point(5000, 5000, inverted_cal)
        self.assertEqual(sx, 0, "Inverted X=5000 must clamp to 0")
        self.assertEqual(sy, 0, "Inverted Y=5000 must clamp to 0")

    def test_zero_range_calibration_divide_by_zero(self):
        """Test zero-range calibration (minRaw == maxRaw) to verify divide-by-zero protection"""
        zero_cal = {
            "minRawX": 1500,
            "maxRawX": 1500,  # xRange == 0 -> protected by if (xRange == 0) xRange = 1
            "minRawY": 1500,
            "maxRawY": 1500,
            "invertX": False,
            "invertY": False
        }
        # Should not crash or throw floating point exception / ZeroDivisionError
        sx, sy = cpp_calibrate_touch_point(1500, 1500, zero_cal)
        self.assertTrue(0 <= sx <= 319)
        self.assertTrue(0 <= sy <= 239)

        sx, sy = cpp_calibrate_touch_point(2000, 2000, zero_cal)
        self.assertEqual(sx, 319)
        self.assertEqual(sy, 239)

    def test_inverted_range_calibration(self):
        """Test calibration where maxRaw < minRaw (negative range)"""
        rev_cal = {
            "minRawX": 3700,
            "maxRawX": 200,
            "minRawY": 3800,
            "maxRawY": 240,
            "invertX": False,
            "invertY": False
        }
        sx, sy = cpp_calibrate_touch_point(3700, 3800, rev_cal)
        self.assertEqual(sx, 0)
        self.assertEqual(sy, 0)

        sx, sy = cpp_calibrate_touch_point(200, 240, rev_cal)
        self.assertEqual(sx, 319)
        self.assertEqual(sy, 239)

        sx, sy = cpp_calibrate_touch_point(5000, 5000, rev_cal)
        self.assertEqual(sx, 0)
        self.assertEqual(sy, 0)

    def test_simulator_touch_integration(self):
        """Verify simulator TouchHAL handles extreme raw coordinates cleanly"""
        self.sim_touch.simulate_raw_touch(-50, -50, z_pressure=1000)
        pressed, x, y = self.sim_touch.get_touch()
        self.assertTrue(pressed)
        self.assertEqual(x, 0)
        self.assertEqual(y, 0)

        self.sim_touch.simulate_raw_touch(5000, 5000, z_pressure=1000)
        pressed, x, y = self.sim_touch.get_touch()
        self.assertTrue(pressed)
        self.assertEqual(x, 319)
        self.assertEqual(y, 239)


# ============================================================================
# Test Suite 2: StorageHAL VFS Path Routing Stress Suite
# ============================================================================

class TestStorageHALStress(unittest.TestCase):
    """Stress tests StorageHAL VFS path routing, fallback, deep paths, and error cases"""

    def setUp(self):
        self.storage = StorageHAL(total_bytes=10000)
        self.storage.init_storage()

    def test_null_path_safety(self):
        """Verify None/nullptr paths: C++ functions are null-safe, while Python simulator lacks None guard"""
        # C++ VFS functions handle nullptr safely
        self.assertFalse(cpp_is_sd_path(None))
        self.assertEqual(cpp_strip_sd_prefix(None), "")
        self.assertFalse(cpp_has_extension(None, ".lua"))
        self.assertFalse(cpp_has_extension("calc.lua", None))

        # Simulator robustness check:
        # C++ StorageHAL.cpp (line 227) has 'if (!path) return false;'
        # But cyd_simulator/storage_hal.py lacks 'if not filepath: return False',
        # raising AttributeError when passed None.
        with self.assertRaises(AttributeError):
            self.storage.file_exists(None)
        with self.assertRaises(AttributeError):
            self.storage.read_file(None)
        with self.assertRaises(AttributeError):
            self.storage.write_file(None, "content")
        with self.assertRaises(AttributeError):
            self.storage.delete_file(None)

    def test_empty_path_safety(self):
        """Verify empty string path is handled safely"""
        self.assertFalse(cpp_is_sd_path(""))
        self.assertEqual(cpp_strip_sd_prefix(""), "")
        self.assertFalse(self.storage.file_exists(""))
        self.assertEqual(self.storage.read_file(""), None)

    def test_sd_path_routing_resolution(self):
        """Verify /sd/ prefix detection and path stripping"""
        self.assertTrue(cpp_is_sd_path("/sd/apps/calc.lua"))
        self.assertTrue(cpp_is_sd_path("/sd/test.txt"))
        self.assertTrue(cpp_is_sd_path("/sd"))
        self.assertTrue(cpp_is_sd_path("/sd/"))
        self.assertFalse(cpp_is_sd_path("/apps/calc.lua"))
        self.assertFalse(cpp_is_sd_path("/sdcard/test.txt"))
        self.assertFalse(cpp_is_sd_path("sd/apps/calc.lua"))

        self.assertEqual(cpp_strip_sd_prefix("/sd/apps/calc.lua"), "/apps/calc.lua")
        self.assertEqual(cpp_strip_sd_prefix("/sd/file.bin"), "/file.bin")
        self.assertEqual(cpp_strip_sd_prefix("/sd"), "/")
        self.assertEqual(cpp_strip_sd_prefix("/sd/"), "/")
        self.assertEqual(cpp_strip_sd_prefix("/apps/calc.lua"), "/apps/calc.lua")

    def test_sd_fallback_when_unmounted(self):
        """Verify operations on /sd/ return False / None when SD card is unmounted"""
        self.storage.mount_sd_card(False)
        self.assertFalse(self.storage.is_sd_card_mounted())

        # Write to unmounted SD must fail
        self.assertFalse(self.storage.write_file("/sd/apps/test.lua", "data"))
        self.assertFalse(self.storage.file_exists("/sd/apps/test.lua"))
        self.assertIsNone(self.storage.read_file("/sd/apps/test.lua"))
        self.assertFalse(self.storage.delete_file("/sd/apps/test.lua"))

        # LittleFS should remain fully operational
        self.assertTrue(self.storage.write_file("/apps/local.lua", "print('hello')"))
        self.assertTrue(self.storage.file_exists("/apps/local.lua"))
        self.assertEqual(self.storage.read_file("/apps/local.lua"), "print('hello')")

    def test_sd_operations_when_mounted(self):
        """Verify operations on /sd/ succeed when SD card is mounted"""
        self.storage.mount_sd_card(True)
        self.assertTrue(self.storage.is_sd_card_mounted())

        self.assertTrue(self.storage.write_file("/sd/apps/sd_app.lua", "-- SD App"))
        self.assertTrue(self.storage.file_exists("/sd/apps/sd_app.lua"))
        self.assertEqual(self.storage.read_file("/sd/apps/sd_app.lua"), "-- SD App")
        self.assertTrue(self.storage.delete_file("/sd/apps/sd_app.lua"))
        self.assertFalse(self.storage.file_exists("/sd/apps/sd_app.lua"))

    def test_deep_paths(self):
        """Verify deeply nested file paths (e.g. /a/b/c/d/e/deep.txt)"""
        deep_path = "/a/b/c/d/e/deep.txt"
        content = "Deep file content"
        self.assertTrue(self.storage.write_file(deep_path, content))
        self.assertTrue(self.storage.file_exists(deep_path))
        self.assertEqual(self.storage.read_file(deep_path), content)

        # SD deep path
        self.storage.mount_sd_card(True)
        sd_deep_path = "/sd/lvl1/lvl2/lvl3/deep.lua"
        self.assertTrue(self.storage.write_file(sd_deep_path, "return 42"))
        self.assertTrue(self.storage.file_exists(sd_deep_path))
        self.assertEqual(self.storage.read_file(sd_deep_path), "return 42")

    def test_path_traversal_attempts(self):
        """Verify path traversal patterns like /../ and /apps/../../root"""
        # Testing how VFS resolves /..
        traversal_path = "/../secret.txt"
        self.storage.write_file(traversal_path, "secret")
        self.assertTrue(self.storage.file_exists(traversal_path))

        sd_traversal = "/sd/../escaped.txt"
        self.assertTrue(cpp_is_sd_path(sd_traversal))
        self.assertEqual(cpp_strip_sd_prefix(sd_traversal), "/../escaped.txt")

    def test_redundant_slashes(self):
        """Verify paths containing redundant slashes e.g. //apps//calc.lua"""
        self.assertTrue(self.storage.write_file("//apps//test.lua", "-- test"))
        self.assertTrue(self.storage.file_exists("//apps//test.lua"))

    def test_non_existent_directories_and_files(self):
        """Verify queries on non-existent paths return False/None cleanly without throwing"""
        self.assertFalse(self.storage.file_exists("/nonexistent/dir/file.lua"))
        self.assertIsNone(self.storage.read_file("/ghost.lua"))
        self.assertFalse(self.storage.delete_file("/nonexistent.lua"))
        self.assertEqual(self.storage.list_dir("/nonexistent_dir"), [])

    def test_extension_filter_matching(self):
        """Verify case-insensitive extension matching and edge cases"""
        self.assertTrue(cpp_has_extension("calc.lua", ".lua"))
        self.assertTrue(cpp_has_extension("calc.LUA", ".lua"))
        self.assertTrue(cpp_has_extension("CALC.LUA", ".LUA"))
        self.assertTrue(cpp_has_extension("paint.Lua", ".lua"))
        self.assertTrue(cpp_has_extension(".lua", ".lua"))  # Hidden/root extension file

        self.assertFalse(cpp_has_extension("calc.lua.bak", ".lua"))
        self.assertFalse(cpp_has_extension("calc.luax", ".lua"))
        self.assertFalse(cpp_has_extension("lua", ".lua"))
        self.assertFalse(cpp_has_extension("", ".lua"))
        self.assertFalse(cpp_has_extension("calc", ""))

    def test_auto_provisioning_preserves_user_edits(self):
        """Verify auto-provisioning is non-destructive (never overwrites user modifications)"""
        # Initial provisioning was run in setUp()
        self.assertTrue(self.storage.file_exists("/apps/calc.lua"))

        # User modifies calc.lua
        custom_calc = "-- User Custom Calculator Script"
        self.storage.write_file("/apps/calc.lua", custom_calc)
        self.assertEqual(self.storage.read_file("/apps/calc.lua"), custom_calc)

        # Trigger auto-provisioning again (e.g. system reboot)
        self.storage.provision_default_apps()

        # User modifications must be preserved!
        self.assertEqual(self.storage.read_file("/apps/calc.lua"), custom_calc,
                         "Auto-provisioning must preserve existing user modifications!")

    def test_storage_capacity_exhaustion(self):
        """Verify storage exhaustion is rejected gracefully without data corruption"""
        small_storage = StorageHAL(total_bytes=300)
        self.assertTrue(small_storage.write_file("/small.txt", "12345"))

        # Attempt to write beyond capacity
        huge_data = "Z" * 500
        ok = small_storage.write_file("/huge.txt", huge_data)
        self.assertFalse(ok, "Write beyond total storage bytes must return False")
        self.assertFalse(small_storage.file_exists("/huge.txt"))

        # Pre-existing file must be completely intact
        self.assertEqual(small_storage.read_file("/small.txt"), "12345")


# ============================================================================
# Test Suite 3: DisplayHAL Backlight PWM Clamping Stress Suite
# ============================================================================

class TestDisplayHALStress(unittest.TestCase):
    """Stress tests DisplayHAL backlight PWM clamping and signed/unsigned contracts"""

    def setUp(self):
        self.board = BoardHAL()
        self.board.init_board()

    def test_backlight_clamping_0_percent(self):
        """Test backlight at 0% (duty cycle 0, completely dark)"""
        u8_pct, duty = cpp_set_backlight_pwm(0)
        self.assertEqual(u8_pct, 0)
        self.assertEqual(duty, 0, "0% backlight must produce 0 PWM duty cycle")

        self.board.set_backlight(0)
        self.assertEqual(self.board.get_backlight(), 0)

    def test_backlight_clamping_100_percent(self):
        """Test backlight at 100% (duty cycle 255, maximum brightness)"""
        u8_pct, duty = cpp_set_backlight_pwm(100)
        self.assertEqual(u8_pct, 100)
        self.assertEqual(duty, 255, "100% backlight must produce 255 PWM duty cycle (8-bit max)")

        self.board.set_backlight(100)
        self.assertEqual(self.board.get_backlight(), 100)

    def test_backlight_clamping_150_percent(self):
        """Test backlight at 150% (exceeds max, must clamp to 100% / duty 255)"""
        u8_pct, duty = cpp_set_backlight_pwm(150)
        self.assertEqual(u8_pct, 100, "150% must clamp to 100%")
        self.assertEqual(duty, 255, "Clamped 150% must produce 255 duty cycle")

        self.board.set_backlight(150)
        self.assertEqual(self.board.get_backlight(), 100)

    def test_backlight_clamping_negative_10_percent(self):
        """
        Analyze signed vs unsigned behavior for -10%:
        1. In Python / signed API: -10 clamps to 0%.
        2. In C++ uint8_t contract: -10 undergoes unsigned conversion to 246.
           In DisplayHAL.cpp: 'if (percent > 100) percent = 100;', so 246 clamps to 100!
        """
        # Signed wrapper behavior (e.g. BoardHAL / Lua binding input validation)
        self.board.set_backlight(-10)
        self.assertEqual(self.board.get_backlight(), 0,
                         "Signed set_backlight(-10) must clamp to 0%")

        # Unsigned C++ hal::setBacklight(uint8_t) bit-level behavior
        u8_pct, duty = cpp_set_backlight_pwm(-10)
        # Note: in C++, (uint8_t)-10 == 246. Since 246 > 100, it clamps to 100 (duty 255).
        self.assertEqual(u8_pct, 100)
        self.assertEqual(duty, 255)

    def test_backlight_intermediate_values(self):
        """Test duty cycle rounding across intermediate percentage values"""
        test_points = [
            (1, 3),      # ((1 * 255) + 50) / 100 = 305 / 100 = 3
            (50, 128),   # ((50 * 255) + 50) / 100 = 12800 / 100 = 128
            (80, 204),   # ((80 * 255) + 50) / 100 = 20450 / 100 = 204 (default CYD brightness)
            (99, 252),   # ((99 * 255) + 50) / 100 = 25295 / 100 = 252
        ]
        for pct, expected_duty in test_points:
            u8_pct, duty = cpp_set_backlight_pwm(pct)
            self.assertEqual(u8_pct, pct)
            self.assertEqual(duty, expected_duty, f"Duty for {pct}% should be {expected_duty}")

    def test_backlight_history_tracking(self):
        """Verify backlight setting history tracks state transitions"""
        self.board.set_backlight(20)
        self.board.set_backlight(80)
        self.board.set_backlight(0)
        self.assertEqual(self.board.backlight_history[-3:], [20, 80, 0])


# ============================================================================
# Main Runner
# ============================================================================

def run_empirical_stress_tests() -> bool:
    print("================================================================================")
    print("      Milestone 1 HAL Empirical Stress & Adversarial Test Suite                 ")
    print("================================================================================")
    loader = unittest.TestLoader()
    suite = unittest.TestSuite()
    suite.addTests(loader.loadTestsFromTestCase(TestTouchHALStress))
    suite.addTests(loader.loadTestsFromTestCase(TestStorageHALStress))
    suite.addTests(loader.loadTestsFromTestCase(TestDisplayHALStress))

    runner = unittest.TextTestRunner(verbosity=2)
    result = runner.run(suite)

    print("================================================================================")
    print(f" TOTAL STRESS TESTS: {result.testsRun}")
    print(f" PASSED:             {result.testsRun - len(result.failures) - len(result.errors)}")
    print(f" FAILED:             {len(result.failures)}")
    print(f" ERRORS:             {len(result.errors)}")
    print("================================================================================")

    return (len(result.failures) == 0 and len(result.errors) == 0)


if __name__ == "__main__":
    success = run_empirical_stress_tests()
    sys.exit(0 if success else 1)
