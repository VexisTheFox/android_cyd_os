# CYD Android-like OS Prototype — E2E Test Infrastructure Specification

## 1. Dual-Track Testing Philosophy

The CYD Android-like OS project adopts a rigorous **Dual-Track Testing Architecture**:
1. **Implementation Track**: Progressive milestone development across M1 (Hardware Abstraction & Storage), M2 (UI Shell & Navigation), M3 (App Framework & Settings/About), M4 (Scripting Engine & Dynamic Apps), and M5 (Integration & Verification).
2. **E2E Testing Track**: Autonomous, opaque-box, requirement-driven test development operating in parallel. The test suite exercises the system against the authoritative specifications defined in `PROJECT.md` and `ORIGINAL_REQUEST.md`.

### Principles of Opaque-Box Requirement Testing
- **Behavior-Driven Contracts**: Tests validate functional inputs against expected outputs, state transitions, hardware pinouts, and error codes without relying on private implementation details.
- **Progressive Testability**: The test suite can run at any stage of development. It validates static contracts and platform configuration immediately, exercises the high-fidelity simulator harness across all 24 features and 4 tiers, and validates firmware build targets via PlatformIO (`pio run`).
- **Deterministic Derivation**: Every test case derives its expected values directly from explicit requirements, mathematical projections, or hardware constraints.
- **Self-Containment & Zero Inter-Test Leakage**: Each test initializes its own isolated state (memory, simulated NVS flash, mock filesystems, and viewport buffers) and cleans up after execution.

---

## 2. Test Suite Tier Hierarchy

The test suite is structured into four comprehensive tiers, delivering multi-layered coverage across all 24 features:

```
+-------------------------------------------------------------------------+
|                  Tier 4: Real-World Scenarios (>= 8 Flows)              |
|   End-to-End User Workflows: Boot -> Shell -> App -> PWM -> Script ->   |
|   Crash Recovery -> LittleFS Persistence -> Clean Home Return           |
+-------------------------------------------------------------------------+
|             Tier 3: Cross-Feature Interactions (>= 15 Combinations)     |
|   Pairwise & Concurrency: Script while Touch Active, Theme during App,  |
|   LittleFS Full during Script Write, WDT Hook with Touch Interrupt      |
+-------------------------------------------------------------------------+
|            Tier 2: Boundary & Corner Cases (>= 120 Test Cases)          |
|   >= 5 tests per feature: Limits, Min/Max Clamping, Off-Screen Hits,   |
|   Corrupt NVS, Division by Zero, Storage Exhaustion, Invalid Scripts    |
+-------------------------------------------------------------------------+
|                Tier 1: Feature Coverage (>= 120 Test Cases)             |
|   >= 5 tests per feature: Primary Happy-Path behaviors for F01..F24     |
+-------------------------------------------------------------------------+
```

