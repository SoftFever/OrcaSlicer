#include "HybridOrcaCore.hpp"

#include "HybridOrcaSession.hpp"

#include <OrcaCore/OrcaMobile.hpp>
#include <OrcaCore/Session.hpp>

namespace margelo::nitro::orca {

std::string HybridOrcaCore::getVersion() {
  return Slic3r::Mobile::core_version();
}

void HybridOrcaCore::initialize(const std::string& resourcesDir, const std::string& dataDir) {
  Slic3r::Mobile::set_directories(resourcesDir, dataDir);
}

std::string HybridOrcaCore::optionDefinitionsJson() {
  return Slic3r::Mobile::Session::option_definitions_json();
}

std::shared_ptr<HybridOrcaSessionSpec> HybridOrcaCore::createSession() {
  return std::make_shared<HybridOrcaSession>();
}

} // namespace margelo::nitro::orca
