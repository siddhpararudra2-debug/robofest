#include "state_machine.h"

DroneStateMachine::DroneStateMachine()
    : current_state_(STATE_IDLE),
      pre_pause_state_(STATE_IDLE),
      claimed_mine_x_(0.0f),
      claimed_mine_y_(0.0f) {
}

void DroneStateMachine::begin() {
    current_state_ = STATE_IDLE;
    pre_pause_state_ = STATE_IDLE;
    claimed_mine_x_ = 0.0f;
    claimed_mine_y_ = 0.0f;
}

DroneState DroneStateMachine::update(float pos_x,
                                     float pos_y,
                                     GestureState gesture,
                                     const MineCandidate &candidate,
                                     bool claimResolved,
                                     bool claimWon,
                                     bool markingComplete) {
    (void)pos_x;
    (void)pos_y;

    // ------------------------------------------------------------------------
    // Rule 1: GESTURE_STOP from any non-PAUSED state -> save prePauseState,
    // transition to STATE_PAUSED immediately, and return.
    // ------------------------------------------------------------------------
    if (gesture == GESTURE_STOP && current_state_ != STATE_PAUSED) {
        pre_pause_state_ = current_state_;
        current_state_ = STATE_PAUSED;
        return current_state_;
    }

    // ------------------------------------------------------------------------
    // Rule 2: If currently PAUSED: GESTURE_START resumes into prePauseState.
    // ------------------------------------------------------------------------
    if (current_state_ == STATE_PAUSED) {
        if (gesture == GESTURE_START) {
            current_state_ = pre_pause_state_;
        }
        return current_state_;
    }

    // ------------------------------------------------------------------------
    // Rule 3: IDLE -> SEARCHING on GESTURE_START.
    // ------------------------------------------------------------------------
    if (current_state_ == STATE_IDLE) {
        if (gesture == GESTURE_START) {
            current_state_ = STATE_SEARCHING;
        }
        return current_state_;
    }

    // ------------------------------------------------------------------------
    // Rule 4: SEARCHING -> CLAIMING when candidate is valid & meets confidence threshold.
    // ------------------------------------------------------------------------
    if (current_state_ == STATE_SEARCHING) {
        if (candidate.valid && candidate.confidence >= CONFIDENCE_CLAIM_THRESHOLD) {
            claimed_mine_x_ = candidate.world_x;
            claimed_mine_y_ = candidate.world_y;
            current_state_ = STATE_CLAIMING;
        }
        return current_state_;
    }

    // ------------------------------------------------------------------------
    // Rule 5: CLAIMING -> MARKING if (claimResolved && claimWon);
    //         CLAIMING -> back to SEARCHING if (claimResolved && !claimWon).
    // ------------------------------------------------------------------------
    if (current_state_ == STATE_CLAIMING) {
        if (claimResolved) {
            if (claimWon) {
                current_state_ = STATE_MARKING;
            } else {
                current_state_ = STATE_SEARCHING;
            }
        }
        return current_state_;
    }

    // ------------------------------------------------------------------------
    // Rule 6: MARKING -> DONE on markingComplete.
    // ------------------------------------------------------------------------
    if (current_state_ == STATE_MARKING) {
        if (markingComplete) {
            current_state_ = STATE_DONE;
        }
        return current_state_;
    }

    // ------------------------------------------------------------------------
    // Rule 7: DONE is terminal; return current state.
    // ------------------------------------------------------------------------
    return current_state_;
}

const char* DroneStateMachine::stateToString(DroneState state) {
    switch (state) {
        case STATE_IDLE:      return "STATE_IDLE";
        case STATE_SEARCHING: return "STATE_SEARCHING";
        case STATE_CLAIMING:  return "STATE_CLAIMING";
        case STATE_MARKING:   return "STATE_MARKING";
        case STATE_DONE:      return "STATE_DONE";
        case STATE_PAUSED:    return "STATE_PAUSED";
        default:              return "UNKNOWN";
    }
}
