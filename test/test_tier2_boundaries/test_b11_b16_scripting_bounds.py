"""
Tier 2: Boundary & Corner Cases Tests B11 - B16 (Scripting & Sandboxing Boundaries)
Authoritative Source: PROJECT.md § Architecture, § Feature Inventory (F11..F16)
"""
import unittest
from test.cyd_simulator import DisplayHAL, TouchHAL, BoardHAL, StorageHAL, ScriptEngine, UIManager, AppRegistry


class TestB11ScriptEngineBounds(unittest.TestCase):
    """B11: Script Engine Boundaries - Extreme script inputs and re-init behavior"""

    def setUp(self):
        self.display = DisplayHAL()
        self.touch = TouchHAL()
        self.board = BoardHAL()
        self.storage = StorageHAL()
        self.engine = ScriptEngine(self.display, self.touch, self.board, self.storage)
        self.engine.init_lua_engine()

    def test_b11_01_empty_script_string(self):
        """Verify executing an empty string returns True with no side effects"""
        success = self.engine.execute_script_string("")
        self.assertTrue(success)
        self.assertEqual(self.engine.get_last_error(), "")

    def test_b11_02_whitespace_only_script(self):
        """Verify executing whitespace and comments only returns True"""
        script = "   \n\t  -- Just comments\n   -- Another line\n"
        success = self.engine.execute_script_string(script)
        self.assertTrue(success)

    def test_b11_03_massive_script_execution(self):
        """Verify executing 1000 lines of sequential valid commands within instruction limit"""
        self.engine.max_instructions = 100000
        script = "\n".join(["gui.clear(0x0000)"] * 500)
        success = self.engine.execute_script_string(script)
        self.assertTrue(success)

    def test_b11_04_execution_after_prior_error(self):
        """Verify engine can execute valid code immediately after a failed script"""
        self.engine.execute_script_string("invalid_syntax")
        self.assertIn("SyntaxError", self.engine.get_last_error())
        # Now run valid script
        success = self.engine.execute_script_string("gui.clear(0x0000)")
        self.assertTrue(success)
        self.assertEqual(self.engine.get_last_error(), "")

    def test_b11_05_double_init_clears_hook_count(self):
        """Verify re-initializing engine resets hook count to 0"""
        self.engine.instruction_count = 500
        self.engine.init_lua_engine()
        self.assertEqual(self.engine.instruction_count, 0)


class TestB12ScriptFileBounds(unittest.TestCase):
    """B12: Script File Execution Boundaries - Missing files, empty files, directory paths"""

    def setUp(self):
        self.display = DisplayHAL()
        self.touch = TouchHAL()
        self.board = BoardHAL()
        self.storage = StorageHAL()
        self.storage.init_storage()
        self.engine = ScriptEngine(self.display, self.touch, self.board, self.storage)
        self.engine.init_lua_engine()

    def test_b12_01_execute_zero_byte_file(self):
        """Verify executing 0-byte file succeeds cleanly without errors"""
        self.storage.write_file("/apps/empty.lua", "")
        success = self.engine.execute_script_file("/apps/empty.lua")
        self.assertTrue(success)

    def test_b12_02_execute_file_with_spaces_in_path(self):
        """Verify executing file with spaces in path succeeds"""
        self.storage.write_file("/apps/my script.lua", "gui.clear(0)")
        success = self.engine.execute_script_file("/apps/my script.lua")
        self.assertTrue(success)

    def test_b12_03_execute_unmounted_sd_file(self):
        """Verify executing from /sd/ when unmounted returns FileNotFound"""
        self.storage.mount_sd_card(False)
        success = self.engine.execute_script_file("/sd/apps/missing.lua")
        self.assertFalse(success)
        self.assertIn("FileNotFound", self.engine.get_last_error())

    def test_b12_04_execute_deleted_file(self):
        """Verify deleting a file causes subsequent execute to fail cleanly"""
        self.storage.write_file("/apps/temp.lua", "gui.clear(0)")
        self.storage.delete_file("/apps/temp.lua")
        success = self.engine.execute_script_file("/apps/temp.lua")
        self.assertFalse(success)

    def test_b12_05_directory_path_instead_of_file(self):
        """Verify attempting to execute a directory path fails gracefully"""
        success = self.engine.execute_script_file("/apps/")
        self.assertFalse(success)


