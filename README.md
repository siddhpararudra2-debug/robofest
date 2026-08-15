# robofest: ESP32-S3 Drone Swarm Firmware & Simulation

Bare-metal C++ drone swarm firmware and Python simulation for autonomous cooperative mine detection and marking using ESP-NOW mesh communication.

---

## Key Architecture

- **Target MCU**: ESP32-S3 (`esp32-s3-devkitc-1` / ESP32-S3-CAM)
- **Framework**: Arduino / ESP-IDF (no ROS2, no Linux companion computer)
- **Inter-Drone Comms**: Peer-to-peer decentralized ESP-NOW broadcast protocol
- **Hardware Isolation**: Pure C++ state machine ([src/state_machine.cpp](src/state_machine.cpp)) compileable and testable natively on host PCs without ESP32/Arduino dependencies.

---

## File Structure

```
.
├── include/
│   ├── config.h             # Drone configuration, thresholds, pinouts, and FOV optics
│   ├── flight_interface.h   # Flight controller telemetry & actuation interface
│   ├── led_marker.h         # Status LED & marker pattern interface
│   ├── state_machine.h      # Pure C++ flight state machine declarations
│   ├── swarm_comm.h         # Decentralized ESP-NOW claim/yield protocol
│   └── vision.h             # Camera pipeline, subpixel centroid & mine candidate
├── src/
│   ├── main.cpp             # 50Hz control loop & subsystem coordinator
│   ├── state_machine.cpp    # Deterministic state machine transition logic
│   ├── swarm_comm.cpp       # ESP-NOW mesh networking & claim contention
│   └── vision.cpp           # Camera frame processing & pixel-to-world transform
├── test/
│   └── test_state_machine.cpp # Native Unity unit test suite
├── sim/
│   └── swarm_sim.py         # Pure-Python multi-agent swarm simulator
├── scripts/
│   └── bringup_check.py     # Single-board hardware smoke test & bring-up script
└── platformio.ini           # PlatformIO project configuration
```

---

## Build & Test Commands

### 1. Run Native Host Unit Tests
```bash
pio test -e native
```

### 2. Build Production Firmware (ESP32-S3)
```bash
pio run -e esp32-s3-devkitc-1
```

### 3. Flash & Single-Board Bring-Up Smoke Test
```bash
python scripts/bringup_check.py --port COM3
```

### 4. Dual-Board Hardware Claim/Yield Test
```bash
# Flash Board 1 (DRONE_ID=1)
pio run -e esp32-s3-test-claim --upload-port COM3 -t upload

# Flash Board 2 (DRONE_ID=2, set in include/config.h)
pio run -e esp32-s3-test-claim --upload-port COM4 -t upload
```

### 5. Run Pure-Python Swarm Simulation
```bash
python sim/swarm_sim.py
```
