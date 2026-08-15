#ifndef SWARM_COMM_H
#define SWARM_COMM_H

#include <stdint.h>
#include <stdbool.h>
#include "config.h"

// ============================================================================
// PACKET TYPES & STRUCTURES (Decentralized ESP-NOW Swarm Protocol)
// ============================================================================

enum PacketType : uint8_t {
    PKT_POSITION  = 0x01,
    PKT_CLAIM     = 0x02,
    PKT_CLAIM_WIN = 0x03,
    PKT_MARK_DONE = 0x04
};

#pragma pack(push, 1)

struct SwarmPacket {
    uint8_t  type;        // PacketType
    uint8_t  drone_id;    // Originating drone ID
    float    pos_x;       // Drone current X position in meters
    float    pos_y;       // Drone current Y position in meters
    float    mine_x;      // Target mine X coordinate in meters
    float    mine_y;      // Target mine Y coordinate in meters
    uint8_t  confidence;  // Detection confidence score (0-100)
    uint32_t claim_id;    // Unique claim sequence identifier
};

#pragma pack(pop)

// ============================================================================
// LOCAL STORAGE STRUCTURES (Fixed Size, Zero Heap Allocation)
// ============================================================================

struct KnownMine {
    float    x;
    float    y;
    bool     marked_done;
    uint8_t  claimed_by_drone;
    uint32_t timestamp_ms;
    bool     active;
};

struct PendingClaim {
    uint32_t claim_id;
    float    mine_x;
    float    mine_y;
    uint8_t  my_confidence;
    uint32_t created_at_ms;
    uint8_t  best_drone_id;    // Tracks the current winning drone ID
    uint8_t  best_confidence;  // Tracks the highest received confidence
    bool     active;
};

// ============================================================================
// SWARM COMM SUBSYSTEM API
// ============================================================================

/**
 * @brief Initialize WiFi STA mode and ESP-NOW mesh broadcast peer.
 * @return true on success, false otherwise.
 */
bool initSwarmMesh();

/**
 * @brief Broadcast current drone coordinates to all peers.
 */
void broadcastPosition(float x, float y);

/**
 * @brief Opens a local pending claim and broadcasts PKT_CLAIM to the swarm.
 * @param mine_x Target mine X coordinate
 * @param mine_y Target mine Y coordinate
 * @param confidence Scored detection confidence (0-100)
 * @return Unique claim_id generated for this contention attempt (0 if full).
 */
uint32_t broadcastClaim(float mine_x, float mine_y, uint8_t confidence);

/**
 * @brief Evaluates whether this drone won the claim contention after CLAIM_WINDOW_MS.
 *        Tiebreak rule: highest confidence wins; if tied, lowest drone_id wins.
 * @param claim_id The claim ID to resolve.
 * @return true if this drone won the claim, false otherwise.
 */
bool resolveClaim(uint32_t claim_id);

/**
 * @brief Broadcasts PKT_CLAIM_WIN and registers mine into the local known-mines list.
 */
void broadcastClaimWin(float mine_x, float mine_y);

/**
 * @brief Broadcasts PKT_MARK_DONE indicating mine clearance is complete.
 */
void broadcastMarkDone(float mine_x, float mine_y);

/**
 * @brief Spatial deduplication check.
 * @return true if (x, y) is within CLAIM_PROXIMITY_M of any active known mine.
 */
bool isMineAlreadyClaimed(float x, float y);

/**
 * @brief Clearance exclusion check (local spatial exclusion rule, no global occupancy grid).
 * @return true if (x, y) is within MINE_EXCLUSION_M of any active known mine.
 */
bool isInsideExclusionZone(float x, float y);

/**
 * @brief Optional query helpers for diagnostic and testing inspection.
 */
uint8_t getKnownMinesCount();
const KnownMine* getKnownMines();
void clearKnownMinesAndClaims();

#if defined(TEST_CLAIM_YIELD)
/**
 * @brief Test runner for hardware-in-the-loop claim/yield contention verification.
 *        Gated entirely behind the TEST_CLAIM_YIELD build flag.
 */
void runClaimYieldTestModeTick(uint32_t current_time_ms);
#endif

#endif // SWARM_COMM_H
