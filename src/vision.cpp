#include "vision.h"
#include <cmath>
#include <cstring>
#include <algorithm>

#ifndef NATIVE_BUILD
#include <Arduino.h>
#include "esp_camera.h"

// Camera state & persistence tracker
static bool s_camera_initialized = false;
static uint8_t s_consecutive_frames = 0;
static float s_last_world_x = 0.0f;
static float s_last_world_y = 0.0f;
static uint32_t s_accum_confidence = 0;

// ============================================================================
// CAMERA INITIALIZATION
// ============================================================================
bool initCamera() {
    camera_config_t config;

    // ========================================================================
    // TODO: Camera Pin Mapping (Board-Specific, configure for actual hardware)
    // ========================================================================
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer   = LEDC_TIMER_0;
    config.pin_d0       = Y2_GPIO_NUM;
    config.pin_d1       = Y3_GPIO_NUM;
    config.pin_d2       = Y4_GPIO_NUM;
    config.pin_d3       = Y5_GPIO_NUM;
    config.pin_d4       = Y6_GPIO_NUM;
    config.pin_d5       = Y7_GPIO_NUM;
    config.pin_d6       = Y8_GPIO_NUM;
    config.pin_d7       = Y9_GPIO_NUM;
    config.pin_xclk     = XCLK_GPIO_NUM;
    config.pin_pclk     = PCLK_GPIO_NUM;
    config.pin_vsync    = VSYNC_GPIO_NUM;
    config.pin_href     = HREF_GPIO_NUM;
    config.pin_sccb_sda = SIOD_GPIO_NUM;
    config.pin_sccb_scl = SIOC_GPIO_NUM;
    config.pin_pwdn     = PWDN_GPIO_NUM;
    config.pin_reset    = RESET_GPIO_NUM;
    // ========================================================================

    config.xclk_freq_hz = 20000000;
    config.frame_size   = FRAMESIZE_QVGA;   // 320x240 (FRAME_WIDTH x FRAME_HEIGHT)
    config.pixel_format = PIXFORMAT_RGB565;
    config.grab_mode    = CAMERA_GRAB_LATEST;
    config.fb_location  = CAMERA_FB_IN_PSRAM;
    config.jpeg_quality = 12;
    config.fb_count     = 2;

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        DEBUG_PRINTF("[BRINGUP][FAIL] Camera init failed with error 0x%x\n", err);
        s_camera_initialized = false;
        return false;
    }

    sensor_t* s = esp_camera_sensor_get();
    if (s != nullptr) {
        s->set_vflip(s, 1);
        s->set_hmirror(s, 0);
    }

    s_camera_initialized = true;
    DEBUG_PRINTLN("[BRINGUP][PASS] Checkpoint 1: Camera initialized successfully (initCamera() == true).");
    return true;
}

// ============================================================================
// BLOB DETECTION & HSV SEGMENTATION (Internal Pipeline Stage)
// ============================================================================
static bool findBestBlob(const camera_fb_t* fb, DetectedBlob* out_blob) {
    if (!fb || !fb->buf || !out_blob) {
        return false;
    }

    out_blob->found = false;
    out_blob->centroid_x = 0.0f;
    out_blob->centroid_y = 0.0f;
    out_blob->area_px = 0.0f;
    out_blob->circularity = 0.0f;

    // ========================================================================
    // TODO: HSV mask thresholds and findBestBlob() internals ported from
    // Python/OpenCV prototype (tuned for mine color & reflectance spectrum).
    //
    // Baseline RGB565 color filtering implementation below for initial testing:
    // ========================================================================
    const uint16_t* pixels = reinterpret_cast<const uint16_t*>(fb->buf);
    int width = fb->width;
    int height = fb->height;
    const int step = 2; // Fast stride sampling

    uint32_t sum_x = 0;
    uint32_t sum_y = 0;
    uint32_t match_count = 0;
    uint32_t perimeter_estimate = 0;

    int min_x = width, max_x = 0;
    int min_y = height, max_y = 0;

    for (int y = 0; y < height; y += step) {
        for (int x = 0; x < width; x += step) {
            uint16_t p = pixels[y * width + x];

            // Extract 8-bit RGB components from RGB565
            uint8_t r = ((p >> 11) & 0x1F) << 3;
            uint8_t g = ((p >> 5) & 0x3F) << 2;
            uint8_t b = (p & 0x1F) << 3;

            // Simple mine color threshold placeholder (e.g. bright orange / metallic reflection)
            if (r > 140 && g < 100 && b < 100) {
                sum_x += x;
                sum_y += y;
                match_count++;

                if (x < min_x) min_x = x;
                if (x > max_x) max_x = x;
                if (y < min_y) min_y = y;
                if (y > max_y) max_y = y;
            }
        }
    }

    if (match_count > 15) {
        out_blob->found = true;
        // Sub-pixel centroid estimation
        out_blob->centroid_x = static_cast<float>(sum_x) / static_cast<float>(match_count);
        out_blob->centroid_y = static_cast<float>(sum_y) / static_cast<float>(match_count);
        out_blob->area_px = static_cast<float>(match_count * (step * step));

        // Circularity approximation: 4 * pi * Area / Perimeter^2
        float bbox_w = static_cast<float>(std::max(1, max_x - min_x + 1));
        float bbox_h = static_cast<float>(std::max(1, max_y - min_y + 1));
        float perimeter = 2.0f * (bbox_w + bbox_h);
        out_blob->circularity = (4.0f * M_PI * out_blob->area_px) / (perimeter * perimeter);
        out_blob->circularity = std::max(0.0f, std::min(1.0f, out_blob->circularity));
        return true;
    }

    return false;
}

