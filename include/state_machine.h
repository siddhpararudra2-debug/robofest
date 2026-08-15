#ifndef STATE_MACHINE_H
#define STATE_MACHINE_H

#include <stdint.h>
#include <stdbool.h>
#include "config.h"
#include "vision.h"

// ============================================================================
// STATE MACHINE ENUMS
// ============================================================================

enum DroneState {
    STATE_IDLE = 0,
    STATE_SEARCHING,
    STATE_CLAIMING,
    STATE_MARKING,
    STATE_DONE,
    STATE_PAUSED
};

// ============================================================================
// HARDWARE-AGNOSTIC DRONE STATE MACHINE CLASS
// ============================================================================

class DroneStateMachine {
public:
    DroneStateMachine();

    // Lifecycle
    void begin();

    // Main deterministic update tick
    DroneState update(float pos_x,
                      float pos_y,
                      GestureState gesture,
                      const MineCandidate &candidate,
                      bool claimResolved,
                      bool claimWon,
                      bool markingComplete);

    // Getters
    DroneState currentState() const { return current_state_; }
    float claimedMineX() const { return claimed_mine_x_; }
    float claimedMineY() const { return claimed_mine_y_; }

    // Helper for debugging / string conversion
    static const char* stateToString(DroneState state);

private:
    DroneState current_state_;
    DroneState pre_pause_state_;

    float claimed_mine_x_;
    float claimed_mine_y_;
};

#endif // STATE_MACHINE_H
