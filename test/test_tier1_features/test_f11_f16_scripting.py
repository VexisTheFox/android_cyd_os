"""
Tier 1: Feature Tests F11 - F16 (Scripting Engine, File Exec, Bindings, WDT Safety, Error Trapping)
Authoritative Source: PROJECT.md § Architecture, § Feature Inventory (F11..F16)
"""
import unittest
from test.cyd_simulator import DisplayHAL, TouchHAL, BoardHAL, StorageHAL, ScriptEngine, UIManager, AppRegistry


class TestF11ScriptEngine(unittest.TestCase):
    """F11: Embedded Script Engine - ANSI C Lua 5.3/5.4 runtime compiled into firmware"""

    def setUp(self):
        self.display = DisplayHAL()
        self.touch = TouchHAL()
        self.board = BoardHAL()
        self.storage = StorageHAL()
        self.engine = ScriptEngine(self.display, self.touch, self.board, self.storage)

    def test_f11_01_engine_initialization(self):
        """Verify Lua engine initializes cleanly"""
        self.assertFalse(self.engine.initialized)
        success = self.engine.init_lua_engine()
        self.assertTrue(success)
        self.assertTrue(self.engine.initialized)

    def test_f11_02_engine_state_before_execution(self):
        """Verify engine starts with no error and running=False"""
        self.engine.init_lua_engine()
        self.assertFalse(self.engine.is_script_running())
        self.assertEqual(self.engine.get_last_error(), "")

    def test_f11_03_engine_simple_expression(self):
        """Verify executing basic script string succeeds"""
        self.engine.init_lua_engine()
        success = self.engine.execute_script_string("-- Lua comment\ngui.clear(0x0000)")
        self.assertTrue(success)
        self.assertFalse(self.engine.is_script_running())

    def test_f11_04_engine_uninitialized_execution_rejected(self):
        """Verify executing without init returns False with error"""
        success = self.engine.execute_script_string("gui.clear(0)")
        self.assertFalse(success)
        self.assertIn("EngineNotInitialized", self.engine.get_last_error())

    def test_f11_05_engine_reinitialization_resets_state(self):
        """Verify re-init clears previous errors and instruction counters"""
        self.engine.init_lua_engine()
        self.engine.last_error = "Previous error"
        self.engine.instruction_count = 42
        self.engine.init_lua_engine()
        self.assertEqual(self.engine.last_error, "")
        self.assertEqual(self.engine.instruction_count, 0)


class TestF12ScriptFileExecution(unittest.TestCase):
    """F12: Script File Execution - Load and run .lua scripts stored on LittleFS or SD storage"""

    def setUp(self):
        self.display = DisplayHAL()
        self.touch = TouchHAL()
        self.board = BoardHAL()
        self.storage = StorageHAL()
        self.storage.init_storage()
        self.engine = ScriptEngine(self.display, self.touch, self.board, self.storage)
        self.engine.init_lua_engine()

    def test_f12_01_execute_littlefs_script(self):
        """Verify executing existing script file from LittleFS"""
        self.storage.write_file("/apps/test.lua", "gui.drawText('Test', 10, 10, 0xFFFF)")
        success = self.engine.execute_script_file("/apps/test.lua")
        self.assertTrue(success)
        self.assertTrue(self.display.has_text("Test"))

    def test_f12_02_execute_bundled_calc_script(self):
        """Verify executing auto-provisioned calc.lua"""
        success = self.engine.execute_script_file("/apps/calc.lua")
        self.assertTrue(success)
        self.assertTrue(self.display.has_text("Calculator"))

    def test_f12_03_execute_bundled_paint_script(self):
        """Verify executing auto-provisioned paint.lua"""
        success = self.engine.execute_script_file("/apps/paint.lua")
        self.assertTrue(success)
        self.assertTrue(self.display.has_text("Touch to Paint"))

    def test_f12_04_execute_non_existent_file_fails(self):
        """Verify executing missing file fails with FileNotFound error"""
        success = self.engine.execute_script_file("/apps/non_existent.lua")
        self.assertFalse(success)
        self.assertIn("FileNotFound", self.engine.get_last_error())

    def test_f12_05_execute_from_sd_storage(self):
        """Verify executing script from SD card when mounted"""
        self.storage.mount_sd_card(True)
        self.storage.write_file("/sd/apps/sd_demo.lua", "gui.drawText('SD App', 20, 20, 0x07E0)")
        success = self.engine.execute_script_file("/sd/apps/sd_demo.lua")
        self.assertTrue(success)
        self.assertTrue(self.display.has_text("SD App"))


