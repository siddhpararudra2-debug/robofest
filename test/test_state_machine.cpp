#include <unity.h>
#include "state_machine.h"

void setUp(void) {
    // Run before each test
}

void tearDown(void) {
    // Run after each test
}

/**
 * @brief Happy path: IDLE -> SEARCHING -> CLAIMING -> MARKING -> DONE
 */
void test_happy_path_lifecycle() {
    DroneStateMachine fsm;
    fsm.begin();

    TEST_ASSERT_EQUAL(STATE_IDLE, fsm.currentState());

    MineCandidate candidate = {false, 0.0f, 0.0f, 0};

    // Tick with GESTURE_START -> transition to SEARCHING
    DroneState s = fsm.update(0.0f, 0.0f, GESTURE_START, candidate, false, false, false);
    TEST_ASSERT_EQUAL(STATE_SEARCHING, s);
    TEST_ASSERT_EQUAL(STATE_SEARCHING, fsm.currentState());

    // Searching with low confidence candidate -> stays SEARCHING
    candidate.valid = true;
    candidate.confidence = 30; // Below CONFIDENCE_CLAIM_THRESHOLD (60)
    candidate.world_x = 4.5f;
    candidate.world_y = 2.5f;
    s = fsm.update(0.0f, 0.0f, GESTURE_NONE, candidate, false, false, false);
    TEST_ASSERT_EQUAL(STATE_SEARCHING, s);

    // Searching with high confidence candidate -> transitions to CLAIMING and stores coords
    candidate.confidence = 75; // >= 60
    s = fsm.update(0.0f, 0.0f, GESTURE_NONE, candidate, false, false, false);
    TEST_ASSERT_EQUAL(STATE_CLAIMING, s);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 4.5f, fsm.claimedMineX());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 2.5f, fsm.claimedMineY());

    // In CLAIMING while contention unresolved -> stays CLAIMING
    s = fsm.update(0.0f, 0.0f, GESTURE_NONE, candidate, false, false, false);
    TEST_ASSERT_EQUAL(STATE_CLAIMING, s);

    // Claim resolved and won -> transitions to MARKING
    s = fsm.update(0.0f, 0.0f, GESTURE_NONE, candidate, true, true, false);
    TEST_ASSERT_EQUAL(STATE_MARKING, s);

    // Marking in progress -> stays MARKING
    s = fsm.update(0.0f, 0.0f, GESTURE_NONE, candidate, false, false, false);
    TEST_ASSERT_EQUAL(STATE_MARKING, s);

    // Marking complete -> transitions to terminal STATE_DONE
    s = fsm.update(0.0f, 0.0f, GESTURE_NONE, candidate, false, false, true);
    TEST_ASSERT_EQUAL(STATE_DONE, s);

    // Subsequent ticks stay in DONE
    s = fsm.update(0.0f, 0.0f, GESTURE_NONE, candidate, false, false, false);
    TEST_ASSERT_EQUAL(STATE_DONE, s);
}

/**
 * @brief Stop gesture interrupting SEARCHING and resuming correctly into SEARCHING
 */
void test_stop_gesture_interrupt_searching_and_resume() {
    DroneStateMachine fsm;
    fsm.begin();

    MineCandidate dummy_cand = {false, 0.0f, 0.0f, 0};

    // IDLE -> SEARCHING
    fsm.update(0.0f, 0.0f, GESTURE_START, dummy_cand, false, false, false);
    TEST_ASSERT_EQUAL(STATE_SEARCHING, fsm.currentState());

    // Send GESTURE_STOP -> transitions to PAUSED immediately
    DroneState s = fsm.update(0.0f, 0.0f, GESTURE_STOP, dummy_cand, false, false, false);
    TEST_ASSERT_EQUAL(STATE_PAUSED, s);
    TEST_ASSERT_EQUAL(STATE_PAUSED, fsm.currentState());

    // Ticking while paused with no gesture remains PAUSED
    s = fsm.update(0.0f, 0.0f, GESTURE_NONE, dummy_cand, false, false, false);
    TEST_ASSERT_EQUAL(STATE_PAUSED, s);

    // GESTURE_START resumes into prePauseState (SEARCHING, not IDLE)
    s = fsm.update(0.0f, 0.0f, GESTURE_START, dummy_cand, false, false, false);
    TEST_ASSERT_EQUAL(STATE_SEARCHING, s);
    TEST_ASSERT_EQUAL(STATE_SEARCHING, fsm.currentState());
}

