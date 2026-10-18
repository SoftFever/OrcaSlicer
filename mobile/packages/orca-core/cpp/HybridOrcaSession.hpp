#pragma once

#include "HybridOrcaSessionSpec.hpp"

#include <OrcaCore/Session.hpp>

#include <cstdint>
#include <mutex>

namespace margelo::nitro::orca {

// One project. Wraps Slic3r::Mobile::Session; see src/mobile/core/Session.hpp.
//
// The façade is not thread-safe, so a mutex serializes every call. Promise-returning
// methods run on Nitro's thread pool holding that mutex; synchronous methods take it
// with try_lock and throw when a long operation holds it, which JS sees as a rejected
// call rather than a frozen UI thread. cancel() never takes the mutex.
class HybridOrcaSession final : public HybridOrcaSessionSpec {
public:
  HybridOrcaSession();
  ~HybridOrcaSession() override;

  // Properties
  double getId() override;
  bool getIsSliced() override;
  bool getIsBusy() override;

  // Presets
  std::shared_ptr<Promise<std::string>> loadPresets() override;
  std::shared_ptr<Promise<std::string>> reloadPresets() override;
  std::vector<PresetInfo> presets(PresetKind kind) override;
  std::string selectedPreset(PresetKind kind) override;
  bool selectPreset(PresetKind kind, const std::string& name) override;
  bool selectFilament(double extruder, const std::string& name) override;
  bool savePresetAs(PresetKind kind, const std::string& name) override;
  bool deletePreset(PresetKind kind, const std::string& name) override;
  std::string presetFile(PresetKind kind, const std::string& name) override;
  BedInfo bed() override;
  std::shared_ptr<Promise<ImportPresetsResult>> importPresets(const std::string& path, bool overwrite, bool asBundle) override;
  std::shared_ptr<Promise<std::string>> exportPresets(const std::vector<PresetRef>& presets, const std::string& outPath) override;

  // Cloud sync (preset side)
  void setCloudUser(const std::string& userId) override;
  std::string getCloudUser() override;
  std::vector<CloudPresetState> cloudDirtyPresets() override;
  std::string cloudPushPayload(PresetKind kind, const std::string& name) override;
  void cloudMarkSynced(PresetKind kind, const std::string& name, const std::string& settingId, double updatedTime,
                       const std::string& syncInfo) override;
  std::shared_ptr<Promise<CloudApplyResult>> cloudApplyPull(const std::string& upsertsJson) override;
  std::vector<PendingDelete> cloudPendingDeletes() override;
  void cloudConfirmDeleted(const std::string& settingId) override;

  // Options
  std::string option(const std::string& key) override;
  bool setOption(const std::string& key, const std::string& value) override;
  std::vector<std::string> modifiedOptions(PresetKind kind) override;
  void discardModifiedOptions(PresetKind kind) override;

  // Model
  std::shared_ptr<Promise<std::string>> saveProject(const std::string& path) override;
  std::shared_ptr<Promise<std::vector<PlateInfo>>> openProject(const std::string& path) override;
  std::shared_ptr<Promise<std::vector<ObjectInfo>>> importModel(const std::string& path) override;
  std::vector<ObjectInfo> objects() override;
  bool removeObject(double id) override;
  bool setTransform(double id, const Vec3& position, const Vec3& rotation, const Vec3& scale) override;
  ObjectInfo duplicateObject(double id) override;
  bool mirrorObject(double id, MirrorAxis axis) override;
  bool layFlat(double id) override;
  bool autoOrient(double id) override;
  std::vector<double> cutObject(double id, double z) override;
  std::shared_ptr<Promise<bool>> simplifyObject(double id, double targetTriangles) override;
  std::shared_ptr<Promise<MeshRepairResult>> repairObject(double id) override;
  PickHit pickAt(const Vec3& rayOrigin, const Vec3& rayDirection) override;
  void clear() override;
  std::shared_ptr<Promise<void>> arrange() override;

  // Plates
  double addPlate() override;
  double duplicatePlate() override;
  bool removePlate(double id) override;
  bool selectPlate(double id) override;
  double getCurrentPlate() override;
  std::vector<PlateInfo> plateList() override;

  // Multi-material
  double getExtruderCount() override;
  std::vector<std::string> selectedFilaments() override;
  double objectExtruder(double id) override;
  bool setObjectExtruder(double id, double extruder) override;

  // Undo / redo
  void takeSnapshot(const std::string& label) override;
  bool undo() override;
  bool redo() override;
  bool getCanUndo() override;
  bool getCanRedo() override;
  std::string undoLabel() override;
  std::string redoLabel() override;
  std::vector<SnapshotInfo> snapshotHistory() override;

  // Slicing
  std::shared_ptr<Promise<SliceResult>> slice(const std::function<void(const Progress&)>& onProgress) override;
  void cancel() override;
  std::shared_ptr<Promise<std::string>> exportGCode(const std::string& path) override;
  SliceStatistics statistics() override;

private:
  // Locks for a synchronous call; throws if a long operation holds the session.
  std::unique_lock<std::mutex> lockNow();

  Slic3r::Mobile::Session _session;
  std::mutex _mutex;
  uint64_t _id = 0;
};

} // namespace margelo::nitro::orca
