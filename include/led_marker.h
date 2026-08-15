#ifndef LED_MARKER_H
#define LED_MARKER_H

#include <stdbool.h>

enum LEDPattern {
    LED_IDLE = 0,
    LED_SEARCHING,
    LED_CLAIMING,
    LED_MARKING,
    LED_DONE,
    LED_PAUSED
};

// ============================================================================
// LED & MARKER PROTOTYPES (Minimal Stubs for Compilation / Hardware Link)
// ============================================================================

inline bool initLEDMarker() {
    return true;
}

inline void setLEDMarker(LEDPattern p) {
    (void)p;
    // Set WS2812 / hardware status LED pattern stub
}

#endif // LED_MARKER_H