class TestF13GUIScriptBindings(unittest.TestCase):
    """F13: GUI Script Bindings - Lua API for drawing primitives: pixels, rects, text, colors"""

    def setUp(self):
        self.display = DisplayHAL()
        self.touch = TouchHAL()
        self.board = BoardHAL()
        self.storage = StorageHAL()
        self.engine = ScriptEngine(self.display, self.touch, self.board, self.storage)
        self.engine.init_lua_engine()

    def test_f13_01_gui_clear(self):
        """Verify gui.clear(color) fills screen with specified RGB565 color"""
        self.engine.execute_script_string("gui.clear(0x001F)")
        self.assertEqual(self.display.get_pixel(0, 0), 0x001F)

    def test_f13_02_gui_draw_text(self):
        """Verify gui.drawText(text, x, y, color) renders text to display"""
        self.engine.execute_script_string("gui.drawText('Hello Lua', 50, 60, 0xFFE0)")
        self.assertTrue(self.display.has_text("Hello Lua"))

    def test_f13_03_gui_draw_rect(self):
        """Verify gui.drawRect(x, y, w, h, color) outlines rectangle"""
        self.engine.execute_script_string("gui.drawRect(10, 10, 80, 40, 0xF800)")
        has_rect = any(op.get("op") == "drawRect" and op.get("w") == 80 and op.get("h") == 40 for op in self.display.draw_history)
        self.assertTrue(has_rect)

    def test_f13_04_gui_fill_rect(self):
        """Verify gui.fillRect(x, y, w, h, color) fills rectangle"""
        self.engine.execute_script_string("gui.fillRect(20, 30, 50, 50, 0x07E0)")
        has_fill = any(op.get("op") == "fillRect" and op.get("w") == 50 and op.get("h") == 50 for op in self.display.draw_history)
        self.assertTrue(has_fill)

    def test_f13_05_gui_color_format_rgb565(self):
        """Verify standard 16-bit RGB565 hex colors are correctly parsed"""
        self.engine.execute_script_string("gui.clear(0xF800)")  # Pure Red
        self.assertEqual(self.display.get_pixel(100, 100), 0xF800)


class TestF14SysBindings(unittest.TestCase):
    """F14: System & Input Script Bindings - Lua API for touch coordinates, delay, heap, LED, and brightness"""

    def setUp(self):
        self.display = DisplayHAL()
        self.touch = TouchHAL()
        self.board = BoardHAL()
        self.storage = StorageHAL()
        self.engine = ScriptEngine(self.display, self.touch, self.board, self.storage)
        self.engine.init_lua_engine()

    def test_f14_01_sys_set_backlight(self):
        """Verify sys.setBacklight(pct) modifies PWM duty cycle"""
        self.engine.execute_script_string("sys.setBacklight(65)")
        self.assertEqual(self.board.get_backlight(), 65)

    def test_f14_02_sys_set_rgb_led(self):
        """Verify sys.setLED(r, g, b) controls active-low RGB LEDs"""
        self.engine.execute_script_string("sys.setLED(1, 0, 1)")
        self.assertEqual(self.board.get_rgb_led(), (1, 0, 1))

    def test_f14_03_input_touch_query(self):
        """Verify input.getTouch returns touch state and coordinates"""
        self.touch.init_touch()
        self.touch.simulate_touch(120, 80, pressed=True)
        touched, x, y = self.touch.get_touch()
        self.assertTrue(touched)
        self.assertEqual(x, 120)
        self.assertEqual(y, 80)

    def test_f14_04_sys_backlight_clamping(self):
        """Verify sys.setBacklight clamps between 0 and 100"""
        self.engine.execute_script_string("sys.setBacklight(150)")
        self.assertEqual(self.board.get_backlight(), 100)
        self.engine.execute_script_string("sys.setBacklight(-20)")
        self.assertEqual(self.board.get_backlight(), 0)

    def test_f14_05_board_tone_generation(self):
        """Verify board tone generator records frequency and duration"""
        self.board.play_tone(1000, 150)
        self.assertEqual(len(self.board.tone_history), 1)
        self.assertEqual(self.board.tone_history[0], {"freq": 1000, "duration": 150})


