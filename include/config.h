#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>

// ============================================================================
// IDENTITY
// ============================================================================
// Note: DRONE_ID is the only line that changes per drone build (1-254)
#ifndef DRONE_ID
#define DRONE_ID                    1
#endif
#define DEFAULT_DRONE_ID            DRONE_ID

// ============================================================================
// ARENA & SAFETY
// ============================================================================
#define MINE_EXCLUSION_M            1.0f    // Spatial exclusion zone around known mines
#define CLAIM_PROXIMITY_M           1.0f    // Deduplication proximity radius for mine claims
#define MIN_SAFE_SEPARATION_M       0.6f    // Minimum inter-drone safe separation distance
#define OBSTACLE_AVOID_DISTANCE_M   0.8f    // Dynamic obstacle avoidance trigger distance
#define TAKEOFF_ALTITUDE_M          1.0f    // Target takeoff altitude (meters)
#define MAX_ALTITUDE_M              5.0f    // Maximum ceiling altitude (meters)
#define TARGET_LOST_TIMEOUT_MS      1500    // Timeout before reverting from tracking to search

#define BATTERY_NOMINAL_MV          3700    // Nominal 1S LiPo voltage (mV)
#define BATTERY_LOW_MV              3400    // Low battery threshold (triggers RTH)
#define BATTERY_CRITICAL_MV         3200    // Critical battery threshold (triggers Emergency Land)

// ============================================================================
// CAMERA HARDWARE & OPTICS
// ============================================================================
#define FRAME_WIDTH                 320     // Frame width in pixels (QVGA)
#define FRAME_HEIGHT                240     // Frame height in pixels (QVGA)
#define CAMERA_FRAME_WIDTH          FRAME_WIDTH
#define CAMERA_FRAME_HEIGHT         FRAME_HEIGHT

// Field of view in radians (placeholder, confirm against camera lens datasheet)
#define CAMERA_H_FOV_RAD            1.0123f // Horizontal Field of View (~58 deg)
#define CAMERA_V_FOV_RAD            0.7854f // Vertical Field of View (~45 deg)
#define H_FOV                       CAMERA_H_FOV_RAD
#define V_FOV                       CAMERA_V_FOV_RAD

// ============================================================================
// VISION THRESHOLDS
// ============================================================================
#define MIN_CIRCULARITY             0.70f   // Minimum circularity metric (4*pi*Area/Perimeter^2)
#define CONFIDENCE_MIN_TO_REPORT    40      // Minimum score (0-100) to report candidate as valid
#define CONFIDENCE_CLAIM_THRESHOLD  60      // Minimum score (0-100) required to initiate swarm claim
#define TEMPORAL_PERSIST_FRAMES     3       // Consecutive frames required for temporal persistence
#define MAX_EXPECTED_BLOB_AREA_PX   800.0f  // Expected max blob area for normalization at QVGA
#define MAX_MINE_DRIFT_M            0.25f   // Maximum drift tolerance across consecutive frames (m)

// ============================================================================
// GESTURE (ASYMMETRIC)
// ============================================================================
#define GESTURE_START_CONFIDENCE    65      // Lower threshold: easier to trigger start gesture
#define GESTURE_STOP_CONFIDENCE     85      // Higher threshold: stricter confidence to stop

// ============================================================================
// SWARM / ESP-NOW PROTOCOL
// ============================================================================
#define ESPNOW_CHANNEL              6       // WiFi Channel for ESP-NOW radio mesh (1-13)
#define SWARM_COMM_CHANNEL          ESPNOW_CHANNEL
#define SWARM_BROADCAST_ID          0xFF    // Broadcast address for all drones
#define SWARM_MAX_PEERS             16      // Maximum tracked neighbors in swarm

#define SWARM_BROADCAST_MS          150     // Position broadcast interval (ms)
#define CLAIM_WINDOW_MS             250     // Claim contention resolution window (ms)
#define PEER_TIMEOUT_MS             500     // Peer disconnection timeout (ms)
#define FORMATION_SYNC_TIMEOUT_MS   1000    // Formation lost timeout (ms)

#define MAX_KNOWN_MINES             40      // Max stored known mines (fixed RAM budget)
#define MAX_PENDING_CLAIMS          8       // Max concurrent pending claims (fixed RAM budget)

// ============================================================================
// LED & VISUAL INDICATORS
// ============================================================================
#define LED_DATA_PIN                48      // WS2812 / Status LED Data GPIO
#define LED_COUNT                   12      // Number of addressable LEDs in ring/strip

// ============================================================================
// TIMING & FREQUENCY CONFIGURATION
// ============================================================================
#define CONTROL_LOOP_FREQ_HZ        50      // Flight state machine tick rate (50Hz = 20ms)
#define CONTROL_LOOP_PERIOD_MS      (1000 / CONTROL_LOOP_FREQ_HZ)

#define VISION_LOOP_FREQ_HZ         20      // Vision frame processing rate (20Hz = 50ms)
#define VISION_LOOP_PERIOD_MS       (1000 / VISION_LOOP_FREQ_HZ)

// ============================================================================
// FORMATION GEOMETRY OFFSETS (Default Slot Offsets relative to Leader in meters)
// ============================================================================
struct FormationSlotOffset {
    float x; // Forward (+) / Backward (-)
    float y; // Right (+) / Left (-)
    float z; // Up (+) / Down (-)
};

// ============================================================================
// ESP32-S3 CAMERA HARDWARE PIN DEFINITIONS (ESP32-S3-CAM Typical Defaults)
// ============================================================================
#ifndef NATIVE_BUILD
#define PWDN_GPIO_NUM       -1
#define RESET_GPIO_NUM      -1
#define XCLK_GPIO_NUM       15
#define SIOD_GPIO_NUM        4
#define SIOC_GPIO_NUM        5

#define Y9_GPIO_NUM         16
#define Y8_GPIO_NUM         17
#define Y7_GPIO_NUM         18
#define Y6_GPIO_NUM         12
#define Y5_GPIO_NUM         10
#define Y4_GPIO_NUM          8
#define Y3_GPIO_NUM          9
#define Y2_GPIO_NUM         11
#define VSYNC_GPIO_NUM       6
#define HREF_GPIO_NUM        7
#define PCLK_GPIO_NUM       13
#endif

// ============================================================================
// DIAGNOSTICS & DEBUG SERIAL CONFIGURATION
// ============================================================================
#define DEBUG_SERIAL                1       // Set to 0 to disable all diagnostic prints

#if DEBUG_SERIAL && !defined(NATIVE_BUILD)
  #define DEBUG_PRINTF(...)         Serial.printf(__VA_ARGS__)
  #define DEBUG_PRINTLN(x)          Serial.println(x)
  #define DEBUG_PRINT(x)            Serial.print(x)
#else
  #define DEBUG_PRINTF(...)         ((void)0)
  #define DEBUG_PRINTLN(x)          ((void)0)
  #define DEBUG_PRINT(x)            ((void)0)
#endif

#endif // CONFIG_H