### Tier 1: Feature Coverage (>= 120 tests; >= 5 per feature)
Covers all 24 features (F01 through F24) in `PROJECT.md`:
- **F01 (Boot Splash Screen)**: Logo display, dimensions, background clear, splash duration, visual elements.
- **F02 (Boot Progress Animation)**: Progress bar steps (0..100%), smooth increments, subsystem labels, rendering bounds, completion flag.
- **F03 (Boot-to-Home Transition)**: Automatic handoff, view deallocation, launcher activation, status bar activation, nav bar activation.
- **F04 (Top Status Bar)**: 20px height (Y=0..19), clock formatting, heap meter, storage mount indicator, battery icon rendering.
- **F05 (Home App Launcher)**: 2x4 grid layout, 44x44 icon hitboxes, app labels, touch press feedback, scrolling/paging layout.
- **F06 (Bottom Navigation Bar)**: 26px height (Y=214..239), Back (`<`), Home (`O`), Tasks (`[]`) buttons, touch priority override.
- **F07 (App Framework Base Class)**: Lifecycle hooks (`onCreate`, `onResume`, `onUpdate`, `onRender`, `onTouch`, `onPause`, `onDestroy`), polymorphic dispatch.
- **F08 (Dynamic App Discovery)**: Directory scanning (`/apps/`, `/sd/apps/`), `.lua` extension filtering, metadata parsing, icon assignment.
- **F09 (Dynamic App Launch)**: Viewport assignment (Y=20..213, 194px), focus transfer, lifecycle pause of launcher, resume of target app.
- **F10 (Dynamic App Exit)**: Nav Bar Home click return, script self-exit, memory deallocation, launcher resumption.
- **F11 (Embedded Script Engine)**: Lua 5.3/5.4 VM lifecycle, initialization, memory allocation cap, garbage collection, string evaluation.
- **F12 (Script File Execution)**: Loading from filesystem, syntax compilation, execution of valid scripts, return code checking.
- **F13 (GUI Script Bindings)**: `gui.clear()`, `gui.drawPixel()`, `gui.drawRect()`, `gui.fillRect()`, `gui.drawText()`, 16-bit 565 color mapping.
- **F14 (System & Input Script Bindings)**: `input.getTouch()`, `sys.delay()`, `sys.getHeap()`, `sys.setLED()`, `sys.setBacklight()`.
- **F15 (Script Sandboxing & WDT Safety)**: `lua_sethook` instruction counting (every 1000 instructions), WDT feed, execution step limits, non-blocking FreeRTOS yield.
- **F16 (Zero-Crash Error Trapping)**: `lua_pcall` protected mode, syntax error catching, runtime exception handling, modal error dialog, safe recovery.
- **F17 (Bundled Applications)**: Calculator (`calc.lua`), Paint (`paint.lua`), SU7 3D Demo (`SU7App`), System Info (`sysinfo.lua`).
- **F18 (Settings Interface)**: Settings menu view, brightness control, sleep timeout slider, theme selector, sound toggle.
- **F19 (Live Hardware Control)**: GPIO 21 PWM backlight (0..100%), active-low RGB LED (GPIO 4, 16, 17), buzzer tone generator.
- **F20 (NVS Settings Persistence)**: NVS namespace `"cyd_os"`, key storage (`brightness`, `timeout`, `theme`, `sound`), reboot reload, integrity validation.
- **F21 (About System Page)**: OS version string, ESP32 chip revision, CPU clock frequency, flash size, real-time free heap.
- **F22 (Hardware Abstraction Layer)**: ST7789 display controller, XPT2046 touch on dedicated SPI (VSPI), RGB LED, LDR photoresistor.
- **F23 (Storage HAL & Provisioning)**: LittleFS auto-mount, SD fallback, default app PROGMEM auto-provisioning when storage is empty.
- **F24 (Multi-Target Clean Build)**: PlatformIO environments: `cyd2usb`, `cyd2usb_ili9341`, `cyd` clean compilation verification.

### Tier 2: Boundary & Corner Cases (>= 120 tests; >= 5 per feature)
Evaluates extreme values, off-screen coordinates, edge conditions, resource depletion, and malformed inputs:
- Zero, negative, and out-of-bounds screen coordinates (clamping to 0..320, 0..240).
- Extreme brightness settings (<10 clamped to 10; >100 clamped to 100).
- Corrupted NVS entries and unknown keys falling back to safe defaults.
- Zero-byte script files, unclosed strings, invalid Lua syntax, infinite loops.
- File system out-of-space conditions, read-only permissions, non-existent directories.
- Empty app registry handling (graceful empty launcher view).
- Multi-touch and rapid press-release bouncing events.
- Extreme heap depletion scenarios and low-memory warnings.

### Tier 3: Cross-Feature Interactions (>= 15 tests)
Evaluates subsystem integration and pairwise interactions:
- Touch input dispatch while a Lua script executes intensive graphics rendering.
- Modifying brightness slider in Settings immediately updating PWM while UI redraws.
- LittleFS write operations during background audio tone generation.
- Dynamic theme switching affecting active app viewport without redraw artifacts.
- Navigation Bar Home button interrupting an active script running an infinite loop.
- Unmounting SD card while an SD-based app is running, triggering safe fallback.
- Rapid successive app launching and exiting verifying heap stability (zero leaks).

