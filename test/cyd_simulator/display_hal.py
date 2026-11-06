"""
Display HAL Simulator for ST7789 on CYD (320x240 LCD, HSPI @ 55MHz)
"""

COLOR_BLACK = 0x0000
COLOR_WHITE = 0xFFFF
COLOR_RED = 0xF800
COLOR_GREEN = 0x07E0
COLOR_BLUE = 0x001F
COLOR_YELLOW = 0xFFE0
COLOR_CYAN = 0x07FF
COLOR_MAGENTA = 0xF81F
COLOR_GRAY = 0x7BEF
COLOR_DARK_GRAY = 0x18C3

SCREEN_WIDTH = 320
SCREEN_HEIGHT = 240

STATUS_BAR_Y = 0
STATUS_BAR_HEIGHT = 20

VIEWPORT_Y = 20
VIEWPORT_HEIGHT = 194

NAV_BAR_Y = 214
NAV_BAR_HEIGHT = 26


class SpriteSimulator:
    def __init__(self, width: int, height: int, depth: int = 16):
        self.width = width
        self.height = height
        self.depth = depth
        self.buffer = [[COLOR_BLACK for _ in range(width)] for _ in range(height)]
        self.text_records = []

    def fill_sprite(self, color: int):
        for y in range(self.height):
            for x in range(self.width):
                self.buffer[y][x] = color

    def draw_pixel(self, x: int, y: int, color: int):
        if 0 <= x < self.width and 0 <= y < self.height:
            self.buffer[y][x] = color

    def draw_rect(self, x: int, y: int, w: int, h: int, color: int):
        for i in range(x, min(x + w, self.width)):
            if 0 <= y < self.height:
                self.buffer[y][i] = color
            if 0 <= y + h - 1 < self.height:
                self.buffer[y + h - 1][i] = color
        for j in range(y, min(y + h, self.height)):
            if 0 <= x < self.width:
                self.buffer[j][x] = color
            if 0 <= x + w - 1 < self.width:
                self.buffer[j][x + w - 1] = color

    def fill_rect(self, x: int, y: int, w: int, h: int, color: int):
        for j in range(max(0, y), min(y + h, self.height)):
            for i in range(max(0, x), min(x + w, self.width)):
                self.buffer[j][i] = color

    def draw_string(self, text: str, x: int, y: int, color: int = COLOR_WHITE):
        self.text_records.append({"text": text, "x": x, "y": y, "color": color})


class DisplayHAL:
    def __init__(self):
        self.width = SCREEN_WIDTH
        self.height = SCREEN_HEIGHT
        self.rotation = 1  # Landscape
        self.driver = "ST7789"
        self.rgb_order = "BGR"
        self.inversion = False
        self.initialized = False
        self.framebuffer = [[COLOR_BLACK for _ in range(SCREEN_WIDTH)] for _ in range(SCREEN_HEIGHT)]
        self.draw_history = []
        self.text_history = []

    def init_display(self):
        self.initialized = True
        self.fill_screen(COLOR_BLACK)

    def set_rotation(self, r: int):
        self.rotation = r
        if r in (1, 3):
            self.width = 320
            self.height = 240
        else:
            self.width = 240
            self.height = 320

    def fill_screen(self, color: int):
        for y in range(self.height):
            for x in range(self.width):
                self.framebuffer[y][x] = color
        self.draw_history.append({"op": "fillScreen", "color": color})

    def draw_pixel(self, x: int, y: int, color: int):
        if 0 <= x < self.width and 0 <= y < self.height:
            self.framebuffer[y][x] = color
            self.draw_history.append({"op": "drawPixel", "x": x, "y": y, "color": color})

    def draw_rect(self, x: int, y: int, w: int, h: int, color: int):
        for i in range(x, min(x + w, self.width)):
            if 0 <= y < self.height:
                self.framebuffer[y][i] = color
            if 0 <= y + h - 1 < self.height:
                self.framebuffer[y + h - 1][i] = color
        for j in range(y, min(y + h, self.height)):
            if 0 <= x < self.width:
                self.framebuffer[j][x] = color
            if 0 <= x + w - 1 < self.width:
                self.framebuffer[j][x + w - 1] = color
        self.draw_history.append({"op": "drawRect", "x": x, "y": y, "w": w, "h": h, "color": color})

    def fill_rect(self, x: int, y: int, w: int, h: int, color: int):
        for j in range(max(0, y), min(y + h, self.height)):
            for i in range(max(0, x), min(x + w, self.width)):
                self.framebuffer[j][i] = color
        self.draw_history.append({"op": "fillRect", "x": x, "y": y, "w": w, "h": h, "color": color})

    def draw_string(self, text: str, x: int, y: int, font: int = 2, color: int = COLOR_WHITE, bg_color: int = COLOR_BLACK):
        self.text_history.append({"text": text, "x": x, "y": y, "font": font, "color": color, "bg": bg_color})
        self.draw_history.append({"op": "drawString", "text": text, "x": x, "y": y, "color": color})

    def create_sprite(self, w: int, h: int) -> SpriteSimulator:
        return SpriteSimulator(w, h)

    def push_sprite(self, sprite: SpriteSimulator, dest_x: int, dest_y: int):
        for j in range(sprite.height):
            target_y = dest_y + j
            if 0 <= target_y < self.height:
                for i in range(sprite.width):
                    target_x = dest_x + i
                    if 0 <= target_x < self.width:
                        self.framebuffer[target_y][target_x] = sprite.buffer[j][i]
        for t in sprite.text_records:
            self.text_history.append({
                "text": t["text"],
                "x": dest_x + t["x"],
                "y": dest_y + t["y"],
                "font": 2,
                "color": t["color"],
                "bg": COLOR_BLACK
            })
        self.draw_history.append({"op": "pushSprite", "x": dest_x, "y": dest_y, "w": sprite.width, "h": sprite.height})

    def get_pixel(self, x: int, y: int) -> int:
        if 0 <= x < self.width and 0 <= y < self.height:
            return self.framebuffer[y][x]
        return 0

    def has_text(self, text_substr: str) -> bool:
        return any(text_substr in item["text"] for item in self.text_history)
