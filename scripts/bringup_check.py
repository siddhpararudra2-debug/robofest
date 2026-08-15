#!/usr/bin/env python3
"""
ESP32-S3-CAM Single-Board Bring-Up & Smoke Test Script
======================================================
Automates flashing the ESP32-S3 board and monitoring serial output for the 3
critical bring-up checkpoints:
  1. Camera Subsystem Init (initCamera() == true)
  2. Raw Frame Capture & Resolution Validation (320x240 @ QVGA)
  3. Standalone ESP-NOW Radio Mesh Init (Broadcast peer & channel readiness)

Usage:
  python scripts/bringup_check.py [--port COM_PORT] [--baud 115200]
"""

import sys
import time
import argparse
import subprocess

try:
    import serial
    import serial.tools.list_ports
except ImportError:
    serial = None

def auto_detect_port():
    if not serial:
        return None
    ports = list(serial.tools.list_ports.comports())
    for p in ports:
        desc = (p.description or "").lower()
        if "usb" in desc or "uart" in desc or "cp210" in desc or "ch340" in desc or "jtag" in desc:
            return p.device
    return ports[0].device if ports else None

def flash_board(port=None):
    print("\n[STEP 1/2] Building and flashing ESP32-S3 firmware via PlatformIO...")
    cmd = ["pio", "run", "-e", "esp32-s3-devkitc-1", "-t", "upload"]
    if port:
        cmd.extend(["--upload-port", port])

    res = subprocess.run(cmd)
    if res.returncode != 0:
        print("[ERROR] Firmware flash failed. Please check board connection and boot mode.")
        sys.exit(1)
    print("[SUCCESS] Flashing completed successfully.\n")

def monitor_bringup_checkpoints(port, baud=115200, timeout=12):
    if not serial:
        print("[WARN] pyserial not installed. Please monitor via 'pio device monitor' manually.")
        return

    print(f"[STEP 2/2] Opening serial monitor on {port} @ {baud} baud (Timeout: {timeout}s)...")

    checkpoints = {
        "checkpoint_1_cam_init": False,
        "checkpoint_2_frame_capture": False,
        "checkpoint_2_resolution_match": False,
        "checkpoint_3_espnow_init": False,
    }

    try:
        ser = serial.Serial(port, baudrate=baud, timeout=1)
        ser.dtr = False
        ser.rts = False
        time.sleep(0.5)
        ser.dtr = True
        ser.rts = True
    except Exception as e:
        print(f"[ERROR] Could not open serial port {port}: {e}")
        return

    start_time = time.time()
    print("Listening for boot diagnostics...\n" + "-" * 60)

    try:
        while time.time() - start_time < timeout:
            line = ser.readline().decode('utf-8', errors='ignore').strip()
            if line:
                print(f" > {line}")

                if "Checkpoint 1: Camera initialized successfully" in line:
                    checkpoints["checkpoint_1_cam_init"] = True
                if "Checkpoint 2: Frame captured" in line:
                    checkpoints["checkpoint_2_frame_capture"] = True
                if "Frame resolution exactly matches config specification" in line:
                    checkpoints["checkpoint_2_resolution_match"] = True
                if "Checkpoint 3: ESP-NOW initialized" in line:
                    checkpoints["checkpoint_3_espnow_init"] = True

                # Check if all passed
                if all(checkpoints.values()):
                    print("-" * 60)
                    print("[INFO] All 3 bring-up checkpoints captured!")
                    break

    except KeyboardInterrupt:
        print("\nMonitoring stopped by user.")
    finally:
        ser.close()

    # Print Summary Report
    print("\n==================================================")
    print("      SINGLE-BOARD BRING-UP CHECKLIST SUMMARY     ")
    print("==================================================")
    
    c1 = "[PASS]" if checkpoints["checkpoint_1_cam_init"] else "[FAIL]"
    c2a = "[PASS]" if checkpoints["checkpoint_2_frame_capture"] else "[FAIL]"
    c2b = "[PASS]" if checkpoints["checkpoint_2_resolution_match"] else "[FAIL]"
    c3 = "[PASS]" if checkpoints["checkpoint_3_espnow_init"] else "[FAIL]"

    print(f" {c1} Checkpoint 1: Camera Subsystem Init (initCamera() == true)")
    print(f" {c2a} Checkpoint 2a: Raw Frame Acquisition (esp_camera_fb_get())")
    print(f" {c2b} Checkpoint 2b: Resolution Verification (320 x 240 @ QVGA)")
    print(f" {c3} Checkpoint 3: ESP-NOW Mesh Standalone Init (Channel 6)")
    print("==================================================")

    if all(checkpoints.values()):
        print("RESULT: BOARD HARDWARE BRING-UP SUCCESSFUL! READY FOR SWARM DEPLOYMENT.\n")
    else:
        print("RESULT: ONE OR MORE CHECKPOINTS FAILED. CHECK HARDWARE PINS / VOLTAGE.\n")

def main():
    parser = argparse.ArgumentParser(description="ESP32-S3-CAM Bring-Up Smoke Test")
    parser.add_argument("--port", type=str, default=None, help="Serial port (e.g. COM3 or /dev/ttyUSB0)")
    parser.add_argument("--baud", type=int, default=115200, help="Serial baud rate (default 115200)")
    parser.add_argument("--skip-flash", action="store_true", help="Skip flashing step, only monitor serial")
    args = parser.parse_args()

    port = args.port or auto_detect_port()
    if not port:
        print("[WARN] No active COM port detected automatically. Please specify with --port COMx")

    if not args.skip_flash:
        flash_board(port)

    if port:
        monitor_bringup_checkpoints(port, args.baud)

if __name__ == "__main__":
    main()
