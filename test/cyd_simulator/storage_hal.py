"""
Storage HAL Simulator for LittleFS internal flash and MicroSD storage
"""
from typing import Dict, List, Optional

DEFAULT_LITTLEFS_TOTAL = 1974272  # ~1.88 MB partition

DEFAULT_PROGMEM_APPS = {
    "/apps/calc.lua": """-- CYD Calculator
gui.clear(0x0000)
gui.drawText("Calculator", 10, 10, 0xFFFF)
gui.drawRect(20, 50, 40, 40, 0xFFE0)
gui.drawText("7", 35, 60, 0xFFFF)
""",
    "/apps/paint.lua": """-- CYD Touch Paint
gui.clear(0x0000)
gui.drawText("Touch to Paint", 10, 10, 0x07E0)
local touched, x, y = input.getTouch()
if touched then
    gui.fillRect(x-2, y-2, 5, 5, 0xF800)
end
""",
    "/apps/sysinfo.lua": """-- CYD System Info
gui.clear(0x10A2)
gui.drawText("CYD OS Telemetry", 10, 10, 0xFFE0)
local heap = sys.getHeap()
gui.drawText("Heap: " .. tostring(heap), 10, 40, 0xFFFF)
"""
}


class StorageHAL:
    def __init__(self, total_bytes: int = DEFAULT_LITTLEFS_TOTAL):
        self.total_bytes = total_bytes
        self.files: Dict[str, str] = {}
        self.sd_mounted = False
        self.sd_files: Dict[str, str] = {}
        self.initialized = False
        self.auto_provision_done = False

    def init_storage(self) -> bool:
        self.initialized = True
        self.provision_default_apps()
        return True

    def provision_default_apps(self):
        # Auto-provision bundled apps if missing
        for path, content in DEFAULT_PROGMEM_APPS.items():
            if path not in self.files:
                self.files[path] = content
        self.auto_provision_done = True

    def is_sd_card_mounted(self) -> bool:
        return self.sd_mounted

    def mount_sd_card(self, mounted: bool = True):
        self.sd_mounted = mounted

    def get_storage_total_bytes(self) -> int:
        return self.total_bytes

    def get_storage_used_bytes(self) -> int:
        return sum(len(content.encode("utf-8")) for content in self.files.values())

    def file_exists(self, filepath: str) -> bool:
        if filepath.startswith("/sd/"):
            return self.sd_mounted and (filepath in self.sd_files)
        return filepath in self.files

    def read_file(self, filepath: str) -> Optional[str]:
        if filepath.startswith("/sd/"):
            if not self.sd_mounted:
                return None
            return self.sd_files.get(filepath)
        return self.files.get(filepath)

    def write_file(self, filepath: str, content: str) -> bool:
        content_size = len(content.encode("utf-8"))
        if filepath.startswith("/sd/"):
            if not self.sd_mounted:
                return False
            self.sd_files[filepath] = content
            return True

        current_size = len(self.files.get(filepath, "").encode("utf-8"))
        if self.get_storage_used_bytes() - current_size + content_size > self.total_bytes:
            return False  # Storage full
        self.files[filepath] = content
        return True

    def delete_file(self, filepath: str) -> bool:
        if filepath.startswith("/sd/"):
            if self.sd_mounted and filepath in self.sd_files:
                del self.sd_files[filepath]
                return True
            return False
        if filepath in self.files:
            del self.files[filepath]
            return True
        return False

    def list_dir(self, dir_path: str) -> List[str]:
        if not dir_path.endswith("/"):
            dir_path += "/"
        results = set()
        source = self.sd_files if dir_path.startswith("/sd/") else self.files
        for path in source:
            if path.startswith(dir_path):
                relative = path[len(dir_path):]
                parts = relative.split("/")
                if parts[0]:
                    results.add(parts[0])
        return sorted(list(results))
