"""
UI Shell Simulator: BootView, StatusBar, NavBar, LauncherView, and UIManager
"""
from typing import Optional, List, Dict
from .display_hal import DisplayHAL, SCREEN_WIDTH, SCREEN_HEIGHT, STATUS_BAR_HEIGHT, NAV_BAR_HEIGHT, VIEWPORT_HEIGHT
from .touch_hal import TouchHAL
from .app_framework import AppRegistry, App


class BootView:
    def __init__(self, display: DisplayHAL):
        self.display = display
        self.progress = 0
        self.status_text = "Initializing hardware..."
        self.is_complete = False

    def render(self):
        self.display.fill_screen(0x0000)
        # Boot Logo (Android green silhouette / text)
        self.display.draw_string("ANDROID OS", 100, 60, font=4, color=0x07E0)
        self.display.draw_string("ESP32 CYD Edition", 95, 95, font=2, color=0xFFFF)

        # Progress bar frame (X=40, Y=140, W=240, H=14)
        self.display.draw_rect(40, 140, 240, 14, 0x7BEF)
        fill_w = int(236 * (self.progress / 100.0))
        if fill_w > 0:
            self.display.fill_rect(42, 142, fill_w, 10, 0x07E0)
        self.display.draw_string(self.status_text, 40, 165, font=2, color=0x7BEF)

    def set_progress(self, percent: int, text: str = ""):
        self.progress = max(0, min(100, percent))
        if text:
            self.status_text = text
        if self.progress >= 100:
            self.is_complete = True
        self.render()


class StatusBar:
    def __init__(self, display: DisplayHAL):
        self.display = display
        self.clock_text = "12:00"
        self.heap_free_kb = 192
        self.sd_mounted = False

    def update(self, clock: str, heap_kb: int, sd_mounted: bool):
        self.clock_text = clock
        self.heap_free_kb = heap_kb
        self.sd_mounted = sd_mounted

    def render(self):
        # 20px status bar: Y=0..19
        self.display.fill_rect(0, 0, SCREEN_WIDTH, STATUS_BAR_HEIGHT, 0x10A2)
        self.display.draw_rect(0, STATUS_BAR_HEIGHT - 1, SCREEN_WIDTH, 1, 0x07E0)
        # Clock at top left
        self.display.draw_string(self.clock_text, 6, 2, font=2, color=0xFFFF)
        # Heap in center
        self.display.draw_string(f"RAM: {self.heap_free_kb}K", 130, 2, font=2, color=0x07FF)
        # Storage indicator
        sd_str = "SD:OK" if self.sd_mounted else "LFS:OK"
        self.display.draw_string(sd_str, 220, 2, font=2, color=0xFFE0)
        # Battery icon indicator
        self.display.draw_string("100%", 280, 2, font=2, color=0x07E0)


class NavBar:
    def __init__(self, display: DisplayHAL):
        self.display = display
        self.height = NAV_BAR_HEIGHT
        self.y = SCREEN_HEIGHT - NAV_BAR_HEIGHT  # 214
        self.back_hitbox = (0, 214, 106, 239)
        self.home_hitbox = (107, 214, 213, 239)
        self.tasks_hitbox = (214, 214, 320, 239)

    def render(self):
        # 26px nav bar: Y=214..239
        self.display.fill_rect(0, self.y, SCREEN_WIDTH, self.height, 0x10A2)
        self.display.draw_rect(0, self.y, SCREEN_WIDTH, 1, 0x7BEF)
        # 3 navigation icons
        self.display.draw_string("<", 48, self.y + 4, font=4, color=0xFFFF)
        self.display.draw_string("O", 155, self.y + 4, font=4, color=0xFFFF)
        self.display.draw_string("[]", 260, self.y + 4, font=4, color=0xFFFF)

    def handle_touch(self, x: int, y: int) -> Optional[str]:
        if y < self.y:
            return None
        if x <= 106:
            return "BACK"
        elif x <= 213:
            return "HOME"
        else:
            return "TASKS"


