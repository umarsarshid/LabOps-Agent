#include "backends/real_sdk/stream_session.hpp"

#include <mutex>

namespace labops::backends::real_sdk {

StreamSession::~StreamSession() {
  // Destructors cannot surface errors; best-effort stop prevents leaked active
  // acquisition state when control flow exits early.
  std::string stop_error;
  (void)Stop(stop_error);
}

bool StreamSession::Start(std::string& error) {
  std::lock_guard<std::mutex> lock(mu_);
  if (running_) {
    error = "real backend stream session is already running";
    return false;
  }
  if (!BeginAcquisition(error)) {
    return false;
  }
  running_ = true;
  ++start_calls_;
  error.clear();
  return true;
}

bool StreamSession::Stop(std::string& error) {
  std::lock_guard<std::mutex> lock(mu_);
  if (!running_) {
    // Idempotent no-op so callers can safely stop in both success and error
    // paths without double-stop failures.
    error.clear();
    return true;
  }
  if (!EndAcquisition(error)) {
    return false;
  }
  running_ = false;
  ++stop_calls_;
  error.clear();
  return true;
}

bool StreamSession::running() const {
  std::lock_guard<std::mutex> lock(mu_);
  return running_;
}

StreamSession::Snapshot StreamSession::DebugSnapshot() const {
  std::lock_guard<std::mutex> lock(mu_);
  return Snapshot{
      .running = running_,
      .start_calls = start_calls_,
      .stop_calls = stop_calls_,
  };
}

bool StreamSession::BeginAcquisition(std::string& error) {
  // Placeholder for vendor SDK acquisition start.
  error.clear();
  return true;
}

bool StreamSession::EndAcquisition(std::string& error) {
  // Placeholder for vendor SDK acquisition stop.
  error.clear();
  return true;
}

} // namespace labops::backends::real_sdk
