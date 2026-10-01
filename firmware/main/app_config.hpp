#pragma once

#include <cstdint>

namespace app_config {

// Set true for UI development without the toothbrush.
constexpr bool kUseMockBrush = false;

// The Mac performs direct BLE/GATT and the official Comino classification;
// the ESP32 receives the resulting state over the local network.
constexpr bool kUseMacServer = true;
constexpr const char* kMacServerUrl = "http://192.168.0.139:8765/api/display-state";
constexpr const char* kMacResetUrl = "http://192.168.0.139:8765/api/reset";
constexpr const char* kMacIngestUrl = "http://192.168.0.139:8765/api/ingest";

// Passive advertisements expose the timed pacer sector, but not a fully
// decoded physical 3-surface mouth position. Keep this false for real BLE.
// Turning it on makes the UI generate demo mouth coverage from elapsed time.
constexpr bool kDemoCoverageFromTimer = false;

// Continuous scan window. Oral-B data is small and regular, so passive scan
// is sufficient for the first bring-up.
constexpr uint32_t kScanWindowMs = 30000;

// UI refresh period.
// The AMOLED flush is relatively expensive; 500 ms keeps the UI responsive
// without starving BLE and the LVGL watchdog on the ESP32-S3.
constexpr uint32_t kUiRefreshMs = 500;

// Display brightness once the BSP has initialized.
constexpr int kDisplayBrightnessPercent = 80;

} // namespace app_config