class LauncherView:
    def __init__(self, display: DisplayHAL, registry: AppRegistry):
        self.display = display
        self.registry = registry
        # 2x4 grid layout coordinates
        # Row 0: Y=35..105, Row 1: Y=125..195
        self.grid_slots = [
            (15, 30, 75, 100),   (90, 30, 150, 100),  (165, 30, 225, 100), (240, 30, 300, 100),
            (15, 120, 75, 190),  (90, 120, 150, 190), (165, 120, 225, 190), (240, 120, 300, 190)
        ]

    def render(self):
        # Viewport background
        self.display.fill_rect(0, STATUS_BAR_HEIGHT, SCREEN_WIDTH, VIEWPORT_HEIGHT, 0x0821)
        count = self.registry.getAppCount()
        for idx in range(min(count, 8)):
            x1, y1, x2, y2 = self.grid_slots[idx]
            app = self.registry.getApp(idx)
            # Icon box
            self.display.draw_rect(x1 + 8, y1, 44, 44, 0x07E0)
            self.display.fill_rect(x1 + 10, y1 + 2, 40, 40, 0x18C3)
            # Label
            name = app.getName()[:8]
            self.display.draw_string(name, x1, y1 + 48, font=2, color=0xFFFF)

    def handle_touch(self, x: int, y: int) -> Optional[int]:
        # Check which app icon was tapped
        for idx, (x1, y1, x2, y2) in enumerate(self.grid_slots):
            if x1 <= x <= x2 and y1 <= y <= y2:
                if idx < self.registry.getAppCount():
                    return idx
        return None


class UIManager:
    def __init__(self, display: DisplayHAL, touch: TouchHAL, registry: AppRegistry):
        self.display = display
        self.touch = touch
        self.registry = registry
        self.boot_view = BootView(display)
        self.status_bar = StatusBar(display)
        self.nav_bar = NavBar(display)
        self.launcher_view = LauncherView(display, registry)

        self.current_state = "BOOT"  # "BOOT", "LAUNCHER", "APP", "ERROR_MODAL"
        self.error_message = ""
        self.last_nav_action = None

    def start_os(self):
        self.current_state = "BOOT"
        self.boot_view.set_progress(10, "Booting CYD Android OS...")

    def transition_to_home(self):
        self.current_state = "LAUNCHER"
        self.display.fill_screen(0x0000)
        self.status_bar.render()
        self.launcher_view.render()
        self.nav_bar.render()

    def show_error_modal(self, error_msg: str):
        self.current_state = "ERROR_MODAL"
        self.error_message = error_msg
        # Render modal popup dialog (centered)
        self.display.fill_rect(30, 60, 260, 120, 0xF800)  # Red banner
        self.display.draw_rect(30, 60, 260, 120, 0xFFFF)
        self.display.draw_string("Application Error", 40, 70, font=2, color=0xFFFF)
        err_snippet = (error_msg[:24] + "...") if len(error_msg) > 24 else error_msg
        self.display.draw_string(err_snippet, 40, 95, font=2, color=0xFFFF)
        # Dismiss button (X=90..230, Y=140..170)
        self.display.fill_rect(100, 135, 120, 30, 0x0000)
        self.display.draw_rect(100, 135, 120, 30, 0xFFFF)
        self.display.draw_string("DISMISS", 130, 142, font=2, color=0xFFFF)

    def dismiss_error_modal(self):
        self.transition_to_home()

    def process_touch(self, x: int, y: int, pressed: bool = True):
        if not pressed:
            return

        # 1. Navigation Bar has supervisor priority
        if y >= self.nav_bar.y:
            action = self.nav_bar.handle_touch(x, y)
            self.last_nav_action = action
            if action in ("HOME", "BACK"):
                if self.current_state == "APP":
                    self.registry.exitCurrentApp()
                    self.transition_to_home()
                elif self.current_state == "ERROR_MODAL":
                    self.dismiss_error_modal()
            return

        # 2. Modal Error Dialog handling
        if self.current_state == "ERROR_MODAL":
            if 100 <= x <= 220 and 135 <= y <= 165:
                self.dismiss_error_modal()
            return

        # 3. Launcher handling
        if self.current_state == "LAUNCHER":
            app_idx = self.launcher_view.handle_touch(x, y)
            if app_idx is not None:
                self.registry.launchApp(app_idx)
                self.current_state = "APP"
                self.render_active_app()
            return

        # 4. Active App touch forwarding
        if self.current_state == "APP":
            active_app = self.registry.getActiveApp()
            if active_app:
                active_app.onTouch(x, y, pressed)

    def render_active_app(self):
        active_app = self.registry.getActiveApp()
        if active_app:
            self.status_bar.render()
            active_app.onRender(self.display, 0, STATUS_BAR_HEIGHT, SCREEN_WIDTH, VIEWPORT_HEIGHT)
            self.nav_bar.render()
