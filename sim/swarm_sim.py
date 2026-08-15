#!/usr/bin/env python3
"""
Decentralized ESP-NOW Swarm Protocol Simulator
==============================================
Pure-Python simulation of the claim/yield swarm protocol without any hardware
or PlatformIO dependencies.

Models:
- Decentralized contention resolution with tiebreaking (confidence > lowest drone_id).
- Competing claims folded into local pending-claim buckets within CLAIM_PROXIMITY_M.
- Spatial deduplication (isMineAlreadyClaimed) and local clearance exclusion (isInsideExclusionZone).
- CLAIM_WIN and MARK_DONE propagation across all peer nodes.
"""

import math
import random
from enum import IntEnum
from dataclasses import dataclass, field
from typing import List, Dict, Optional, Tuple

# ============================================================================
# PROTOCOL CONSTANTS (Matching include/config.h)
# ============================================================================
CLAIM_PROXIMITY_M = 1.0       # Deduplication proximity radius (meters)
MINE_EXCLUSION_M = 1.0        # Spatial clearance exclusion zone radius (meters)
CLAIM_WINDOW_MS = 250         # Claim contention duration (ms)
SWARM_BROADCAST_MS = 150      # Position heartbeat period (ms)
MAX_KNOWN_MINES = 40          # Max memory capacity for known mines
MAX_PENDING_CLAIMS = 8        # Max concurrent pending claims

# ============================================================================
# PACKET & DATA DEFINITIONS
# ============================================================================
class PacketType(IntEnum):
    PKT_POSITION  = 1
    PKT_CLAIM     = 2
    PKT_CLAIM_WIN = 3
    PKT_MARK_DONE = 4

@dataclass
class SwarmPacket:
    type: PacketType
    drone_id: int
    pos_x: float = 0.0
    pos_y: float = 0.0
    mine_x: float = 0.0
    mine_y: float = 0.0
    confidence: int = 0
    claim_id: int = 0

@dataclass
class KnownMine:
    x: float
    y: float
    claimed_by_drone: int
    marked_done: bool = False
    active: bool = True

@dataclass
class PendingClaim:
    claim_id: int
    mine_x: float
    mine_y: float
    my_confidence: int
    created_at_ms: float
    best_drone_id: int
    best_confidence: int
    active: bool = True

