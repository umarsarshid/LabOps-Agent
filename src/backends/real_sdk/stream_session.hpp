#pragma once

#include <cstdint>
#include <mutex>
#include <string>

namespace labops::backends::real_sdk {

// Stream-session guard for real backend acquisition lifecycle.
//
// Why this exists:
// - real camera SDKs usually require balanced acquisition start/stop calls
// - run orchestration has many early-return paths (errors, pauses, interrupts)
// - centralizing lifecycle handling avoids leaked running sessions between runs
class StreamSession {
public:
  StreamSession() = default;
  ~StreamSession();

  StreamSession(const StreamSession&) = delete;
  StreamSession& operator=(const StreamSession&) = delete;
  StreamSession(StreamSession&&) = delete;
  StreamSession& operator=(StreamSession&&) = delete;

  // Starts acquisition for this backend session.
  bool Start(std::string& error);

  // Stops acquisition safely. Idempotent: calling Stop repeatedly succeeds.
  bool Stop(std::string& error);

  bool running() const;

  struct Snapshot {
    bool running = false;
    std::uint64_t start_calls = 0;
    std::uint64_t stop_calls = 0;
  };

  Snapshot DebugSnapshot() const;

private:
  bool BeginAcquisition(std::string& error);
  bool EndAcquisition(std::string& error);

  mutable std::mutex mu_;
  bool running_ = false;
  std::uint64_t start_calls_ = 0;
  std::uint64_t stop_calls_ = 0;
};

} // namespace labops::backends::real_sdk
