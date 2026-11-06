#pragma once
#include <Arduino.h>

// Vector 3D structure
struct Point3D {
    float x;
    float y;
    float z;
};

// 2D Projected Point structure
struct Point2D {
    int16_t x;
    int16_t y;
    float z;      // for depth sorting / intensity
    bool visible;
};

// Edge (connecting line) with color styling
struct Edge3D {
    uint8_t v0;
    uint8_t v1;
    uint16_t color;
};

// Color definitions (RGB565)
#define COLOR_ULTRA_YELLOW 0xFFE0   // Lightning Yellow (signature SU7 Ultra color)
#define COLOR_WING_YELLOW  0xFEE0   // Bright Aero Yellow
#define COLOR_NEON_CYAN    0x07FF   // HUD / Tech Cyan
#define COLOR_WHITE        0xFFFF   // Headlights / Windshield
#define COLOR_DARK_GRAY    0x39E7   // Chassis / Shadow
#define COLOR_TAILLIGHT    0xF800   // Vivid Red Halo Taillight
#define COLOR_WHEEL_GOLD   0xFEA0   // Ultra Rims Gold/Yellow
#define COLOR_CALIPER      0xFD00   // Carbon-Ceramic Yellow Caliper

// Complete 3D Model Vertices for Xiaomi SU7 Ultra
// Dimensions scaled to fit nicely in 3D viewport
static const Point3D SU7_VERTICES[] = {
    // 0-6: Front Splitter & Bumper
    { 2.55f,  0.00f, -0.18f }, // 0: Nose center tip
    { 2.48f,  0.92f, -0.28f }, // 1: Front splitter left corner
    { 2.48f, -0.92f, -0.28f }, // 2: Front splitter right corner
    { 2.53f,  0.45f, -0.28f }, // 3: Front splitter left mid
    { 2.53f, -0.45f, -0.28f }, // 4: Front splitter right mid
    { 2.38f,  0.92f, -0.05f }, // 5: Front bumper left canard
    { 2.38f, -0.92f, -0.05f }, // 6: Front bumper right canard

    // 7-10: Teardrop Headlights
    { 2.12f,  0.82f,  0.12f }, // 7: Left headlight top outer
    { 2.22f,  0.52f,  0.08f }, // 8: Left headlight inner
    { 2.12f, -0.82f,  0.12f }, // 9: Right headlight top outer
    { 2.22f, -0.52f,  0.08f }, // 10: Right headlight inner

    // 11-15: Hood & Front Fenders
    { 1.25f,  0.78f,  0.22f }, // 11: Hood base left
    { 1.25f, -0.78f,  0.22f }, // 12: Hood base right
    { 1.28f,  0.00f,  0.24f }, // 13: Hood base center
    { 1.75f,  0.38f,  0.18f }, // 14: Hood power-ridge left
    { 1.75f, -0.38f,  0.18f }, // 15: Hood power-ridge right

    // 16-20: Windshield & A-Pillars
    { 0.35f,  0.55f,  0.64f }, // 16: Windshield top left
    { 0.35f, -0.55f,  0.64f }, // 17: Windshield top right
    { 0.35f,  0.00f,  0.66f }, // 18: Windshield top center
    { 1.05f,  0.84f,  0.22f }, // 19: A-pillar base left
    { 1.05f, -0.84f,  0.22f }, // 20: A-pillar base right

    // 21-24: Signature Roof LiDAR Pod ("Pebble")
    { 0.38f,  0.00f,  0.66f }, // 21: LiDAR base front
    { 0.30f,  0.00f,  0.74f }, // 22: LiDAR top apex
    { 0.34f,  0.09f,  0.70f }, // 23: LiDAR left
    { 0.34f, -0.09f,  0.70f }, // 24: LiDAR right

    // 25-28: Coupe Roof & B-Pillars
    { -0.30f,  0.54f, 0.67f }, // 25: Roof mid left
    { -0.30f, -0.54f, 0.67f }, // 26: Roof mid right
    { -0.85f,  0.50f, 0.63f }, // 27: Roof rear / glass start left
    { -0.85f, -0.50f, 0.63f }, // 28: Roof rear / glass start right

    // 29-33: Fastback Sloping Rear Glass & C-Pillars
    { -1.80f,  0.68f, 0.32f }, // 29: Rear glass bottom left
    { -1.80f, -0.68f, 0.32f }, // 30: Rear glass bottom right
    { -1.85f,  0.00f, 0.33f }, // 31: Rear glass bottom center
    { -0.30f,  0.88f, 0.22f }, // 32: Beltline mid left
    { -0.30f, -0.88f, 0.22f }, // 33: Beltline mid right

    // 34-37: Side Skirts & Rockers
    {  1.00f,  0.92f, -0.27f }, // 34: Rocker front left
    {  1.00f, -0.92f, -0.27f }, // 35: Rocker front right
    { -1.00f,  0.92f, -0.27f }, // 36: Rocker rear left
    { -1.00f, -0.92f, -0.27f }, // 37: Rocker rear right

    // 38-42: Rear Trunk & Continuous Halo Taillight Bar
    { -2.25f,  0.72f,  0.28f }, // 38: Taillight left end
    { -2.25f, -0.72f,  0.28f }, // 39: Taillight right end
    { -2.30f,  0.00f,  0.30f }, // 40: Taillight center (curved halo)
    { -2.28f,  0.42f,  0.29f }, // 41: Taillight left center
    { -2.28f, -0.42f,  0.29f }, // 42: Taillight right center

    // 43-46: Rear Diffuser & Exhaust Tunnel
    { -2.40f,  0.82f, -0.27f }, // 43: Diffuser left bottom
    { -2.40f, -0.82f, -0.27f }, // 44: Diffuser right bottom
    { -2.44f,  0.28f, -0.27f }, // 45: Diffuser center fin L
    { -2.44f, -0.28f, -0.27f }, // 46: Diffuser center fin R

    // 47-50: Ultra GT Rear Wing Pylons (Swan-neck mounts)
    { -2.05f,  0.35f, 0.30f }, // 47: Pylon left base
    { -2.15f,  0.35f, 0.60f }, // 48: Pylon left top
    { -2.05f, -0.35f, 0.30f }, // 49: Pylon right base
    { -2.15f, -0.35f, 0.60f }, // 50: Pylon right top

    // 51-58: Ultra GT Carbon Fiber Rear Wing & Endplates
    { -2.02f,  0.98f, 0.62f }, // 51: Wing leading edge left
    { -2.02f, -0.98f, 0.62f }, // 52: Wing leading edge right
    { -2.32f,  0.98f, 0.66f }, // 53: Wing trailing edge left
    { -2.32f, -0.98f, 0.66f }, // 54: Wing trailing edge right
    { -2.17f,  0.99f, 0.74f }, // 55: Endplate top left
    { -2.17f,  0.99f, 0.52f }, // 56: Endplate bottom left
    { -2.17f, -0.99f, 0.74f }, // 57: Endplate top right
    { -2.17f, -0.99f, 0.52f }, // 58: Endplate bottom right

    // 59-66: Front-Left Wheel (Hexagonal Rim + Hub)
    {  1.55f,  0.94f, -0.10f }, // 59: Hub center FL
    {  1.78f,  0.94f, -0.10f }, // 60: Rim FL right
    {  1.67f,  0.94f,  0.11f }, // 61: Rim FL top-right
    {  1.43f,  0.94f,  0.11f }, // 62: Rim FL top-left
    {  1.32f,  0.94f, -0.10f }, // 63: Rim FL left
    {  1.43f,  0.94f, -0.31f }, // 64: Rim FL bottom-left
    {  1.67f,  0.94f, -0.31f }, // 65: Rim FL bottom-right

    // 66-72: Front-Right Wheel
    {  1.55f, -0.94f, -0.10f }, // 66: Hub center FR
    {  1.78f, -0.94f, -0.10f }, // 67: Rim FR right
    {  1.67f, -0.94f,  0.11f }, // 68: Rim FR top-right
    {  1.43f, -0.94f,  0.11f }, // 69: Rim FR top-left
    {  1.32f, -0.94f, -0.10f }, // 70: Rim FR left
    {  1.43f, -0.94f, -0.31f }, // 71: Rim FR bottom-left
    {  1.67f, -0.94f, -0.31f }, // 72: Rim FR bottom-right

    // 73-79: Rear-Left Wheel
    { -1.45f,  0.94f, -0.10f }, // 73: Hub center RL
    { -1.22f,  0.94f, -0.10f }, // 74: Rim RL right
    { -1.33f,  0.94f,  0.11f }, // 75: Rim RL top-right
    { -1.57f,  0.94f,  0.11f }, // 76: Rim RL top-left
    { -1.68f,  0.94f, -0.10f }, // 77: Rim RL left
    { -1.57f,  0.94f, -0.31f }, // 78: Rim RL bottom-left
    { -1.33f,  0.94f, -0.31f }, // 79: Rim RL bottom-right

    // 80-86: Rear-Right Wheel
    { -1.45f, -0.94f, -0.10f }, // 80: Hub center RR
    { -1.22f, -0.94f, -0.10f }, // 81: Rim RR right
    { -1.33f, -0.94f,  0.11f }, // 82: Rim RR top-right
    { -1.57f, -0.94f,  0.11f }, // 83: Rim RR top-left
    { -1.68f, -0.94f, -0.10f }, // 84: Rim RR left
    { -1.57f, -0.94f, -0.31f }, // 85: Rim RR bottom-left
    { -1.33f, -0.94f, -0.31f }  // 86: Rim RR bottom-right
};