# ============================================================================
# SIMULATED DRONE AGENT
# ============================================================================
class SimulatedDrone:
    def __init__(self, drone_id: int, pos_x: float = 0.0, pos_y: float = 0.0):
        self.drone_id = drone_id
        self.pos_x = pos_x
        self.pos_y = pos_y
        self.known_mines: List[KnownMine] = []
        self.pending_claims: Dict[int, PendingClaim] = {}
        self._seq_counter = 0

    def broadcast_position(self) -> SwarmPacket:
        return SwarmPacket(
            type=PacketType.PKT_POSITION,
            drone_id=self.drone_id,
            pos_x=self.pos_x,
            pos_y=self.pos_y
        )

    def broadcast_claim(self, mine_x: float, mine_y: float, confidence: int, current_time_ms: float = 0.0) -> Tuple[int, SwarmPacket]:
        if len(self.pending_claims) >= MAX_PENDING_CLAIMS:
            return (0, None)

        self._seq_counter += 1
        claim_id = (self.drone_id << 24) | (self._seq_counter & 0x00FFFFFF)

        claim = PendingClaim(
            claim_id=claim_id,
            mine_x=mine_x,
            mine_y=mine_y,
            my_confidence=confidence,
            created_at_ms=current_time_ms,
            best_drone_id=self.drone_id,
            best_confidence=confidence,
            active=True
        )
        self.pending_claims[claim_id] = claim

        pkt = SwarmPacket(
            type=PacketType.PKT_CLAIM,
            drone_id=self.drone_id,
            mine_x=mine_x,
            mine_y=mine_y,
            confidence=confidence,
            claim_id=claim_id
        )
        return (claim_id, pkt)

    def resolve_claim(self, claim_id: int) -> bool:
        if claim_id not in self.pending_claims:
            return False

        claim = self.pending_claims[claim_id]
        if not claim.active:
            return False

        # Tiebreak rule: highest confidence wins; if tied, lowest drone_id wins
        if claim.best_confidence > claim.my_confidence:
            won = False
        elif claim.best_confidence == claim.my_confidence:
            won = (self.drone_id <= claim.best_drone_id)
        else:
            won = True

        # Clean up pending claim
        del self.pending_claims[claim_id]
        return won

    def broadcast_claim_win(self, mine_x: float, mine_y: float) -> SwarmPacket:
        self._register_known_mine(mine_x, mine_y, self.drone_id, False)
        return SwarmPacket(
            type=PacketType.PKT_CLAIM_WIN,
            drone_id=self.drone_id,
            mine_x=mine_x,
            mine_y=mine_y,
            confidence=100
        )

    def broadcast_mark_done(self, mine_x: float, mine_y: float) -> SwarmPacket:
        self._register_known_mine(mine_x, mine_y, self.drone_id, True)
        return SwarmPacket(
            type=PacketType.PKT_MARK_DONE,
            drone_id=self.drone_id,
            mine_x=mine_x,
            mine_y=mine_y,
            confidence=100
        )

    def receive_packet(self, pkt: SwarmPacket):
        # 1. Ignore packets from own DRONE_ID
        if pkt.drone_id == self.drone_id:
            return

        if pkt.type == PacketType.PKT_CLAIM:
            # Fold into any pending claim within CLAIM_PROXIMITY_M
            for claim in self.pending_claims.values():
                if claim.active:
                    dist = math.hypot(pkt.mine_x - claim.mine_x, pkt.mine_y - claim.mine_y)
                    if dist <= CLAIM_PROXIMITY_M:
                        # Tiebreak rule update
                        if pkt.confidence > claim.best_confidence:
                            claim.best_confidence = pkt.confidence
                            claim.best_drone_id = pkt.drone_id
                        elif pkt.confidence == claim.best_confidence:
                            if pkt.drone_id < claim.best_drone_id:
                                claim.best_drone_id = pkt.drone_id

        elif pkt.type == PacketType.PKT_CLAIM_WIN:
            self._register_known_mine(pkt.mine_x, pkt.mine_y, pkt.drone_id, False)
            # Invalidate any matching local pending claim
            to_remove = [cid for cid, c in self.pending_claims.items()
                         if math.hypot(pkt.mine_x - c.mine_x, pkt.mine_y - c.mine_y) <= CLAIM_PROXIMITY_M]
            for cid in to_remove:
                del self.pending_claims[cid]

        elif pkt.type == PacketType.PKT_MARK_DONE:
            self._register_known_mine(pkt.mine_x, pkt.mine_y, pkt.drone_id, True)
            to_remove = [cid for cid, c in self.pending_claims.items()
                         if math.hypot(pkt.mine_x - c.mine_x, pkt.mine_y - c.mine_y) <= CLAIM_PROXIMITY_M]
            for cid in to_remove:
                del self.pending_claims[cid]

    def is_mine_already_claimed(self, x: float, y: float) -> bool:
        """Dedup check: returns True if within CLAIM_PROXIMITY_M of any active known mine."""
        for m in self.known_mines:
            if m.active and math.hypot(x - m.x, y - m.y) <= CLAIM_PROXIMITY_M:
                return True
        return False

    def is_inside_exclusion_zone(self, x: float, y: float) -> bool:
        """Local exclusion rule: returns True if within MINE_EXCLUSION_M of any active known mine."""
        for m in self.known_mines:
            if m.active and math.hypot(x - m.x, y - m.y) <= MINE_EXCLUSION_M:
                return True
        return False

    def _register_known_mine(self, x: float, y: float, claimed_by: int, marked_done: bool):
        for m in self.known_mines:
            if m.active and math.hypot(x - m.x, y - m.y) <= CLAIM_PROXIMITY_M:
                m.claimed_by_drone = claimed_by
                if marked_done:
                    m.marked_done = True
                return

        if len(self.known_mines) < MAX_KNOWN_MINES:
            self.known_mines.append(KnownMine(
                x=x,
                y=y,
                claimed_by_drone=claimed_by,
                marked_done=marked_done,
                active=True
            ))

