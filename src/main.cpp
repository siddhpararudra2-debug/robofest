#ifndef NATIVE_BUILD

#include <Arduino.h>
#include "config.h"
#include "state_machine.h"
#include "vision.h"
#include "swarm_comm.h"
#include "flight_interface.h"
#include "led_marker.h"

#ifndef NATIVE_BUILD
#include "esp_camera.h"
#endif

// ============================================================================
// SYSTEM INSTANCE
// ============================================================================
static DroneStateMachine sm;

void setup() {
    Serial.begin(115200);
    delay(1000); // Allow USB CDC connection

    DEBUG_PRINTLN("==================================================");
    DEBUG_PRINTF(" ESP32-S3 DRONE SWARM FIRMWARE (Drone #%d)\n", DRONE_ID);
    DEBUG_PRINTLN(" Single-Board Hardware Bring-Up Diagnostics Mode");
    DEBUG_PRINTLN("==================================================");

    sm.begin();
    initFlightLink();
    initLEDMarker();

    // ------------------------------------------------------------------------
    // CHECKPOINT 1: Camera Subsystem Initialization
    // ------------------------------------------------------------------------
    bool cam_ok = initCamera();
    if (!cam_ok) {
        DEBUG_PRINTLN("[BRINGUP][FAIL] Checkpoint 1: Camera initialization failed!");
    }

    // ------------------------------------------------------------------------
    // CHECKPOINT 2: Frame Capture Resolution & Buffer Verification
    // ------------------------------------------------------------------------
#if DEBUG_SERIAL && !defined(NATIVE_BUILD)
    if (cam_ok) {
        camera_fb_t* test_fb = esp_camera_fb_get();
        if (test_fb != nullptr) {
            DEBUG_PRINTF("[BRINGUP][PASS] Checkpoint 2: Frame captured: %d x %d (expected %d x %d), buffer=%zu bytes, format=%d\n",
                         test_fb->width, test_fb->height, FRAME_WIDTH, FRAME_HEIGHT, test_fb->len, test_fb->format);
            if (test_fb->width == FRAME_WIDTH && test_fb->height == FRAME_HEIGHT) {
                DEBUG_PRINTLN("[BRINGUP][PASS] Frame resolution exactly matches config specification.");
            } else {
                DEBUG_PRINTF("[BRINGUP][WARN] Frame resolution (%dx%d) != expected (%dx%d).\n",
                             test_fb->width, test_fb->height, FRAME_WIDTH, FRAME_HEIGHT);
            }
            esp_camera_fb_return(test_fb);
        } else {
            DEBUG_PRINTLN("[BRINGUP][FAIL] Checkpoint 2: Failed to acquire test frame buffer from camera.");
        }
    }
#endif

    // ------------------------------------------------------------------------
    // CHECKPOINT 3: ESP-NOW Mesh Standalone Smoke Test (No peer required)
    // ------------------------------------------------------------------------
    bool mesh_ok = initSwarmMesh();
    if (!mesh_ok) {
        DEBUG_PRINTLN("[BRINGUP][FAIL] Checkpoint 3: ESP-NOW Mesh initialization failed!");
    }

    setLEDMarker(LED_IDLE);
    DEBUG_PRINTLN("==================================================");
    DEBUG_PRINTLN("[SYSTEM] Bring-up checkpoints complete. Loop starting...");
    DEBUG_PRINTLN("==================================================");
}

