#pragma once

#include "backends/webcam/device_model.hpp"

#include <string>
#include <vector>

namespace labops::backends::webcam {

// Enumerates Linux webcam devices using V4L2 `VIDIOC_QUERYCAP`.
//
// Contract:
// - returns `true` on successful scan (including zero devices found)
// - returns `false` only for hard scan/setup errors
// - emits normalized `WebcamDeviceInfo` rows with deterministic ordering
// - each discovered device includes a best-effort `supported_controls` snapshot
//   (pixel formats, width/height, fps, exposure/gain/auto-exposure when exposed)
bool EnumerateV4l2Devices(std::vector<WebcamDeviceInfo>& devices, std::string& error);

} // namespace labops::backends::webcam