# ============================================================================
# SWARM SIMULATOR HARNESS
# ============================================================================
class SwarmNetworkSimulator:
    def __init__(self, num_drones: int = 2):
        self.drones: Dict[int, SimulatedDrone] = {
            i: SimulatedDrone(drone_id=i, pos_x=float(i * 2), pos_y=0.0)
            for i in range(1, num_drones + 1)
        }

    def broadcast(self, sender_id: int, packet: SwarmPacket):
        """Simulates instantaneous radio mesh broadcast to all peers."""
        if not packet:
            return
        for peer_id, drone in self.drones.items():
            if peer_id != sender_id:
                drone.receive_packet(packet)

# ============================================================================
# TEST SUITE
# ============================================================================
def test_two_drones_contention_single_winner():
    """
    Test 1: Two drones discover the same mine at (5.0, 5.0) with jittered confidence.
    Verify that exactly one drone wins and CLAIM_WIN propagates.
    """
    sim = SwarmNetworkSimulator(num_drones=2)
    d1 = sim.drones[1]
    d2 = sim.drones[2]

    mine_x, mine_y = 5.0, 5.0

    # Drone 1 sees mine with confidence 72, Drone 2 sees mine with confidence 84
    conf1 = 72
    conf2 = 84

    # Both drones broadcast claims
    cid1, pkt1 = d1.broadcast_claim(mine_x, mine_y, conf1)
    cid2, pkt2 = d2.broadcast_claim(mine_x + 0.1, mine_y - 0.1, conf2) # within 1.0m

    sim.broadcast(1, pkt1)
    sim.broadcast(2, pkt2)

    # Resolve after CLAIM_WINDOW_MS
    won1 = d1.resolve_claim(cid1)
    won2 = d2.resolve_claim(cid2)

    # Drone 2 had higher confidence (84 > 72) -> exactly one winner
    assert won2 is True, "Drone 2 with higher confidence must win"
    assert won1 is False, "Drone 1 with lower confidence must lose"
    assert (won1 ^ won2) is True, "Exactly one drone must win contention"

    # Drone 2 broadcasts claim win
    win_pkt = d2.broadcast_claim_win(mine_x, mine_y)
    sim.broadcast(2, win_pkt)

    # Both drones must now have mine in their known list
    assert len(d1.known_mines) == 1, "Drone 1 must register winner's mine"
    assert len(d2.known_mines) == 1, "Drone 2 must register own won mine"
    assert d1.known_mines[0].claimed_by_drone == 2
    assert d2.known_mines[0].claimed_by_drone == 2

def test_deterministic_tiebreak_by_drone_id():
    """
    Test 2: Two drones discover the same mine with IDENTICAL confidence scores.
    Verify that tiebreak is deterministic and favors lower drone_id.
    """
    sim = SwarmNetworkSimulator(num_drones=2)
    d1 = sim.drones[1]
    d2 = sim.drones[2]

    mine_x, mine_y = 12.0, 8.0
    identical_conf = 75

    cid1, pkt1 = d1.broadcast_claim(mine_x, mine_y, identical_conf)
    cid2, pkt2 = d2.broadcast_claim(mine_x + 0.2, mine_y + 0.2, identical_conf)

    sim.broadcast(1, pkt1)
    sim.broadcast(2, pkt2)

    won1 = d1.resolve_claim(cid1)
    won2 = d2.resolve_claim(cid2)

    assert won1 is True, "Drone 1 (lower ID=1) must win tiebreak against Drone 2 (ID=2)"
    assert won2 is False, "Drone 2 must lose tiebreak"

