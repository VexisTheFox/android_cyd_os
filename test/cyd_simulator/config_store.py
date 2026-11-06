"""
ConfigStore Simulator for NVS Preferences (namespace "cyd_os")
"""
from dataclasses import dataclass


@dataclass
class SystemConfig:
    brightness: int = 80          # 10..100%
    sleepTimeoutSec: int = 60     # 0 = off, 10..3600
    theme: int = 0                # 0 = dark, 1 = light
    soundEnabled: bool = True     # Sound effects enabled


class ConfigStore:
    def __init__(self):
        self.namespace = "cyd_os"
        self.storage = {}
        self.reset_to_defaults()

    def reset_to_defaults(self):
        self.storage = {
            "brightness": 80,
            "sleepTimeoutSec": 60,
            "theme": 0,
            "soundEnabled": True
        }

    def load_config(self) -> SystemConfig:
        cfg = SystemConfig()
        # Clamping and fallback handling
        b = self.storage.get("brightness", 80)
        try:
            cfg.brightness = max(10, min(100, int(b)))
        except (ValueError, TypeError):
            cfg.brightness = 80

        s = self.storage.get("sleepTimeoutSec", 60)
        try:
            cfg.sleepTimeoutSec = max(0, min(3600, int(s)))
        except (ValueError, TypeError):
            cfg.sleepTimeoutSec = 60

        t = self.storage.get("theme", 0)
        try:
            cfg.theme = 1 if int(t) == 1 else 0
        except (ValueError, TypeError):
            cfg.theme = 0

        snd = self.storage.get("soundEnabled", True)
        cfg.soundEnabled = bool(snd)
        return cfg

    def save_config(self, cfg: SystemConfig):
        # Validate and persist
        self.storage["brightness"] = max(10, min(100, int(cfg.brightness)))
        self.storage["sleepTimeoutSec"] = max(0, min(3600, int(cfg.sleepTimeoutSec)))
        self.storage["theme"] = 1 if cfg.theme == 1 else 0
        self.storage["soundEnabled"] = bool(cfg.soundEnabled)
