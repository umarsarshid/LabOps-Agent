#include "backends/webcam/opencv_bootstrap.hpp"

#include <string>

#if LABOPS_ENABLE_WEBCAM_OPENCV
#include <opencv2/core/version.hpp>
#endif

namespace labops::backends::webcam {

bool IsOpenCvBootstrapEnabled() {
#if LABOPS_ENABLE_WEBCAM_OPENCV
  return true;
#else
  return false;
#endif
}

const char* OpenCvBootstrapStatusText() {
#if LABOPS_ENABLE_WEBCAM_OPENCV
  return "enabled";
#else
  return "disabled";
#endif
}

std::string OpenCvBootstrapDetail() {
#if LABOPS_ENABLE_WEBCAM_OPENCV
  return std::string("OpenCV bootstrap compiled (OpenCV ") + CV_VERSION + ")";
#else
  return "OpenCV bootstrap not compiled";
#endif
}

} // namespace labops::backends::webcam
