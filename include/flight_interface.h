#ifndef FLIGHT_INTERFACE_H
#define FLIGHT_INTERFACE_H

#include <stdbool.h>

struct FlightTelemetry {
    float x;
    float y;
    float z;
    float yaw;
};

// ============================================================================
// FLIGHT INTERFACE PROTOTYPES (Minimal Stubs for Compilation / Hardware Link)
// ============================================================================

inline bool initFlightLink() {
    return true;
}

inline FlightTelemetry readFlightTelemetry() {
    FlightTelemetry tel = {0.0f, 0.0f, 1.0f, 0.0f}; // Default 1.0m altitude
    return tel;
}

inline void holdPosition() {
    // Flight controller hold position command stub
}

inline void sweepPattern(FlightTelemetry tel, bool (*exclusionCheck)(float, float)) {
    (void)tel;
    (void)exclusionCheck;
    // Flight controller arena sweep pattern stub with dynamic exclusion steering
}

inline bool placeMarker(float x, float y) {
    (void)x;
    (void)y;
    // Marker deployment actuator stub (returns true when marking complete)
    return true;
}

#endif // FLIGHT_INTERFACE_H