def test_spatial_deduplication_sync():
    """
    Test 3: Mine claimed by Drone 1 is recognized as already claimed by Drone 2's dedup check.
    """
    sim = SwarmNetworkSimulator(num_drones=2)
    d1 = sim.drones[1]
    d2 = sim.drones[2]

    mine_x, mine_y = 3.0, 7.0

    # Before claim: Drone 2 does NOT see it as claimed
    assert d2.is_mine_already_claimed(mine_x, mine_y) is False

    # Drone 1 wins claim and broadcasts
    win_pkt = d1.broadcast_claim_win(mine_x, mine_y)
    sim.broadcast(1, win_pkt)

    # After claim win propagation:
    # Within 1.0m -> deduplicated (True)
    assert d2.is_mine_already_claimed(mine_x + 0.5, mine_y + 0.5) is True, "Must detect within 1.0m proximity"
    # Farther away (> 1.0m) -> False
    assert d2.is_mine_already_claimed(mine_x + 2.0, mine_y + 2.0) is False, "Must not block outside proximity"

def test_exclusion_zone_boundaries():
    """
    Test 4: Exclusion zone correctly blocks a waypoint within 1.0m and allows one outside.
    """
    drone = SimulatedDrone(drone_id=1)
    mine_x, mine_y = 10.0, 10.0

    # Register known mine
    drone.broadcast_claim_win(mine_x, mine_y)

    # Test exact exclusion boundary (1.0m radius)
    assert drone.is_inside_exclusion_zone(10.0, 10.5) is True, "0.5m distance must be inside exclusion zone"
    assert drone.is_inside_exclusion_zone(10.7, 10.7) is True, "~0.99m distance must be inside exclusion zone"
    assert drone.is_inside_exclusion_zone(11.5, 10.0) is False, "1.5m distance must be outside exclusion zone"
    assert drone.is_inside_exclusion_zone(0.0, 0.0) is False, "Origin must be outside exclusion zone"

def test_mark_done_propagation():
    """
    Test 5: MARK_DONE propagates completion status to all peers.
    """
    sim = SwarmNetworkSimulator(num_drones=2)
    d1 = sim.drones[1]
    d2 = sim.drones[2]

    mine_x, mine_y = 6.0, 4.0

    # Drone 1 claims and marks done
    win_pkt = d1.broadcast_claim_win(mine_x, mine_y)
    sim.broadcast(1, win_pkt)

    done_pkt = d1.broadcast_mark_done(mine_x, mine_y)
    sim.broadcast(1, done_pkt)

    assert d2.known_mines[0].marked_done is True, "Peer must observe marked_done = True"
    assert d2.is_inside_exclusion_zone(mine_x, mine_y) is True, "Marked mine still maintains exclusion"

# ============================================================================
# MAIN RUNNER
# ============================================================================
def main():
    print("=================================================================")
    print(" ESP32-S3 SWARM PROTOCOL SIMULATOR (sim/swarm_sim.py)")
    print("=================================================================")

    tests = [
        ("Two Drones Contention -> Single Winner", test_two_drones_contention_single_winner),
        ("Deterministic Tiebreak by Drone ID", test_deterministic_tiebreak_by_drone_id),
        ("Spatial Deduplication Sync (isMineAlreadyClaimed)", test_spatial_deduplication_sync),
        ("Exclusion Zone Boundaries (isInsideExclusionZone)", test_exclusion_zone_boundaries),
        ("Mark Done Propagation Across Swarm", test_mark_done_propagation),
    ]

    passed = 0
    failed = 0

    for name, test_fn in tests:
        try:
            test_fn()
            print(f" [PASS] {name}")
            passed += 1
        except AssertionError as e:
            print(f" [FAIL] {name}: {e}")
            failed += 1
        except Exception as e:
            print(f" [ERROR] {name}: {e}")
            failed += 1

    print("=================================================================")
    print(f" RESULTS: {passed} passed, {failed} failed out of {len(tests)} tests.")
    print("=================================================================")

    if failed > 0:
        exit(1)
    else:
        print("ALL SWARM SIMULATION PROTOCOL TESTS PASSED SUCCESSFULLY!")

if __name__ == "__main__":
    main()