// ============================================================================
// MINE DETECTION PIPELINE
// ============================================================================
MineCandidate scanForMine(float drone_x, float drone_y, float altitude) {
    MineCandidate candidate = {};
    candidate.valid = false;
    candidate.world_x = drone_x;
    candidate.world_y = drone_y;
    candidate.confidence = 0;

    if (!s_camera_initialized) {
        return candidate;
    }

    // 1. Capture Camera Frame
    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) {
        return candidate;
    }

    // 2. Run Blob Detection (HSV threshold -> Blob Centroid & Geometry)
    DetectedBlob blob = {};
    bool blob_found = findBestBlob(fb, &blob);

    float width = static_cast<float>(fb->width);
    float height = static_cast<float>(fb->height);

    if (blob_found) {
        // 3. Circularity Filter (>= MIN_CIRCULARITY)
        if (blob.circularity >= MIN_CIRCULARITY) {
            // 4. Confidence Score (0-100, weighted: circularity up to 60 pts + normalized pixel area up to 40 pts)
            float circularity_pts = std::min(60.0f, (blob.circularity / 1.0f) * 60.0f);
            float area_norm = std::min(1.0f, blob.area_px / MAX_EXPECTED_BLOB_AREA_PX);
            float area_pts = area_norm * 40.0f;
            uint8_t raw_confidence = static_cast<uint8_t>(std::min(100.0f, circularity_pts + area_pts));

            // 5. Pixel-to-World Transform
            float world_x = drone_x + altitude * std::tan((blob.centroid_x - width / 2.0f) * (H_FOV / width));
            float world_y = drone_y + altitude * std::tan((blob.centroid_y - height / 2.0f) * (V_FOV / height));

            // 6. Temporal Persistence Check (must match within MAX_MINE_DRIFT_M across TEMPORAL_PERSIST_FRAMES)
            float dx = world_x - s_last_world_x;
            float dy = world_y - s_last_world_y;
            float drift_dist = std::sqrt(dx * dx + dy * dy);

            if (s_consecutive_frames > 0 && drift_dist <= MAX_MINE_DRIFT_M) {
                s_consecutive_frames++;
                // Smooth coordinates with running average
                s_last_world_x = 0.7f * s_last_world_x + 0.3f * world_x;
                s_last_world_y = 0.7f * s_last_world_y + 0.3f * world_y;
                s_accum_confidence = (s_accum_confidence * (s_consecutive_frames - 1) + raw_confidence) / s_consecutive_frames;
            } else {
                // Reset persistence with new candidate
                s_consecutive_frames = 1;
                s_last_world_x = world_x;
                s_last_world_y = world_y;
                s_accum_confidence = raw_confidence;
            }

            candidate.world_x = s_last_world_x;
            candidate.world_y = s_last_world_y;
            candidate.confidence = static_cast<uint8_t>(s_accum_confidence);

            // Report valid only if temporally persistent AND confidence meets reporting threshold
            if (s_consecutive_frames >= TEMPORAL_PERSIST_FRAMES &&
                candidate.confidence >= CONFIDENCE_MIN_TO_REPORT) {
                candidate.valid = true;
            } else {
                candidate.valid = false;
            }
        } else {
            // Failed circularity filter - decay persistence
            if (s_consecutive_frames > 0) s_consecutive_frames--;
        }
    } else {
        // No blob found - decay persistence
        if (s_consecutive_frames > 0) s_consecutive_frames--;
    }

    esp_camera_fb_return(fb);
    return candidate;
}

// ============================================================================
// GESTURE RECOGNITION (Independent Per-Drone Watcher)
// ============================================================================
GestureState watchForGesture() {
    if (!s_camera_initialized) {
        return GESTURE_NONE;
    }

    // ========================================================================
    // TODO: Gesture-detection ROI & pattern recognition logic ported from
    // Python/OpenCV prototype.
    //
    // Applies GESTURE_START_CONFIDENCE and GESTURE_STOP_CONFIDENCE thresholds
    // against scored candidate gestures.
    // ========================================================================
    uint8_t start_confidence = 0; // Scored confidence (0-100)
    uint8_t stop_confidence  = 0; // Scored confidence (0-100)

    if (start_confidence >= GESTURE_START_CONFIDENCE) {
        return GESTURE_START;
    } else if (stop_confidence >= GESTURE_STOP_CONFIDENCE) {
        return GESTURE_STOP;
    }

    return GESTURE_NONE;
}

#else

// Native Stub Implementation for host platform testing
bool initCamera() { return true; }
MineCandidate scanForMine(float drone_x, float drone_y, float altitude) {
    MineCandidate c = {};
    c.valid = false;
    c.world_x = drone_x;
    c.world_y = drone_y;
    c.confidence = 0;
    return c;
}
GestureState watchForGesture() { return GESTURE_NONE; }

#endif
