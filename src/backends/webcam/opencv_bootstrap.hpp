#pragma once

#include <string>

namespace labops::backends::webcam {

// Reports whether the webcam OpenCV bootstrap path was compiled into the
// current binary.
bool IsOpenCvBootstrapEnabled();

// Short machine-friendly status string: `enabled` or `disabled`.
const char* OpenCvBootstrapStatusText();

// Human-readable status detail intended for `dump_config()` evidence.
std::string OpenCvBootstrapDetail();

} // namespace labops::backends::webcam
