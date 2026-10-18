#pragma once

#include "HybridOrcaCoreSpec.hpp"

namespace margelo::nitro::orca {

// The core's entry point: version, directories, option metadata and session creation.
// Autolinked by nitro.json, so it needs the default constructor.
class HybridOrcaCore final : public HybridOrcaCoreSpec {
public:
  HybridOrcaCore() : HybridObject(TAG) {}

  std::string getVersion() override;
  void initialize(const std::string& resourcesDir, const std::string& dataDir) override;
  std::string optionDefinitionsJson() override;
  std::shared_ptr<HybridOrcaSessionSpec> createSession() override;
};

} // namespace margelo::nitro::orca
