"""
Automated Verification Suite for Xiaomi SU7 Ultra 3D Telemetry on CYD 2 USB-C
"""
import os
import re
import math
import subprocess
import sys

def test_platformio_config():
    print("[TEST 1/5] Verifying platformio.ini pinouts and configuration...")
    with open("platformio.ini", "r") as f:
        ini = f.read()

    assert "default_envs = cyd2usb" in ini, "Default env must be cyd2usb"
    assert "-DST7789_DRIVER=1" in ini, "Must use ST7789 driver"
    assert "-DTFT_RGB_ORDER=TFT_BGR" in ini, "Color order must be TFT_BGR"
    assert "-DTFT_INVERSION_OFF=1" in ini, "Inversion must be OFF for ST7789"
    assert "-DTFT_BL=21" in ini, "Backlight must be GPIO 21"
    assert "-DTFT_MOSI=13" in ini, "TFT_MOSI must be GPIO 13"
    assert "-DTFT_MISO=12" in ini, "TFT_MISO must be GPIO 12"
    assert "-DTFT_SCLK=14" in ini, "TFT_SCLK must be GPIO 14"
    assert "-DTFT_CS=15" in ini, "TFT_CS must be GPIO 15"
    assert "-DTFT_DC=2" in ini, "TFT_DC must be GPIO 2"
    print("  -> PASSED: All hardware pins and ST7789 display controller configs verified.")

def test_3d_model_data():
    print("[TEST 2/5] Verifying 3D model geometry and wireframe topology...")
    with open("src/su7_data.h", "r") as f:
        data = f.read()

    v_match = re.search(r"SU7_VERTICES\[\]\s*=\s*\{([^;]+)\};", data, re.DOTALL)
    assert v_match, "SU7_VERTICES array not found"
    verts = re.findall(r"\{\s*([-\d\.]+)f?\s*,\s*([-\d\.]+)f?\s*,\s*([-\d\.]+)f?\s*\}", v_match.group(1))
    
    e_match = re.search(r"SU7_EDGES\[\]\s*=\s*\{([^;]+)\};", data, re.DOTALL)
    assert e_match, "SU7_EDGES array not found"
    edges = re.findall(r"\{\s*(\d+)\s*,\s*(\d+)\s*,", e_match.group(1))

    print(f"  -> Model has {len(verts)} vertices and {len(edges)} edges.")
    assert len(verts) >= 70, f"Expected at least 70 vertices for SU7 Ultra, got {len(verts)}"
    assert len(edges) >= 100, f"Expected at least 100 edges for SU7 Ultra, got {len(edges)}"

    # Check index bounds
    max_idx = len(verts) - 1
    seen_edges = set()
    for idx, (v0_s, v1_s) in enumerate(edges):
        v0, v1 = int(v0_s), int(v1_s)
        assert 0 <= v0 <= max_idx, f"Edge {idx} v0={v0} out of range [0, {max_idx}]"
        assert 0 <= v1 <= max_idx, f"Edge {idx} v1={v1} out of range [0, {max_idx}]"
        assert v0 != v1, f"Edge {idx} has identical start and end vertices ({v0}, {v1})"
        pair = tuple(sorted([v0, v1]))
        assert pair not in seen_edges, f"Duplicate edge found: {pair} at index {idx}"
        seen_edges.add(pair)

    # Check iconic features
    assert "LiDAR" in data, "LiDAR pod missing from model"
    assert "Rear Wing" in data or "GT" in data, "GT rear wing missing"
    assert "Taillight" in data, "Continuous halo taillight missing"
    assert "Splitter" in data, "Front splitter missing"
    assert "COLOR_ULTRA_YELLOW" in data, "Ultra yellow color missing"
    print("  -> PASSED: SU7 Ultra geometry, topology, and iconic features verified.")

