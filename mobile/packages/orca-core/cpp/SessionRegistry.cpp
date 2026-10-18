#include "SessionRegistry.hpp"

#include <unordered_map>

namespace margelo::nitro::orca {

namespace {

struct Entry {
  Slic3r::Mobile::Session* session;
  std::mutex* mutex;
};

std::mutex& registryMutex() {
  static std::mutex m;
  return m;
}

std::unordered_map<uint64_t, Entry>& entries() {
  static std::unordered_map<uint64_t, Entry> map;
  return map;
}

} // namespace

uint64_t SessionRegistry::add(Slic3r::Mobile::Session* session, std::mutex* mutex) {
  static uint64_t next = 1;
  std::lock_guard<std::mutex> lock(registryMutex());
  const uint64_t id = next++;
  entries()[id] = Entry{session, mutex};
  return id;
}

void SessionRegistry::remove(uint64_t id) {
  std::lock_guard<std::mutex> lock(registryMutex());
  entries().erase(id);
}

bool SessionRegistry::with(uint64_t id, const std::function<void(Slic3r::Mobile::Session&)>& fn) {
  Entry entry{nullptr, nullptr};
  {
    std::lock_guard<std::mutex> lock(registryMutex());
    auto it = entries().find(id);
    if (it == entries().end()) {
      return false;
    }
    entry = it->second;
  }
  // The session mutex is taken while the registry lock is released: a slice may hold it
  // for minutes, and HybridOrcaSession::~HybridOrcaSession removes the entry first and
  // only then destroys the session, so the pointers stay valid while we hold the mutex.
  std::unique_lock<std::mutex> lock(*entry.mutex, std::try_to_lock);
  if (!lock.owns_lock()) {
    return false;
  }
  fn(*entry.session);
  return true;
}

} // namespace margelo::nitro::orca
