// PsramAlloc.h — explicit placement of large buffers in PSRAM.
//
// The core's malloc threshold (4 KB) is deliberately NOT lowered: small system
// objects (FreeRTOS queues, driver buffers) must stay in internal RAM — PSRAM
// is unavailable while flash is being written, and touching it from an
// interrupt then crashes the board. So only our own large buffers go to PSRAM,
// and only where we ask for it.
#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <esp_heap_caps.h>
#include <vector>

inline void* psMalloc(size_t n) {
    void* p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return p ? p : malloc(n);   // no PSRAM — regular heap
}
inline void* psRealloc(void* p, size_t n) {
    void* q = heap_caps_realloc(p, n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return q ? q : realloc(p, n);
}

// ArduinoJson 7 allocator: parsed API responses live in PSRAM
struct PsramJsonAllocator : ArduinoJson::Allocator {
    void* allocate(size_t n) override { return psMalloc(n); }
    void  deallocate(void* p) override { free(p); }
    void* reallocate(void* p, size_t n) override { return psRealloc(p, n); }
    static PsramJsonAllocator* instance() {
        static PsramJsonAllocator a;
        return &a;
    }
};

// std::vector allocator (weather icon PNG buffer)
template <typename T>
struct PsramStlAllocator {
    using value_type = T;
    PsramStlAllocator() = default;
    template <typename U> PsramStlAllocator(const PsramStlAllocator<U>&) {}
    T* allocate(size_t n) {
        void* p = psMalloc(n * sizeof(T));
        if (!p) throw std::bad_alloc();
        return static_cast<T*>(p);
    }
    void deallocate(T* p, size_t) { free(p); }
    template <typename U> bool operator==(const PsramStlAllocator<U>&) const { return true; }
    template <typename U> bool operator!=(const PsramStlAllocator<U>&) const { return false; }
};

using PsBytes = std::vector<uint8_t, PsramStlAllocator<uint8_t>>;
