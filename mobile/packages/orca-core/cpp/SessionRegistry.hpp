#pragma once

#include <OrcaCore/Session.hpp>

#include <cstdint>
#include <functional>
#include <mutex>

namespace margelo::nitro::orca {

// Lets native views reach a session by id without passing Hybrid Objects through
// Swift. HybridOrcaSession registers itself on construction; the viewport looks the
// session up by the id it received as a prop and reads geometry under the session's
// own mutex, so a running slice and a redraw never touch the façade at the same time.
class SessionRegistry final {
public:
  // Registers a session and the mutex that guards it. Returns the id (never 0).
  static uint64_t add(Slic3r::Mobile::Session* session, std::mutex* mutex);
  static void remove(uint64_t id);

  // Runs fn with the session locked. Returns false without calling fn when the id is
  // unknown or a long operation currently holds the session.
  static bool with(uint64_t id, const std::function<void(Slic3r::Mobile::Session&)>& fn);
};

} // namespace margelo::nitro::orca
