"""
App Framework and Registry Simulator
"""
from abc import ABC, abstractmethod
from typing import List, Optional
from .display_hal import DisplayHAL
from .storage_hal import StorageHAL


class App(ABC):
    def __init__(self, name: str, icon: str):
        self._name = name
        self._icon = icon
        self.state = "UNINITIALIZED"
        self.lifecycle_log = []

    def getName(self) -> str:
        return self._name

    def getIcon(self) -> str:
        return self._icon

    def onCreate(self):
        self.state = "CREATED"
        self.lifecycle_log.append("onCreate")

    def onResume(self):
        self.state = "RUNNING"
        self.lifecycle_log.append("onResume")

    def onUpdate(self):
        self.lifecycle_log.append("onUpdate")

    @abstractmethod
    def onRender(self, display: DisplayHAL, viewportX: int, viewportY: int, viewportW: int, viewportH: int):
        pass

    def onTouch(self, x: int, y: int, pressed: bool):
        self.lifecycle_log.append(f"onTouch({x},{y},{pressed})")

    def onPause(self):
        self.state = "PAUSED"
        self.lifecycle_log.append("onPause")

    def onDestroy(self):
        self.state = "DESTROYED"
        self.lifecycle_log.append("onDestroy")


class SettingsApp(App):
    def __init__(self):
        super().__init__("Settings", "ICON_SETTINGS")

    def onRender(self, display: DisplayHAL, viewportX: int, viewportY: int, viewportW: int, viewportH: int):
        display.fill_rect(viewportX, viewportY, viewportW, viewportH, 0x10A2)
        display.draw_string("Device Settings", viewportX + 10, viewportY + 10, font=2, color=0xFFFF)
        display.draw_string("Brightness: 80%", viewportX + 10, viewportY + 40, font=2, color=0xFFE0)


class AboutApp(App):
    def __init__(self):
        super().__init__("About", "ICON_ABOUT")

    def onRender(self, display: DisplayHAL, viewportX: int, viewportY: int, viewportW: int, viewportH: int):
        display.fill_rect(viewportX, viewportY, viewportW, viewportH, 0x0821)
        display.draw_string("CYD Android OS v1.0", viewportX + 10, viewportY + 10, font=2, color=0x07FF)
        display.draw_string("ESP32-D0WDQ6-V3 240MHz", viewportX + 10, viewportY + 35, font=2, color=0xFFFF)
        display.draw_string("Flash: 4MB | RAM: 320KB", viewportX + 10, viewportY + 60, font=2, color=0xFFFF)


class SU7App(App):
    def __init__(self):
        super().__init__("SU7 Ultra", "ICON_SU7")

    def onRender(self, display: DisplayHAL, viewportX: int, viewportY: int, viewportW: int, viewportH: int):
        display.fill_rect(viewportX, viewportY, viewportW, viewportH, 0x0000)
        display.draw_string("Xiaomi SU7 Ultra 3D", viewportX + 10, viewportY + 10, font=2, color=0xFFE0)


class DynamicScriptApp(App):
    def __init__(self, script_path: str, name: str, icon: str = "ICON_LUA"):
        super().__init__(name, icon)
        self.script_path = script_path

    def onRender(self, display: DisplayHAL, viewportX: int, viewportY: int, viewportW: int, viewportH: int):
        display.fill_rect(viewportX, viewportY, viewportW, viewportH, 0x0000)
        display.draw_string(f"Running {self.getName()}", viewportX + 10, viewportY + 10, font=2, color=0x07E0)


class AppRegistry:
    def __init__(self):
        self.apps: List[App] = []
        self.active_app_index: Optional[int] = None

    def registerApp(self, app: App):
        # Register if not already present
        if app not in self.apps:
            self.apps.append(app)

    def scanFilesystemApps(self, storage: StorageHAL):
        # Scan LittleFS /apps/
        littlefs_apps = storage.list_dir("/apps/")
        for filename in littlefs_apps:
            if filename.endswith(".lua"):
                name = filename[:-4].capitalize()
                path = f"/apps/{filename}"
                # Check if already registered
                if not any(isinstance(a, DynamicScriptApp) and a.script_path == path for a in self.apps):
                    self.registerApp(DynamicScriptApp(path, name))

        # Scan SD /sd/apps/ if mounted
        if storage.is_sd_card_mounted():
            sd_apps = storage.list_dir("/sd/apps/")
            for filename in sd_apps:
                if filename.endswith(".lua"):
                    name = filename[:-4].capitalize()
                    path = f"/sd/apps/{filename}"
                    if not any(isinstance(a, DynamicScriptApp) and a.script_path == path for a in self.apps):
                        self.registerApp(DynamicScriptApp(path, f"SD-{name}"))

    def getAppCount(self) -> int:
        return len(self.apps)

    def getApp(self, index: int) -> Optional[App]:
        if 0 <= index < len(self.apps):
            return self.apps[index]
        return None

    def getActiveApp(self) -> Optional[App]:
        if self.active_app_index is not None and 0 <= self.active_app_index < len(self.apps):
            return self.apps[self.active_app_index]
        return None

    def launchApp(self, index: int) -> bool:
        if index < 0 or index >= len(self.apps):
            return False
        # If another app is active, pause it
        if self.active_app_index is not None and self.active_app_index != index:
            prev_app = self.apps[self.active_app_index]
            prev_app.onPause()

        app = self.apps[index]
        if app.state == "UNINITIALIZED":
            app.onCreate()
        app.onResume()
        self.active_app_index = index
        return True

    def exitCurrentApp(self) -> bool:
        if self.active_app_index is None:
            return False
        app = self.apps[self.active_app_index]
        app.onPause()
        self.active_app_index = None
        return True