/**
 * @brief Stop interrupting CLAIMING and resuming into CLAIMING (not restarting claim or clearing coords)
 */
void test_stop_interrupt_claiming_and_resume_into_claiming() {
    DroneStateMachine fsm;
    fsm.begin();

    MineCandidate candidate = {true, 8.2f, 6.4f, 80};

    // Start -> Search -> Claim
    fsm.update(0.0f, 0.0f, GESTURE_START, candidate, false, false, false); // SEARCHING
    fsm.update(0.0f, 0.0f, GESTURE_NONE, candidate, false, false, false);  // CLAIMING
    TEST_ASSERT_EQUAL(STATE_CLAIMING, fsm.currentState());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 8.2f, fsm.claimedMineX());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 6.4f, fsm.claimedMineY());

    // Stop gesture while in CLAIMING -> PAUSED
    DroneState s = fsm.update(0.0f, 0.0f, GESTURE_STOP, candidate, false, false, false);
    TEST_ASSERT_EQUAL(STATE_PAUSED, s);

    // Coords preserved
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 8.2f, fsm.claimedMineX());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 6.4f, fsm.claimedMineY());

    // Resume -> returns to CLAIMING
    s = fsm.update(0.0f, 0.0f, GESTURE_START, candidate, false, false, false);
    TEST_ASSERT_EQUAL(STATE_CLAIMING, s);
    TEST_ASSERT_EQUAL(STATE_CLAIMING, fsm.currentState());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 8.2f, fsm.claimedMineX());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 6.4f, fsm.claimedMineY());
}

/**
 * @brief Losing a claim (claimResolved && !claimWon) returns to SEARCHING
 */
void test_claim_loss_returns_to_searching() {
    DroneStateMachine fsm;
    fsm.begin();

    MineCandidate candidate = {true, 3.0f, 3.0f, 70};

    // Move to CLAIMING
    fsm.update(0.0f, 0.0f, GESTURE_START, candidate, false, false, false);
    fsm.update(0.0f, 0.0f, GESTURE_NONE, candidate, false, false, false);
    TEST_ASSERT_EQUAL(STATE_CLAIMING, fsm.currentState());

    // Claim resolved but lost (claimResolved=true, claimWon=false)
    MineCandidate no_cand = {false, 0.0f, 0.0f, 0};
    DroneState s = fsm.update(0.0f, 0.0f, GESTURE_NONE, no_cand, true, false, false);
    TEST_ASSERT_EQUAL(STATE_SEARCHING, s);
    TEST_ASSERT_EQUAL(STATE_SEARCHING, fsm.currentState());
}

/**
 * @brief Stop gesture overriding an in-progress claim immediately before resolution
 */
void test_stop_overriding_in_progress_claim() {
    DroneStateMachine fsm;
    fsm.begin();

    MineCandidate candidate = {true, 5.0f, 5.0f, 90};

    // Move to CLAIMING
    fsm.update(0.0f, 0.0f, GESTURE_START, candidate, false, false, false);
    fsm.update(0.0f, 0.0f, GESTURE_NONE, candidate, false, false, false);
    TEST_ASSERT_EQUAL(STATE_CLAIMING, fsm.currentState());

    // In a single update tick, claimResolved=true, claimWon=true BUT gesture=GESTURE_STOP is present
    // Priority Rule 1 must override resolution and transition immediately to STATE_PAUSED
    DroneState s = fsm.update(0.0f, 0.0f, GESTURE_STOP, candidate, true, true, false);
    TEST_ASSERT_EQUAL(STATE_PAUSED, s);
    TEST_ASSERT_EQUAL(STATE_PAUSED, fsm.currentState());
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_happy_path_lifecycle);
    RUN_TEST(test_stop_gesture_interrupt_searching_and_resume);
    RUN_TEST(test_stop_interrupt_claiming_and_resume_into_claiming);
    RUN_TEST(test_claim_loss_returns_to_searching);
    RUN_TEST(test_stop_overriding_in_progress_claim);
    return UNITY_END();
}