void loop() {
    uint32_t now = millis();

#if defined(TEST_CLAIM_YIELD)
    // Hardware-in-the-loop claim/yield test mode runner
    runClaimYieldTestModeTick(now);
#endif

    // 1. Read flight telemetry
    FlightTelemetry tel = readFlightTelemetry();

    // 2. Broadcast position every SWARM_BROADCAST_MS (decoupled from state machine)
    static uint32_t last_pos_broadcast_ms = 0;
    if (now - last_pos_broadcast_ms >= SWARM_BROADCAST_MS) {
        last_pos_broadcast_ms = now;
        broadcastPosition(tel.x, tel.y);
    }

    // 3. Poll watchForGesture()
    GestureState gesture = watchForGesture();

    // 4. If state == SEARCHING: call scanForMine(); if candidate valid but isMineAlreadyClaimed(), discard it
    DroneState cur_state = sm.currentState();
    MineCandidate candidate = {false, 0.0f, 0.0f, 0};

    if (cur_state == STATE_SEARCHING) {
        candidate = scanForMine(tel.x, tel.y, tel.z);
        if (candidate.valid && isMineAlreadyClaimed(candidate.world_x, candidate.world_y)) {
            candidate.valid = false; // Another drone already owns it, discard
        }
    }

    // 5. If state == CLAIMING: on first tick, broadcastClaim(); once CLAIM_WINDOW_MS has elapsed, resolveClaim() and broadcastClaimWin() if won
    static uint32_t claim_id = 0;
    static uint32_t claim_start_ms = 0;
    static uint8_t last_claim_confidence = CONFIDENCE_CLAIM_THRESHOLD;
    bool claimResolved = false;
    bool claimWon = false;

    // Track detection confidence when valid candidate appears
    if (candidate.valid && candidate.confidence >= CONFIDENCE_CLAIM_THRESHOLD) {
        last_claim_confidence = candidate.confidence;
    }

    if (cur_state == STATE_CLAIMING) {
        if (claim_id == 0) {
            // First tick in CLAIMING: broadcast claim
            claim_id = broadcastClaim(sm.claimedMineX(), sm.claimedMineY(), last_claim_confidence);
            claim_start_ms = now;
            claimResolved = false;
            claimWon = false;
        } else {
            // Contention window check
            if (now - claim_start_ms >= CLAIM_WINDOW_MS) {
                claimResolved = true;
                claimWon = resolveClaim(claim_id);
                if (claimWon) {
                    broadcastClaimWin(sm.claimedMineX(), sm.claimedMineY());
                }
                claim_id = 0; // Reset claim ID after resolution
            }
        }
    } else {
        // Reset claim persistence when not in CLAIMING state
        claim_id = 0;
        claimResolved = false;
        claimWon = false;
    }

    // 6. If state == MARKING: call placeMarker(); on success, broadcastMarkDone()
    bool markingComplete = false;
    if (cur_state == STATE_MARKING) {
        if (placeMarker(sm.claimedMineX(), sm.claimedMineY())) {
            markingComplete = true;
            broadcastMarkDone(sm.claimedMineX(), sm.claimedMineY());
        }
    }

    // 7. Call sm.update(...) with all the above inputs
    DroneState resulting_state = sm.update(
        tel.x,
        tel.y,
        gesture,
        candidate,
        claimResolved,
        claimWon,
        markingComplete
    );

    // 8. Switch on resulting state: set matching LED pattern, call holdPosition() or sweepPattern()
    switch (resulting_state) {
        case STATE_IDLE:
            setLEDMarker(LED_IDLE);
            holdPosition();
            break;

        case STATE_SEARCHING:
            setLEDMarker(LED_SEARCHING);
            sweepPattern(tel, isInsideExclusionZone);
            break;

        case STATE_CLAIMING:
            setLEDMarker(LED_CLAIMING);
            holdPosition();
            break;

        case STATE_MARKING:
            setLEDMarker(LED_MARKING);
            holdPosition();
            break;

        case STATE_DONE:
            setLEDMarker(LED_DONE);
            holdPosition();
            break;

        case STATE_PAUSED:
            setLEDMarker(LED_PAUSED);
            holdPosition();
            break;

        default:
            setLEDMarker(LED_IDLE);
            holdPosition();
            break;
    }

    // Loop pacing ~50Hz (20ms)
    delay(20);
}

#endif // NATIVE_BUILD
