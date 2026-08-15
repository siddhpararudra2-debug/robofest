#ifndef VISION_H
#define VISION_H

#include <stdint.h>
#include <stdbool.h>
#include "config.h"

// ============================================================================
// DATA STRUCTURES & ENUMS
// ============================================================================

struct MineCandidate {
    bool valid;          // True if confidence >= CONFIDENCE_MIN_TO_REPORT and temporally verified
    float world_x;       // Estimated global/arena X coordinate in meters
    float world_y;       // Estimated global/arena Y coordinate in meters
    uint8_t confidence;  // Scored 0-100 value (circularity + normalized area)
};

enum GestureState {
    GESTURE_NONE = 0,
    GESTURE_START,
    GESTURE_STOP
};

// Internal Blob Representation for Pipeline
struct DetectedBlob {
    bool found;
    float centroid_x;    // Sub-pixel centroid X coordinate
    float centroid_y;    // Sub-pixel centroid Y coordinate
    float area_px;       // Pixel area
    float circularity;   // 0.0 to 1.0 (4 * pi * Area / Perimeter^2)
};

// ============================================================================
// VISION SUBSYSTEM API
// ============================================================================

/**
 * @brief Initialize ESP32-S3-CAM sensor via esp_camera_init.
 * @return true if camera initialized successfully, false otherwise.
 */
bool initCamera();

/**
 * @brief Runs the complete mine detection pipeline:
 *        HSV Threshold -> Blob Detection -> Circularity Filter -> Sub-pixel Centroid ->
 *        Confidence Scoring (0-100) -> Temporal Persistence Check -> Pixel-to-World Transform.
 * 
 * @param drone_x  Current drone X coordinate in meters
 * @param drone_y  Current drone Y coordinate in meters
 * @param altitude Current drone altitude in meters
 * @return MineCandidate with valid flag, world coordinates, and 0-100 confidence score.
 */
MineCandidate scanForMine(float drone_x, float drone_y, float altitude);

/**
 * @brief Independent per-drone gesture watcher (runs on local camera frame).
 *        Applies GESTURE_START_CONFIDENCE and GESTURE_STOP_CONFIDENCE filters.
 * @return Detected GestureState (GESTURE_NONE, GESTURE_START, or GESTURE_STOP).
 */
GestureState watchForGesture();

#endif // VISION_H