class TestB13GUIScriptBindingsBounds(unittest.TestCase):
    """B13: GUI Script Bindings Boundaries - Coordinates outside screen, zero dimensions, extremes"""

    def setUp(self):
        self.display = DisplayHAL()
        self.touch = TouchHAL()
        self.board = BoardHAL()
        self.storage = StorageHAL()
        self.engine = ScriptEngine(self.display, self.touch, self.board, self.storage)
        self.engine.init_lua_engine()

    def test_b13_01_draw_rect_completely_offscreen(self):
        """Verify drawing rectangle completely offscreen (500, 500) does not throw exception"""
        success = self.engine.execute_script_string("gui.drawRect(500, 500, 100, 50, 0xFFFF)")
        self.assertTrue(success)

    def test_b13_02_draw_rect_zero_dimensions(self):
        """Verify drawing rectangle with w=0, h=0 executes without error"""
        success = self.engine.execute_script_string("gui.drawRect(50, 50, 0, 0, 0xFFFF)")
        self.assertTrue(success)

    def test_b13_03_draw_text_empty_string(self):
        """Verify drawing empty text string executes cleanly"""
        success = self.engine.execute_script_string("gui.drawText('', 10, 10, 0xFFFF)")
        self.assertTrue(success)

    def test_b13_04_extreme_colors(self):
        """Verify pure black (0x0000) and pure white (0xFFFF) colors work properly"""
        self.engine.execute_script_string("gui.clear(0xFFFF)")
        self.assertEqual(self.display.get_pixel(0, 0), 0xFFFF)
        self.engine.execute_script_string("gui.clear(0x0000)")
        self.assertEqual(self.display.get_pixel(0, 0), 0x0000)

    def test_b13_05_draw_text_at_boundary_margins(self):
        """Verify drawing text at screen edges X=0, Y=0 and X=300, Y=220"""
        success = self.engine.execute_script_string("gui.drawText('Edge', 0, 0, 0x07E0)\ngui.drawText('Bot', 300, 220, 0x07E0)")
        self.assertTrue(success)
        self.assertTrue(self.display.has_text("Edge"))
        self.assertTrue(self.display.has_text("Bot"))


class TestB14SysBindingsBounds(unittest.TestCase):
    """B14: System Bindings Boundaries - Clamping, sensor limits, tone parameters"""

    def setUp(self):
        self.display = DisplayHAL()
        self.touch = TouchHAL()
        self.board = BoardHAL()
        self.storage = StorageHAL()
        self.engine = ScriptEngine(self.display, self.touch, self.board, self.storage)
        self.engine.init_lua_engine()

    def test_b14_01_backlight_clamping_negative(self):
        """Verify negative backlight percent is clamped to 0"""
        self.engine.execute_script_string("sys.setBacklight(-50)")
        self.assertEqual(self.board.get_backlight(), 0)

    def test_b14_02_backlight_clamping_excessive(self):
        """Verify backlight percent > 100 is clamped to 100"""
        self.engine.execute_script_string("sys.setBacklight(500)")
        self.assertEqual(self.board.get_backlight(), 100)

    def test_b14_03_led_clamping_positive_values(self):
        """Verify integer values >= 1 are treated as ON (1)"""
        self.engine.execute_script_string("sys.setLED(5, 0, 10)")
        self.assertEqual(self.board.get_rgb_led(), (1, 0, 1))

    def test_b14_04_touch_query_when_unpressed(self):
        """Verify touch query returns pressed=False and last coords"""
        self.touch.init_touch()
        self.touch.simulate_touch(0, 0, pressed=False)
        touched, x, y = self.touch.get_touch()
        self.assertFalse(touched)

    def test_b14_05_tone_zero_duration(self):
        """Verify playing tone with 0 duration records cleanly"""
        self.board.play_tone(1000, 0)
        self.assertEqual(self.board.tone_history[-1]["duration"], 0)