def test_projection_bounds():
    print("[TEST 3/5] Verifying 3D rotation and perspective projection bounding box...")
    with open("src/su7_data.h", "r") as f:
        data = f.read()

    v_match = re.search(r"SU7_VERTICES\[\]\s*=\s*\{([^;]+)\};", data, re.DOTALL)
    verts = []
    for m in re.finditer(r"\{\s*([-\d\.]+)f?\s*,\s*([-\d\.]+)f?\s*,\s*([-\d\.]+)f?\s*\}", v_match.group(1)):
        verts.append((float(m.group(1)), float(m.group(2)), float(m.group(3))))

    CAMERA_DIST = 6.2
    FOCAL_LEN = 290.0
    PITCH_RAD = 0.30
    cosPitch = math.cos(PITCH_RAD)
    sinPitch = math.sin(PITCH_RAD)

    VIEWPORT_W = 320
    VIEWPORT_H = 172
    cx = VIEWPORT_W * 0.5
    cy = VIEWPORT_H * 0.5 - 4.0

    min_x, max_x = float('inf'), float('-inf')
    min_y, max_y = float('inf'), float('-inf')

    # Sweep full 360 degree rotation in 1-degree increments
    for deg in range(360):
        yaw = math.radians(deg)
        cosYaw = math.cos(yaw)
        sinYaw = math.sin(yaw)
        for vx, vy, vz in verts:
            xRot = vx * cosYaw - vy * sinYaw
            yRot = vx * sinYaw + vy * cosYaw
            zRot = vz
            xCam = xRot
            yCam = yRot * cosPitch - zRot * sinPitch
            zCam = yRot * sinPitch + zRot * cosPitch
            zDepth = CAMERA_DIST + yCam
            assert zDepth > 0.5, f"Near clip plane breached: zDepth={zDepth}"
            invZ = FOCAL_LEN / zDepth
            px = cx + xCam * invZ
            py = cy - zCam * invZ
            min_x = min(min_x, px)
            max_x = max(max_x, px)
            min_y = min(min_y, py)
            max_y = max(max_y, py)

    print(f"  -> Projected X range across 360 deg: [{min_x:.1f}, {max_x:.1f}] (Viewport width: {VIEWPORT_W})")
    print(f"  -> Projected Y range across 360 deg: [{min_y:.1f}, {max_y:.1f}] (Viewport height: {VIEWPORT_H})")
    assert min_x >= 10.0 and max_x <= 310.0, f"X bounds [{min_x}, {max_x}] exceeded safe viewport margins"
    assert min_y >= 10.0 and max_y <= 168.0, f"Y bounds [{min_y}, {max_y}] exceeded safe viewport margins"
    print("  -> PASSED: All projected vertices remain comfortably within 320x172 viewport with no clipping.")

def test_telemetry_and_flicker_free_rendering():
    print("[TEST 4/5] Verifying telemetry overlay and double-buffering architecture...")
    with open("src/main.cpp", "r") as f:
        main_cpp = f.read()

    assert "TFT_eSprite sprite" in main_cpp, "Main 3D viewport sprite double-buffer missing"
    assert "TFT_eSprite hudSprite" in main_cpp, "HUD telemetry sprite double-buffer missing"
    assert "sprite.createSprite" in main_cpp, "Sprite allocation missing"
    assert "sprite.pushSprite" in main_cpp, "Offscreen sprite push missing"
    assert "hudSprite.pushSprite" in main_cpp, "HUD sprite push missing"
    assert "ROT_SPEED" in main_cpp, "Rotation speed constant missing"
    assert "yawAngle" in main_cpp, "Yaw angle variable missing"
    assert "currentFps" in main_cpp, "Live FPS calculation missing"
    assert "snprintf" in main_cpp and "YAW:" in main_cpp and "FPS" in main_cpp, "Telemetry format missing"
    assert "updateDynamicHUD" in main_cpp, "Dynamic HUD updater missing"
    print("  -> PASSED: Double-buffered RAM sprites and dynamic telemetry overlay verified.")

def test_platformio_clean_build():
    print("[TEST 5/5] Compiling project cleanly with PlatformIO (pio run)...")
    res = subprocess.run(["pio", "run"], shell=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    if res.returncode != 0:
        print(res.stdout)
        print(res.stderr, file=sys.stderr)
        raise RuntimeError(f"PlatformIO build failed with returncode {res.returncode}")
    
    assert os.path.exists(".pio/build/cyd2usb/firmware.bin"), "firmware.bin not generated"
    assert os.path.exists(".pio/build/cyd2usb/firmware.elf"), "firmware.elf not generated"
    bin_size = os.path.getsize(".pio/build/cyd2usb/firmware.bin")
    print(f"  -> Generated firmware.bin size: {bin_size} bytes ({bin_size / 1024:.1f} KB).")
    print("  -> PASSED: Clean compilation with 0 errors and valid binary output.")

if __name__ == "__main__":
    print("==================================================================")
    print(" Running Full Verification Suite for Xiaomi SU7 Ultra 3D Display  ")
    print("==================================================================")
    try:
        test_platformio_config()
        test_3d_model_data()
        test_projection_bounds()
        test_telemetry_and_flicker_free_rendering()
        test_platformio_clean_build()
        print("\n==================================================================")
        print(" ALL VERIFICATION TESTS PASSED SUCCESSFULLY! (5/5)               ")
        print("==================================================================")
    except AssertionError as e:
        print(f"\n[FAIL] {e}", file=sys.stderr)
        sys.exit(1)
    except Exception as e:
        print(f"\n[ERROR] {e}", file=sys.stderr)
        sys.exit(1)
