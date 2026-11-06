/**
 * @file MemoryGuard.cpp
 * @brief Implementation of OS Memory Starvation Hardening & Emergency RAM Reserve Pool
 */

#include "MemoryGuard.h"
#include "Logger.h"
#include <cstdlib>
#include <cstring>
#include <cstdio>

namespace core {

void*         MemoryGuard::s_emergencyPool = nullptr;
volatile bool MemoryGuard::s_oomTriggered  = false;
char          MemoryGuard::s_oomReason[64] = {0};

void MemoryGuard::init() {
    s_oomTriggered = false;
    s_oomReason[0] = '\0';
    reclaimEmergencyPool();
}

bool MemoryGuard::reclaimEmergencyPool() {
    if (s_emergencyPool != nullptr) {
        return true;
    }
    size_t freeNow = xPortGetFreeHeapSize();
    if (freeNow > (EMERGENCY_POOL_BYTES + LUA_HARD_HEAP_FLOOR)) {
        s_emergencyPool = pvPortMalloc(EMERGENCY_POOL_BYTES);
        if (s_emergencyPool != nullptr) {
            // Touch memory to ensure physical backing
            memset(s_emergencyPool, 0xA5, 32);
            return true;
        }
    }
    return false;
}

void MemoryGuard::releaseEmergencyPool(const char* reason) {
    if (reason && s_oomReason[0] == '\0') {
        snprintf(s_oomReason, sizeof(s_oomReason), "%s", reason);
    }
    if (s_emergencyPool != nullptr) {
        void* ptr = s_emergencyPool;
        s_emergencyPool = nullptr;
        vPortFree(ptr);
        // Safe fixed-buffer serial output without dynamic heap allocation
        Serial.printf("[MemoryGuard] EMERGENCY POOL RELEASED (%u B freed) - Reason: %s\n",
                      (unsigned)EMERGENCY_POOL_BYTES, reason ? reason : "OOM");
    }
}

bool MemoryGuard::hasEmergencyPool() {
    return (s_emergencyPool != nullptr);
}

bool MemoryGuard::ensureHeapFloor(size_t additionalBytes) {
    size_t freeHeap = xPortGetFreeHeapSize();
    if (freeHeap < CRITICAL_HEAP_FLOOR) {
        s_oomTriggered = true;
        releaseEmergencyPool("Critical Heap (<8KB)");
        return false;
    }
    if (freeHeap < (LUA_HARD_HEAP_FLOOR + additionalBytes)) {
        s_oomTriggered = true;
        char buf[64];
        snprintf(buf, sizeof(buf), "Heap floor violated (%u B free)", (unsigned)freeHeap);
        releaseEmergencyPool(buf);
        return false;
    }
    return true;
}

void* MemoryGuard::safeMalloc(size_t size) {
    if (size == 0) return nullptr;
    if (!ensureHeapFloor(size)) {
        return nullptr;
    }
    void* ptr = pvPortMalloc(size);
    if (ptr == nullptr) {
        s_oomTriggered = true;
        releaseEmergencyPool("pvPortMalloc returned nullptr");
    }
    return ptr;
}

void* MemoryGuard::luaSandboxedAlloc(void* ud, void* ptr, size_t osize, size_t nsize) {
    (void)ud;

    // 1. Free request
    if (nsize == 0) {
        if (ptr != nullptr) {
            free(ptr);
        }
        return nullptr;
    }

    // 2. Shrinking or in-place reallocation (never increases heap consumption)
    if (ptr != nullptr && nsize <= osize) {
        void* shrunk = realloc(ptr, nsize);
        return (shrunk != nullptr) ? shrunk : ptr;
    }

    // 3. Allocation or growth: enforce hard heap floor BEFORE calling realloc/malloc!
    size_t delta = (ptr != nullptr) ? (nsize - osize) : nsize;

    // Reject absurd single-allocation requests (> 64 KB) immediately
    if (delta > 65536) {
        s_oomTriggered = true;
        releaseEmergencyPool("Lua alloc >64KB rejected");
        return nullptr;
    }

    size_t freeHeap = xPortGetFreeHeapSize();
    if (freeHeap <= (LUA_HARD_HEAP_FLOOR + delta)) {
        s_oomTriggered = true;
        char reason[64];
        snprintf(reason, sizeof(reason), "Lua OOM: %uB free, req +%uB",
                 (unsigned)freeHeap, (unsigned)delta);
        releaseEmergencyPool(reason);
        return nullptr; // Forces Lua VM to raise LUA_ERRMEM cleanly!
    }

    void* newPtr = realloc(ptr, nsize);
    if (newPtr == nullptr) {
        s_oomTriggered = true;
        releaseEmergencyPool("Lua realloc nullptr");
    }
    return newPtr;
}

bool MemoryGuard::wasOOMTriggered() {
    return s_oomTriggered;
}

void MemoryGuard::clearOOMFlag() {
    s_oomTriggered = false;
    s_oomReason[0] = '\0';
}

const char* MemoryGuard::getLastOOMReason() {
    return (s_oomReason[0] != '\0') ? s_oomReason : "Out of Memory";
}

} // namespace core
