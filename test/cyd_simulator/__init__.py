"""
CYD Simulator Package - High-fidelity host simulation harness for CYD Android OS
"""
from .display_hal import DisplayHAL, COLOR_BLACK, COLOR_WHITE, COLOR_RED, COLOR_GREEN, COLOR_BLUE
from .touch_hal import TouchHAL
from .board_hal import BoardHAL
from .storage_hal import StorageHAL
from .config_store import ConfigStore, SystemConfig
from .app_framework import App, AppRegistry
from .script_engine import ScriptEngine
from .ui_shell import UIManager, BootView, StatusBar, NavBar, LauncherView

__all__ = [
    'DisplayHAL', 'COLOR_BLACK', 'COLOR_WHITE', 'COLOR_RED', 'COLOR_GREEN', 'COLOR_BLUE',
    'TouchHAL', 'BoardHAL', 'StorageHAL',
    'ConfigStore', 'SystemConfig',
    'App', 'AppRegistry',
    'ScriptEngine',
    'UIManager', 'BootView', 'StatusBar', 'NavBar', 'LauncherView'
]