class TestF15ScriptSandboxWDT(unittest.TestCase):
    """F15: Script Sandboxing & WDT Safety - lua_sethook instruction counter feeding WDT & allowing aborts"""

    def setUp(self):
        self.display = DisplayHAL()
        self.touch = TouchHAL()
        self.board = BoardHAL()
        self.storage = StorageHAL()
        self.engine = ScriptEngine(self.display, self.touch, self.board, self.storage)
        self.engine.init_lua_engine()

    def test_f15_01_instruction_hook_increments(self):
        """Verify instructions increment hook counter"""
        self.engine.execute_script_string("gui.clear(0)\ngui.drawText('A',1,1,1)\ngui.drawText('B',2,2,1)")
        self.assertGreater(self.engine.instruction_count, 0)

    def test_f15_02_wdt_feed_on_counter_rollover(self):
        """Verify WDT reset is called every 1000 instructions"""
        self.engine.max_instructions = 10000
        # Trigger 2500 hooks
        for _ in range(2500):
            self.engine._execute_hook()
        self.assertGreaterEqual(self.engine.wdt_resets, 2)

    def test_f15_03_instruction_limit_abort(self):
        """Verify script exceeding max instruction threshold is aborted safely"""
        self.engine.max_instructions = 5
        # 6 statements
        script = "\n".join(["gui.clear(0)"] * 8)
        success = self.engine.execute_script_string(script)
        self.assertFalse(success)
        self.assertIn("ExecutionTimeout", self.engine.get_last_error())

    def test_f15_04_manual_abort_flag(self):
        """Verify calling abort_current_script halts execution"""
        self.engine.abort_current_script()
        self.assertTrue(self.engine.abort_requested)
        success = self.engine.execute_script_string("gui.clear(0)")
        self.assertFalse(success)
        self.assertIn("ScriptAborted", self.engine.get_last_error())

    def test_f15_05_safe_state_after_abort(self):
        """Verify engine is not stuck in running state after abort"""
        self.engine.abort_current_script()
        self.engine.execute_script_string("gui.clear(0)")
        self.assertFalse(self.engine.is_script_running())


class TestF16ZeroCrashErrorTrapping(unittest.TestCase):
    """F16: Zero-Crash Error Trapping - lua_pcall protected execution with modal crash alert UI"""

    def setUp(self):
        self.display = DisplayHAL()
        self.touch = TouchHAL()
        self.board = BoardHAL()
        self.storage = StorageHAL()
        self.engine = ScriptEngine(self.display, self.touch, self.board, self.storage)
        self.engine.init_lua_engine()
        self.registry = AppRegistry()
        self.ui = UIManager(self.display, self.touch, self.registry)

    def test_f16_01_syntax_error_trapped(self):
        """Verify syntax error in script is caught without crashing"""
        script = "gui.drawText('Unclosed string, 10, 10)"
        success = self.engine.execute_script_string(script)
        self.assertFalse(success)
        self.assertIn("SyntaxError", self.engine.get_last_error())

    def test_f16_02_runtime_error_trapped(self):
        """Verify runtime error (e.g. error() call) is caught without crashing"""
        script = 'error("Null pointer dereference")'
        success = self.engine.execute_script_string(script)
        self.assertFalse(success)
        self.assertIn("RuntimeError", self.engine.get_last_error())

    def test_f16_03_modal_error_dialog_renders(self):
        """Verify UI displays modal error dialog on script failure"""
        self.ui.show_error_modal("SyntaxError: unmatched parentheses")
        self.assertEqual(self.ui.current_state, "ERROR_MODAL")
        self.assertTrue(self.display.has_text("Application Error"))
        self.assertTrue(self.display.has_text("DISMISS"))

    def test_f16_04_modal_error_dismissal(self):
        """Verify tapping dismiss button returns UI safely to Home Launcher"""
        self.ui.show_error_modal("Test Error")
        # Dismiss button hitbox: X=100..220, Y=135..165
        self.ui.process_touch(150, 150, pressed=True)
        self.assertEqual(self.ui.current_state, "LAUNCHER")

    def test_f16_05_modal_error_dismissal_via_home_nav(self):
        """Verify pressing Nav Bar Home button also dismisses error modal"""
        self.ui.show_error_modal("Test Error")
        # Nav Bar Home: X=160, Y=225
        self.ui.process_touch(160, 225, pressed=True)
        self.assertEqual(self.ui.current_state, "LAUNCHER")


if __name__ == "__main__":
    unittest.main()
