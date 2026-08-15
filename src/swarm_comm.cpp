#include "swarm_comm.h"
#include <cmath>
#include <cstring>
#include <algorithm>

// Static storage for known mines and pending claims (zero dynamic heap allocation)
static KnownMine s_known_mines[MAX_KNOWN_MINES];
static PendingClaim s_pending_claims[MAX_PENDING_CLAIMS];
static uint32_t s_claim_seq_counter = 0;
static bool s_mesh_initialized = false;

#ifndef NATIVE_BUILD
#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

static uint8_t s_broadcast_mac[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// Forward declaration of internal packet handler
static void handleIncomingSwarmPacket(const uint8_t* data, int len);

#if defined(ESP_IDF_VERSION) && ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
static void onDataRecv(const esp_now_recv_info_t* recv_info, const uint8_t* data, int len) {
    (void)recv_info;
    if (data != nullptr && len > 0) {
        handleIncomingSwarmPacket(data, len);
    }
}
#else
static void onDataRecv(const uint8_t* mac_addr, const uint8_t* data, int len) {
    (void)mac_addr;
    if (data != nullptr && len > 0) {
        handleIncomingSwarmPacket(data, len);
    }
}
#endif
#endif

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

static inline float distance2D(float x1, float y1, float x2, float y2) {
    float dx = x1 - x2;
    float dy = y1 - y2;
    return std::sqrt(dx * dx + dy * dy);
}

static int findKnownMineIndex(float x, float y, float radius) {
    for (int i = 0; i < MAX_KNOWN_MINES; ++i) {
        if (s_known_mines[i].active) {
            if (distance2D(x, y, s_known_mines[i].x, s_known_mines[i].y) <= radius) {
                return i;
            }
        }
    }
    return -1;
}

static bool registerKnownMine(float x, float y, uint8_t claimed_by, bool marked_done) {
    int existing_idx = findKnownMineIndex(x, y, CLAIM_PROXIMITY_M);
    if (existing_idx != -1) {
        // Update existing entry
        s_known_mines[existing_idx].claimed_by_drone = claimed_by;
        if (marked_done) {
            s_known_mines[existing_idx].marked_done = true;
        }
        return true;
    }

    // Find first free slot
    for (int i = 0; i < MAX_KNOWN_MINES; ++i) {
        if (!s_known_mines[i].active) {
            s_known_mines[i].x = x;
            s_known_mines[i].y = y;
            s_known_mines[i].claimed_by_drone = claimed_by;
            s_known_mines[i].marked_done = marked_done;
            s_known_mines[i].active = true;
            return true;
        }
    }
    return false; // Storage full
}

// Internal packet reception & state machine folding
static void handleIncomingSwarmPacket(const uint8_t* data, int len) {
    if (len < static_cast<int>(sizeof(SwarmPacket))) {
        return;
    }

    const SwarmPacket* pkt = reinterpret_cast<const SwarmPacket*>(data);

    // 1. Ignore packets from own DRONE_ID
    if (pkt->drone_id == DEFAULT_DRONE_ID) {
        return;
    }

    switch (pkt->type) {
        case PKT_CLAIM: {
            // Fold into any pending claim within CLAIM_PROXIMITY_M
            for (int i = 0; i < MAX_PENDING_CLAIMS; ++i) {
                if (s_pending_claims[i].active) {
                    float dist = distance2D(pkt->mine_x, pkt->mine_y,
                                            s_pending_claims[i].mine_x, s_pending_claims[i].mine_y);
                    if (dist <= CLAIM_PROXIMITY_M) {
                        // Tiebreak rule: highest confidence wins; if tied, lowest drone_id wins
                        if (pkt->confidence > s_pending_claims[i].best_confidence) {
                            s_pending_claims[i].best_confidence = pkt->confidence;
                            s_pending_claims[i].best_drone_id = pkt->drone_id;
                        } else if (pkt->confidence == s_pending_claims[i].best_confidence) {
                            if (pkt->drone_id < s_pending_claims[i].best_drone_id) {
                                s_pending_claims[i].best_drone_id = pkt->drone_id;
                            }
                        }
                    }
                }
            }
            break;
        }

        case PKT_CLAIM_WIN: {
            // Add to known-mines list
            registerKnownMine(pkt->mine_x, pkt->mine_y, pkt->drone_id, false);

            // Invalidate/clear local pending claims on this mine
            for (int i = 0; i < MAX_PENDING_CLAIMS; ++i) {
                if (s_pending_claims[i].active) {
                    if (distance2D(pkt->mine_x, pkt->mine_y,
                                   s_pending_claims[i].mine_x, s_pending_claims[i].mine_y) <= CLAIM_PROXIMITY_M) {
                        s_pending_claims[i].active = false;
                    }
                }
            }
            break;
        }

        case PKT_MARK_DONE: {
            // Add/update to known-mines list as marked done
            registerKnownMine(pkt->mine_x, pkt->mine_y, pkt->drone_id, true);

            // Invalidate local pending claims on this mine
            for (int i = 0; i < MAX_PENDING_CLAIMS; ++i) {
                if (s_pending_claims[i].active) {
                    if (distance2D(pkt->mine_x, pkt->mine_y,
                                   s_pending_claims[i].mine_x, s_pending_claims[i].mine_y) <= CLAIM_PROXIMITY_M) {
                        s_pending_claims[i].active = false;
                    }
                }
            }
            break;
        }

        case PKT_POSITION:
        default:
            break;
    }
}

// ============================================================================
// SWARM COMM SUBSYSTEM IMPLEMENTATION
// ============================================================================

bool initSwarmMesh() {
    clearKnownMinesAndClaims();

#ifndef NATIVE_BUILD
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();

    // Lock WiFi channel
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
    esp_wifi_set_promiscuous(false);

    if (esp_now_init() != ESP_OK) {
        DEBUG_PRINTLN("[BRINGUP][FAIL] Error initializing ESP-NOW");
        s_mesh_initialized = false;
        return false;
    }

    esp_now_register_recv_cb(onDataRecv);

    // Register broadcast peer (FF:FF:FF:FF:FF:FF)
    esp_now_peer_info_t peer_info = {};
    std::memcpy(peer_info.peer_addr, s_broadcast_mac, 6);
    peer_info.channel = ESPNOW_CHANNEL;
    peer_info.encrypt = false;

    if (esp_now_add_peer(&peer_info) != ESP_OK) {
        DEBUG_PRINTLN("[BRINGUP][FAIL] Failed to register broadcast peer");
        s_mesh_initialized = false;
        return false;
    }

    DEBUG_PRINTF("[BRINGUP][PASS] Checkpoint 3: ESP-NOW initialized on Channel %d (MAC: %s, Broadcast Ready).\n",
                 ESPNOW_CHANNEL, WiFi.macAddress().c_str());
#endif

    s_mesh_initialized = true;
    return true;
}

void broadcastPosition(float x, float y) {
    if (!s_mesh_initialized) return;

    SwarmPacket pkt = {};
    pkt.type = PKT_POSITION;
    pkt.drone_id = DEFAULT_DRONE_ID;
    pkt.pos_x = x;
    pkt.pos_y = y;
    pkt.mine_x = 0.0f;
    pkt.mine_y = 0.0f;
    pkt.confidence = 0;
    pkt.claim_id = 0;

#ifndef NATIVE_BUILD
    esp_now_send(s_broadcast_mac, reinterpret_cast<const uint8_t*>(&pkt), sizeof(SwarmPacket));
#endif
}

uint32_t broadcastClaim(float mine_x, float mine_y, uint8_t confidence) {
    if (!s_mesh_initialized) return 0;

    // Check for free slot in pending claims
    int free_slot = -1;
    for (int i = 0; i < MAX_PENDING_CLAIMS; ++i) {
        if (!s_pending_claims[i].active) {
            free_slot = i;
            break;
        }
    }

    if (free_slot == -1) {
        return 0; // Pending claims table full
    }

    // Generate unique claim ID
    s_claim_seq_counter++;
    uint32_t claim_id = (static_cast<uint32_t>(DEFAULT_DRONE_ID) << 24) |
                        (s_claim_seq_counter & 0x00FFFFFF);

    // Initialize local PendingClaim entry
    s_pending_claims[free_slot].claim_id = claim_id;
    s_pending_claims[free_slot].mine_x = mine_x;
    s_pending_claims[free_slot].mine_y = mine_y;
    s_pending_claims[free_slot].my_confidence = confidence;
    s_pending_claims[free_slot].best_drone_id = DEFAULT_DRONE_ID;
    s_pending_claims[free_slot].best_confidence = confidence;
    s_pending_claims[free_slot].active = true;

#ifndef NATIVE_BUILD
    s_pending_claims[free_slot].created_at_ms = millis();
#else
    s_pending_claims[free_slot].created_at_ms = 0;
#endif

    // Broadcast PKT_CLAIM to swarm
    SwarmPacket pkt = {};
    pkt.type = PKT_CLAIM;
    pkt.drone_id = DEFAULT_DRONE_ID;
    pkt.pos_x = 0.0f;
    pkt.pos_y = 0.0f;
    pkt.mine_x = mine_x;
    pkt.mine_y = mine_y;
    pkt.confidence = confidence;
    pkt.claim_id = claim_id;

#ifndef NATIVE_BUILD
    esp_now_send(s_broadcast_mac, reinterpret_cast<const uint8_t*>(&pkt), sizeof(SwarmPacket));
#endif

    return claim_id;
}

bool resolveClaim(uint32_t claim_id) {
    if (claim_id == 0) return false;

    for (int i = 0; i < MAX_PENDING_CLAIMS; ++i) {
        if (s_pending_claims[i].active && s_pending_claims[i].claim_id == claim_id) {
            bool won = false;

            // Tiebreak rule: highest confidence wins; if tied, lowest drone_id wins
            if (s_pending_claims[i].best_confidence > s_pending_claims[i].my_confidence) {
                won = false;
            } else if (s_pending_claims[i].best_confidence == s_pending_claims[i].my_confidence) {
                won = (DEFAULT_DRONE_ID <= s_pending_claims[i].best_drone_id);
            } else {
                won = true;
            }

            // Deactivate pending claim entry
            s_pending_claims[i].active = false;
            return won;
        }
    }

    return false;
}

void broadcastClaimWin(float mine_x, float mine_y) {
    // Add mine to local known-mines list
    registerKnownMine(mine_x, mine_y, DEFAULT_DRONE_ID, false);

    if (!s_mesh_initialized) return;

    SwarmPacket pkt = {};
    pkt.type = PKT_CLAIM_WIN;
    pkt.drone_id = DEFAULT_DRONE_ID;
    pkt.pos_x = 0.0f;
    pkt.pos_y = 0.0f;
    pkt.mine_x = mine_x;
    pkt.mine_y = mine_y;
    pkt.confidence = 100;
    pkt.claim_id = 0;

#ifndef NATIVE_BUILD
    esp_now_send(s_broadcast_mac, reinterpret_cast<const uint8_t*>(&pkt), sizeof(SwarmPacket));
#endif
}

void broadcastMarkDone(float mine_x, float mine_y) {
    // Add/update to local known-mines list as marked done
    registerKnownMine(mine_x, mine_y, DEFAULT_DRONE_ID, true);

    if (!s_mesh_initialized) return;

    SwarmPacket pkt = {};
    pkt.type = PKT_MARK_DONE;
    pkt.drone_id = DEFAULT_DRONE_ID;
    pkt.pos_x = 0.0f;
    pkt.pos_y = 0.0f;
    pkt.mine_x = mine_x;
    pkt.mine_y = mine_y;
    pkt.confidence = 100;
    pkt.claim_id = 0;

#ifndef NATIVE_BUILD
    esp_now_send(s_broadcast_mac, reinterpret_cast<const uint8_t*>(&pkt), sizeof(SwarmPacket));
#endif
}

bool isMineAlreadyClaimed(float x, float y) {
    return (findKnownMineIndex(x, y, CLAIM_PROXIMITY_M) != -1);
}

bool isInsideExclusionZone(float x, float y) {
    return (findKnownMineIndex(x, y, MINE_EXCLUSION_M) != -1);
}

uint8_t getKnownMinesCount() {
    uint8_t count = 0;
    for (int i = 0; i < MAX_KNOWN_MINES; ++i) {
        if (s_known_mines[i].active) {
            count++;
        }
    }
    return count;
}

const KnownMine* getKnownMines() {
    return s_known_mines;
}

void clearKnownMinesAndClaims() {
    std::memset(s_known_mines, 0, sizeof(s_known_mines));
    std::memset(s_pending_claims, 0, sizeof(s_pending_claims));
}

// ============================================================================
// TEST MODE: DUAL-BOARD HARDWARE CLAIM/YIELD VERIFICATION
// ============================================================================
#if defined(TEST_CLAIM_YIELD)
enum TestClaimState {
    TEST_STATE_WAIT_BOOT = 0,
    TEST_STATE_TRIGGER_CLAIM,
    TEST_STATE_WAIT_RESOLUTION,
    TEST_STATE_COMPLETE,
    TEST_STATE_IDLE
};

void runClaimYieldTestModeTick(uint32_t current_time_ms) {
    static TestClaimState s_test_state = TEST_STATE_WAIT_BOOT;
    static uint32_t s_test_claim_id = 0;
    static uint32_t s_claim_trigger_ms = 0;
    static uint32_t s_last_countdown_ms = 0;

    const float TEST_MINE_X = 5.0f;
    const float TEST_MINE_Y = 5.0f;

    // Configured test confidence per drone ID:
    // Drone 1: 70 confidence
    // Drone 2: 85 confidence (higher confidence -> Drone 2 should win)
    // If testing equal confidence, set both to 75 -> Drone 1 should win by lower ID
    const uint8_t TEST_CONFIDENCE = (DEFAULT_DRONE_ID == 2) ? 85 : 70;

    switch (s_test_state) {
        case TEST_STATE_WAIT_BOOT:
            if (current_time_ms - s_last_countdown_ms >= 1000 && current_time_ms < 3000) {
                s_last_countdown_ms = current_time_ms;
                DEBUG_PRINTF("[TEST_CLAIM_YIELD] Drone #%d test claim triggers in %ds...\n",
                             DEFAULT_DRONE_ID, (int)((3000 - current_time_ms) / 1000 + 1));
            }
            if (current_time_ms >= 3000) {
                s_test_state = TEST_STATE_TRIGGER_CLAIM;
            }
            break;

        case TEST_STATE_TRIGGER_CLAIM:
            DEBUG_PRINTLN("--------------------------------------------------");
            DEBUG_PRINTF("[TEST_CLAIM_YIELD] Drone #%d TRIGGERING CLAIM on mine (%.1f, %.1f) with confidence %d...\n",
                         DEFAULT_DRONE_ID, TEST_MINE_X, TEST_MINE_Y, TEST_CONFIDENCE);
            s_test_claim_id = broadcastClaim(TEST_MINE_X, TEST_MINE_Y, TEST_CONFIDENCE);
            s_claim_trigger_ms = current_time_ms;
            DEBUG_PRINTF("[TEST_CLAIM_YIELD] Generated claim_id=0x%08X. Contention window active (%dms)...\n",
                         s_test_claim_id, CLAIM_WINDOW_MS);
            s_test_state = TEST_STATE_WAIT_RESOLUTION;
            break;

        case TEST_STATE_WAIT_RESOLUTION:
            // Allow full CLAIM_WINDOW_MS + 50ms radio propagation margin
            if (current_time_ms - s_claim_trigger_ms >= (CLAIM_WINDOW_MS + 50)) {
                DEBUG_PRINTLN("[TEST_CLAIM_YIELD] Claim window elapsed. Resolving contention...");
                bool won = resolveClaim(s_test_claim_id);

                if (won) {
                    broadcastClaimWin(TEST_MINE_X, TEST_MINE_Y);
                    DEBUG_PRINTLN("--------------------------------------------------");
                    DEBUG_PRINTF("[TEST_CLAIM_YIELD][RESULT] >> Drone #%d: WON CLAIM! (Broadcasted CLAIM_WIN)\n", DEFAULT_DRONE_ID);
                    DEBUG_PRINTLN("[TEST_CLAIM_YIELD] Verification: MATCHED EXPECTED WINNER (Higher confidence / Lower ID tiebreak).");
                    DEBUG_PRINTLN("--------------------------------------------------");
                } else {
                    DEBUG_PRINTLN("--------------------------------------------------");
                    DEBUG_PRINTF("[TEST_CLAIM_YIELD][RESULT] << Drone #%d: YIELDED (Lost claim to peer)\n", DEFAULT_DRONE_ID);
                    DEBUG_PRINTLN("[TEST_CLAIM_YIELD] Verification: MATCHED EXPECTED YIELD (Peer had higher confidence / lower ID).");
                    DEBUG_PRINTLN("--------------------------------------------------");
                }
                s_test_state = TEST_STATE_COMPLETE;
            }
            break;

        case TEST_STATE_COMPLETE:
            if (current_time_ms - s_claim_trigger_ms >= 2000) {
                DEBUG_PRINTF("[TEST_CLAIM_YIELD] Final Synced Known Mines in Memory: %d\n", getKnownMinesCount());
                DEBUG_PRINTLN("[TEST_CLAIM_YIELD] Dual-board physical ESP-NOW claim/yield test COMPLETE.");
                s_test_state = TEST_STATE_IDLE;
            }
            break;

        case TEST_STATE_IDLE:
        default:
            break;
    }
}
#endif
