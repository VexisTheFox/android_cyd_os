/**
 * @file MemoryGuard.h
 * @brief OS Memory Starvation Hardening, Emergency RAM Reserve Pool & Lua Heap Floor Guard
 *
 * Implements the 4 core defensive memory hardening guarantees:
 *   1. Emergency RAM Reserve Pool (6 KB pre-allocated at boot, released only on critical OOM
 *      so the OS crash renderer, kernel logger, and task killer never suffer nested OOM).
 *   2. Hard Heap Floor Limit for untrusted Lua scripts (custom lua_Alloc + C-API entry guard
 *      enforcing a strict 10 KB free heap floor via xPortGetFreeHeapSize()).
 *   3. Mandatory null-pointer checked allocation wrappers (safeMalloc / safeRealloc).
 *   4. Zero-heap-allocation crash & OOM state tracking.
 */

#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/portable.h>

namespace core {

class MemoryGuard {
public:
    /// Size of the emergency heap reserve pool held for OS crash handlers (6 KB)
    static constexpr size_t EMERGENCY_POOL_BYTES = 6144;

    /// Hard free-heap floor below which untrusted Lua scripts are denied allocations (10 KB)
    static constexpr size_t LUA_HARD_HEAP_FLOOR  = 10240;

    /// Critical threshold where the emergency pool is released for OS recovery (8 KB)
    static constexpr size_t CRITICAL_HEAP_FLOOR  = 8192;

    /**
     * @brief Initialize the emergency RAM reserve pool at OS startup.
     */
    static void init();

    /**
     * @brief Re-allocate the emergency RAM reserve pool after a crashed app is cleaned up.
     */
    static bool reclaimEmergencyPool();

    /**
     * @brief Release the emergency RAM reserve pool back to the system heap so the
     * OS crash screen, kernel.log writer, and UI shell can execute without nested OOM.
     */
    static void releaseEmergencyPool(const char* reason);

    /**
     * @brief Check if the emergency pool is currently intact.
     */
    static bool hasEmergencyPool();

    /**
     * @brief Verify that free heap is above the hard floor for Lua-to-C++ API calls.
     * If free heap drops below LUA_HARD_HEAP_FLOOR, releases the emergency pool and
     * flags an OOM abort.
     * @param additionalBytes Extra bytes the C++ API call is about to allocate
     * @return true if safe to proceed, false if heap floor violated
     */
    static bool ensureHeapFloor(size_t additionalBytes = 0);

    /**
     * @brief Checked heap allocation wrapper that enforces the hard floor and checks nullptr.
     */
    static void* safeMalloc(size_t size);

    /**
     * @brief Custom lua_Alloc function for lua_newstate() that prevents any Lua script
     * (even with GC stopped) from exhausting system SRAM below LUA_HARD_HEAP_FLOOR.
     */
    static void* luaSandboxedAlloc(void* ud, void* ptr, size_t osize, size_t nsize);

    /**
     * @brief Query whether the current/last script triggered an OOM or heap-floor violation.
     */
    static bool wasOOMTriggered();
    static void clearOOMFlag();
    static const char* getLastOOMReason();

private:
    static void* s_emergencyPool;
    static volatile bool s_oomTriggered;
    static char s_oomReason[64];
};

} // namespace core