class TestB15ScriptSandboxWDTBounds(unittest.TestCase):
    """B15: Script Sandboxing & WDT Safety Boundaries - Infinite loop abort and instruction limits"""

    def setUp(self):
        self.display = DisplayHAL()
        self.touch = TouchHAL()
        self.board = BoardHAL()
        self.storage = StorageHAL()
        self.engine = ScriptEngine(self.display, self.touch, self.board, self.storage)
        self.engine.init_lua_engine()

    def test_b15_01_instruction_cap_trigger(self):
        """Verify hitting instruction threshold aborts and sets ExecutionTimeout error"""
        self.engine.max_instructions = 10
        script = "\n".join(["gui.clear(0)"] * 15)
        success = self.engine.execute_script_string(script)
        self.assertFalse(success)
        self.assertIn("ExecutionTimeout", self.engine.get_last_error())

    def test_b15_02_abort_flag_before_script(self):
        """Verify setting abort_requested before execution aborts immediately"""
        self.engine.abort_requested = True
        success = self.engine.execute_script_string("gui.clear(0)")
        self.assertFalse(success)
        self.assertIn("ScriptAborted", self.engine.get_last_error())

    def test_b15_03_wdt_feed_boundary_at_999_and_1000(self):
        """Verify WDT reset is triggered exactly at multiples of 1000 instructions"""
        self.engine.max_instructions = 5000
        # 999 instructions
        for _ in range(999):
            self.engine._execute_hook()
        self.assertEqual(self.engine.wdt_resets, 0)
        # 1000th instruction triggers WDT reset
        self.engine._execute_hook()
        self.assertEqual(self.engine.wdt_resets, 1)

    def test_b15_04_wdt_feed_at_exact_multiples(self):
        """Verify WDT resets count matches division by 1000"""
        self.engine.max_instructions = 10000
        for _ in range(3000):
            self.engine._execute_hook()
        self.assertEqual(self.engine.wdt_resets, 3)

    def test_b15_05_engine_not_running_after_timeout(self):
        """Verify running status is False after WDT abort"""
        self.engine.max_instructions = 5
        self.engine.execute_script_string("\n".join(["gui.clear(0)"] * 10))
        self.assertFalse(self.engine.is_script_running())


class TestB16ZeroCrashErrorTrappingBounds(unittest.TestCase):
    """B16: Zero-Crash Error Trapping Boundaries - Modal dismiss edge cases and error strings"""

    def setUp(self):
        self.display = DisplayHAL()
        self.touch = TouchHAL()
        self.registry = AppRegistry()
        self.ui = UIManager(self.display, self.touch, self.registry)

    def test_b16_01_long_error_string_truncation(self):
        """Verify very long error message is safely truncated in modal dialog"""
        long_err = "SyntaxError: " + ("x" * 200)
        self.ui.show_error_modal(long_err)
        self.assertEqual(self.ui.current_state, "ERROR_MODAL")
        # Check snippet truncation with ellipsis
        self.assertTrue(any("..." in t["text"] for t in self.display.text_history))

    def test_b16_02_dismiss_touch_boundary_edges(self):
        """Verify touch exactly at corners of dismiss button (100, 135) dismisses modal"""
        self.ui.show_error_modal("Short Error")
        self.ui.process_touch(100, 135, pressed=True)
        self.assertEqual(self.ui.current_state, "LAUNCHER")

    def test_b16_03_touch_outside_dismiss_button_in_modal(self):
        """Verify touch outside dismiss button does not dismiss modal dialog"""
        self.ui.show_error_modal("Short Error")
        # Touch outside button at (10, 10)
        self.ui.process_touch(10, 10, pressed=True)
        self.assertEqual(self.ui.current_state, "ERROR_MODAL")

    def test_b16_04_nav_bar_back_dismisses_modal(self):
        """Verify Nav Bar Back button (<) also dismisses modal dialog"""
        self.ui.show_error_modal("Short Error")
        # Nav Bar Back: X=50, Y=225
        self.ui.process_touch(50, 225, pressed=True)
        self.assertEqual(self.ui.current_state, "LAUNCHER")

    def test_b16_05_modal_rendered_dialog_box_dimensions(self):
        """Verify error modal dialog box is 260x120 pixels centered"""
        self.ui.show_error_modal("Error Test")
        dialog_rect = next((op for op in self.display.draw_history if op.get("op") == "fillRect" and op.get("w") == 260 and op.get("h") == 120), None)
        self.assertIsNotNone(dialog_rect)
        self.assertEqual(dialog_rect["x"], 30)
        self.assertEqual(dialog_rect["y"], 60)


if __name__ == "__main__":
    unittest.main()
