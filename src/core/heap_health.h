#pragma once

#include <cstdint>

// Memory pressure levels for graduated degradation.
// Consumers can query getPressureLevel() and shed load accordingly.
// See heap_research.md for theoretical basis (Robson bounds, Knuth's 50% rule).
enum class HeapPressureLevel : uint8_t {
    Normal   = 0,  // All features enabled
    Caution  = 1,  // Reduce non-essential features (UI animations, max networks)
    Warning  = 2,  // Aggressive shedding (deinit BLE, shrink vectors)
    Critical = 3   // Freeze state, auto-brew, graceful recovery
};

namespace HeapHealth {
    // Update heap health state (rate-limited).
    void update();

    // Current heap health percent (0-100), raw instantaneous.
    uint8_t getPercent();

    // EMA-smoothed percent for UI display (absorbs transient spikes).
    uint8_t getDisplayPercent();

    // Current memory pressure level (graduated degradation).
    HeapPressureLevel getPressureLevel();

    // Reset peak baseline to current heap values.
    void resetPeaks(bool suppressToast = true);

    // Watermarks (min observed values)
    uint32_t getMinFree();
    uint32_t getMinLargest();

    // Knuth's Rule: free_blocks / allocated_blocks ratio.
    // By the Fifty Percent Rule, should be ~0.5 at steady state.
    // Values above 0.7 indicate pathological fragmentation.
    float getKnuthRatio();

    // Enable/disable Knuth ratio computation (expensive heap enumeration).
    // Only enable when diagnostics menu is active.
    void setKnuthEnabled(bool enable);

    // Conditioning trigger (set by update(), consumed by caller)
    bool consumeConditionRequest();

    // Session watermark persistence (binary file on SD).
    // loadPreviousSession(): call at boot after SD init.
    // persistWatermarks(): call from update() or main loop, rate-limited internally.
    void loadPreviousSession();
    void persistWatermarks();
    uint32_t getPrevMinFree();
    uint32_t getPrevMinLargest();

    // Toast helpers
    bool shouldShowToast();
    bool isToastImproved();
    uint8_t getToastDelta();
}