### Tier 4: Real-World Scenarios (>= 8 comprehensive flows)
Simulates end-to-end user journeys:
1. **Cold Boot to Home**: Power-on -> BootView logo -> 0-100% progress -> Status Bar sync -> Home Launcher grid ready.
2. **App Launch and Back Navigation**: Launcher touch -> App load -> onResume -> Nav Bar `<` touch -> onPause -> Launcher restored.
3. **Settings Reconfiguration & NVS Persist**: Launch Settings -> Adjust Backlight to 85% -> Toggle Theme to Light -> Exit -> Reboot simulation -> Config restored.
4. **Dynamic Lua App Execution**: File discovery -> Launch `calc.lua` -> Send touch events to buttons -> Display calculation result -> Exit.
5. **Script Fault Injection & Self-Healing**: Launch corrupt script -> Syntax error caught by `lua_pcall` -> Android modal error dialog shown -> Tap "Dismiss" -> Return safely to Home.
6. **Hardware Telemetry Monitoring**: Launch `sysinfo.lua` / About Page -> Verify CPU freq (240MHz), Flash (4MB), Free Heap (>180KB), Storage (LittleFS).
7. **SU7 3D Wireframe Demo Execution**: Launch SU7 native app -> 3D rotation projection -> Turntable ground ring rendering -> Nav Bar Home exit.
8. **Storage Auto-Provisioning on Fresh Flash**: Empty LittleFS simulation -> System detects missing `/apps/` -> Auto-provisions bundled scripts from PROGMEM -> Launcher populated with all apps.

---

## 3. High-Fidelity Simulation & Test Harness Architecture

The test harness is implemented in pure Python under `test/cyd_simulator/` without requiring external dependencies beyond the standard library. It precisely mirrors the hardware registers, memory partitions, and C++ class contracts defined in `PROJECT.md`:

```
test/
├── run_e2e_tests.py                 # Unified Test Runner & CLI
├── cyd_simulator/
│   ├── display_hal.py              # ST7789 320x240 display & double-buffer sprites
│   ├── touch_hal.py                # XPT2046 resistive digitizer & SPI calibration
│   ├── board_hal.py                # GPIO 21 PWM backlight, RGB LED, LDR, Tone
│   ├── storage_hal.py              # LittleFS & SD storage simulator with PROGMEM fallbacks
│   ├── config_store.py             # NVS Preferences ("cyd_os") simulation
│   ├── app_framework.py            # App base class, AppRegistry, lifecycle supervisor
│   ├── script_engine.py            # Sandboxed Lua VM simulator with WDT hooks & pcall
│   └── ui_shell.py                 # Top StatusBar, Viewport, Bottom NavBar, Launcher
├── test_tier1_features/            # T1: 24 Feature Test Modules (F01..F24)
├── test_tier2_boundaries/          # T2: 24 Boundary Test Modules (B01..B24)
├── test_tier3_interactions/        # T3: Cross-Feature Interaction Modules
└── test_tier4_scenarios/           # T4: Real-World Scenario Modules
```

---

## 4. Test Runner CLI & Execution

### Execution Commands
```bash
# Run the entire E2E test suite (all 4 Tiers, 260+ tests)
python test/run_e2e_tests.py

# Run specific tiers
python test/run_e2e_tests.py --tier 1
python test/run_e2e_tests.py --tier 2
python test/run_e2e_tests.py --tier 3
python test/run_e2e_tests.py --tier 4

# Run specific feature tests across tiers
python test/run_e2e_tests.py --feature F01
python test/run_e2e_tests.py --feature F16

# Generate machine-readable JSON report
python test/run_e2e_tests.py --json-report test_report.json

# Run via standard Python unittest discovery
python -m unittest discover -s test
```

### Exit Codes
- `0`: All tests passed cleanly (100% success).
- `1`: One or more test failures or assertion errors.
- `2`: Invalid CLI invocation or missing test environment.