static const uint16_t NUM_SU7_VERTICES = sizeof(SU7_VERTICES) / sizeof(SU7_VERTICES[0]);

// Connecting edges forming the 3D Wireframe of the Xiaomi SU7 Ultra
static const Edge3D SU7_EDGES[] = {
    // Front Splitter Lip (Aerodynamic Carbon fiber splitter)
    { 1, 3, COLOR_ULTRA_YELLOW },
    { 3, 0, COLOR_ULTRA_YELLOW },
    { 0, 4, COLOR_ULTRA_YELLOW },
    { 4, 2, COLOR_ULTRA_YELLOW },
    { 1, 5, COLOR_ULTRA_YELLOW },
    { 2, 6, COLOR_ULTRA_YELLOW },

    // Nose & Front Canards
    { 0, 8, COLOR_ULTRA_YELLOW },
    { 0, 10, COLOR_ULTRA_YELLOW },
    { 5, 7, COLOR_ULTRA_YELLOW },
    { 6, 9, COLOR_ULTRA_YELLOW },

    // Headlights (Teardrops)
    { 7, 8, COLOR_WHITE },
    { 9, 10, COLOR_WHITE },
    { 8, 14, COLOR_WHITE },
    { 10, 15, COLOR_WHITE },

    // Hood & Fenders
    { 7, 19, COLOR_ULTRA_YELLOW },
    { 9, 20, COLOR_ULTRA_YELLOW },
    { 14, 13, COLOR_ULTRA_YELLOW },
    { 15, 13, COLOR_ULTRA_YELLOW },
    { 11, 13, COLOR_ULTRA_YELLOW },
    { 12, 13, COLOR_ULTRA_YELLOW },
    { 11, 19, COLOR_ULTRA_YELLOW },
    { 12, 20, COLOR_ULTRA_YELLOW },

    // Windshield & A-Pillars
    { 19, 16, COLOR_WHITE },
    { 20, 17, COLOR_WHITE },
    { 16, 18, COLOR_WHITE },
    { 17, 18, COLOR_WHITE },
    { 13, 18, COLOR_DARK_GRAY },

    // Roof LiDAR Module ("Pebble")
    { 21, 22, COLOR_NEON_CYAN },
    { 23, 24, COLOR_NEON_CYAN },
    { 21, 23, COLOR_NEON_CYAN },
    { 21, 24, COLOR_NEON_CYAN },
    { 22, 23, COLOR_NEON_CYAN },
    { 22, 24, COLOR_NEON_CYAN },

    // Roof & Cabin Lines
    { 16, 25, COLOR_ULTRA_YELLOW },
    { 17, 26, COLOR_ULTRA_YELLOW },
    { 25, 27, COLOR_ULTRA_YELLOW },
    { 26, 28, COLOR_ULTRA_YELLOW },
    { 25, 26, COLOR_DARK_GRAY },
    { 27, 28, COLOR_DARK_GRAY },

    // Fastback Rear Glass & C-Pillars
    { 27, 29, COLOR_WHITE },
    { 28, 30, COLOR_WHITE },
    { 29, 31, COLOR_WHITE },
    { 30, 31, COLOR_WHITE },
    { 27, 31, COLOR_DARK_GRAY },
    { 28, 31, COLOR_DARK_GRAY },

    // Side Beltline & Windows
    { 19, 32, COLOR_ULTRA_YELLOW },
    { 20, 33, COLOR_ULTRA_YELLOW },
    { 32, 29, COLOR_ULTRA_YELLOW },
    { 33, 30, COLOR_ULTRA_YELLOW },
    { 32, 25, COLOR_DARK_GRAY },
    { 33, 26, COLOR_DARK_GRAY },

    // Side Skirts & Rockers
    { 1, 34, COLOR_DARK_GRAY },
    { 2, 35, COLOR_DARK_GRAY },
    { 34, 36, COLOR_ULTRA_YELLOW },
    { 35, 37, COLOR_ULTRA_YELLOW },
    { 36, 43, COLOR_DARK_GRAY },
    { 37, 44, COLOR_DARK_GRAY },

    // Rear Trunk Deck
    { 29, 38, COLOR_ULTRA_YELLOW },
    { 30, 39, COLOR_ULTRA_YELLOW },
    { 31, 40, COLOR_ULTRA_YELLOW },

    // Continuous Halo Taillight Bar (Signature red loop)
    { 38, 41, COLOR_TAILLIGHT },
    { 41, 40, COLOR_TAILLIGHT },
    { 40, 42, COLOR_TAILLIGHT },
    { 42, 39, COLOR_TAILLIGHT },

    // Rear Aerodynamic Diffuser
    { 43, 45, COLOR_DARK_GRAY },
    { 45, 46, COLOR_DARK_GRAY },
    { 46, 44, COLOR_DARK_GRAY },
    { 38, 43, COLOR_DARK_GRAY },
    { 39, 44, COLOR_DARK_GRAY },

    // Ultra GT Wing Pylons (Dual Swan-neck mounts)
    { 47, 48, COLOR_WING_YELLOW },
    { 49, 50, COLOR_WING_YELLOW },

    // Ultra GT Rear Carbon Wing (Airfoil)
    { 51, 48, COLOR_WING_YELLOW },
    { 48, 50, COLOR_WING_YELLOW },
    { 50, 52, COLOR_WING_YELLOW },
    { 53, 48, COLOR_WING_YELLOW },
    { 50, 54, COLOR_WING_YELLOW },
    { 51, 53, COLOR_WING_YELLOW },
    { 52, 54, COLOR_WING_YELLOW },

    // Wing Endplates
    { 55, 56, COLOR_WING_YELLOW },
    { 51, 55, COLOR_WING_YELLOW },
    { 53, 56, COLOR_WING_YELLOW },
    { 57, 58, COLOR_WING_YELLOW },
    { 52, 57, COLOR_WING_YELLOW },
    { 54, 58, COLOR_WING_YELLOW },

    // Front-Left Wheel Rim & Spokes
    { 60, 61, COLOR_WHEEL_GOLD },
    { 61, 62, COLOR_WHEEL_GOLD },
    { 62, 63, COLOR_WHEEL_GOLD },
    { 63, 64, COLOR_WHEEL_GOLD },
    { 64, 65, COLOR_WHEEL_GOLD },
    { 65, 60, COLOR_WHEEL_GOLD },
    { 59, 61, COLOR_CALIPER },
    { 59, 63, COLOR_WHEEL_GOLD },
    { 59, 65, COLOR_WHEEL_GOLD },

    // Front-Right Wheel Rim & Spokes
    { 67, 68, COLOR_WHEEL_GOLD },
    { 68, 69, COLOR_WHEEL_GOLD },
    { 69, 70, COLOR_WHEEL_GOLD },
    { 70, 71, COLOR_WHEEL_GOLD },
    { 71, 72, COLOR_WHEEL_GOLD },
    { 72, 67, COLOR_WHEEL_GOLD },
    { 66, 68, COLOR_CALIPER },
    { 66, 70, COLOR_WHEEL_GOLD },
    { 66, 72, COLOR_WHEEL_GOLD },

    // Rear-Left Wheel Rim & Spokes
    { 74, 75, COLOR_WHEEL_GOLD },
    { 75, 76, COLOR_WHEEL_GOLD },
    { 76, 77, COLOR_WHEEL_GOLD },
    { 77, 78, COLOR_WHEEL_GOLD },
    { 78, 79, COLOR_WHEEL_GOLD },
    { 79, 74, COLOR_WHEEL_GOLD },
    { 73, 75, COLOR_CALIPER },
    { 73, 77, COLOR_WHEEL_GOLD },
    { 73, 79, COLOR_WHEEL_GOLD },

    // Rear-Right Wheel Rim & Spokes
    { 81, 82, COLOR_WHEEL_GOLD },
    { 82, 83, COLOR_WHEEL_GOLD },
    { 83, 84, COLOR_WHEEL_GOLD },
    { 84, 85, COLOR_WHEEL_GOLD },
    { 85, 86, COLOR_WHEEL_GOLD },
    { 86, 81, COLOR_WHEEL_GOLD },
    { 80, 82, COLOR_CALIPER },
    { 80, 84, COLOR_WHEEL_GOLD },
    { 80, 86, COLOR_WHEEL_GOLD }
};

static const uint16_t NUM_SU7_EDGES = sizeof(SU7_EDGES) / sizeof(SU7_EDGES[0]);
