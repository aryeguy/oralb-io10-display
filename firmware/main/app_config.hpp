#pragma once

#include <cstdint>

namespace app_config {

// Set true for UI development without the toothbrush.
constexpr bool kUseMockBrush = false;

// Passive advertisements expose the timed pacer sector, but not a fully
// decoded physical 3-surface mouth position. Keep this false for real BLE.
// Turning it on makes the UI generate demo mouth coverage from elapsed time.
constexpr bool kDemoCoverageFromTimer = false;

// Continuous scan window. Oral-B data is small and regular, so passive scan
// is sufficient for the first bring-up.
constexpr uint32_t kScanWindowMs = 30000;

// UI refresh period.
constexpr uint32_t kUiRefreshMs = 50;

// Display brightness once the BSP has initialized.
constexpr int kDisplayBrightnessPercent = 80;

} // namespace app_config
