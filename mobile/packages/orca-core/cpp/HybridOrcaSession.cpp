#include "HybridOrcaSession.hpp"

#include "SessionRegistry.hpp"

#include <NitroModules/Promise.hpp>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <stdexcept>
#include <thread>
#include <unistd.h>
#include <utility>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#if TARGET_OS_IPHONE
// os_proc_available_memory() is the headroom left before the kernel's per-process
// limit (jetsam), the figure that decides whether the app is about to be killed.
#include <dispatch/dispatch.h>
#include <os/proc.h>
#define ORCA_MEMORY_WATCHDOG 1
#endif
#endif

namespace margelo::nitro::orca {

namespace {

using Slic3r::Mobile::Session;

Slic3r::Mobile::PresetKind toCore(PresetKind kind) {
  switch (kind) {
    case PresetKind::PRINTER:
      return Slic3r::Mobile::PresetKind::Printer;
    case PresetKind::FILAMENT:
      return Slic3r::Mobile::PresetKind::Filament;
    default:
      return Slic3r::Mobile::PresetKind::Process;
  }
}

Vec3 toJS(const Slic3r::Mobile::Vec3& v) {
  return Vec3(v[0], v[1], v[2]);
}

Slic3r::Mobile::Vec3 toCore(const Vec3& v) {
  return {v.x, v.y, v.z};
}

PresetKind toJS(Slic3r::Mobile::PresetKind kind) {
  switch (kind) {
    case Slic3r::Mobile::PresetKind::Printer:
      return PresetKind::PRINTER;
    case Slic3r::Mobile::PresetKind::Filament:
      return PresetKind::FILAMENT;
    default:
      return PresetKind::PROCESS;
  }
}

SkipReason toJSReason(const std::string& reason) {
  if (reason == "system_name") return SkipReason::SYSTEM_NAME;
  if (reason == "missing_parent") return SkipReason::MISSING_PARENT;
  if (reason == "exists") return SkipReason::EXISTS;
  return SkipReason::INVALID;
}

std::vector<SkippedPreset> toJS(const std::vector<Slic3r::Mobile::SkippedPreset>& skipped) {
  std::vector<SkippedPreset> out;
  out.reserve(skipped.size());
  for (const auto& s : skipped) {
    out.push_back(SkippedPreset(s.name, toJSReason(s.reason), s.inherits, s.vendor_hint));
  }
  return out;
}

ImportPresetsResult toJS(const Slic3r::Mobile::ImportPresetsResult& r) {
  std::vector<PresetRef> imported;
  imported.reserve(r.imported.size());
  for (const auto& p : r.imported) {
    imported.push_back(PresetRef(toJS(p.kind), p.name));
  }
  return ImportPresetsResult(std::move(imported), toJS(r.skipped), r.conflicts);
}

PresetInfo toJS(const Slic3r::Mobile::PresetInfo& p) {
  return PresetInfo(p.name, p.vendor, p.is_system, p.is_user, p.is_visible, p.is_compatible, p.is_selected);
}

ObjectInfo toJS(const Slic3r::Mobile::ObjectInfo& o) {
  return ObjectInfo(static_cast<double>(o.id), o.name, toJS(o.position), toJS(o.rotation), toJS(o.scale), toJS(o.size),
                    static_cast<double>(o.triangles), static_cast<double>(o.open_edges));
}

std::vector<ObjectInfo> toJS(const std::vector<Slic3r::Mobile::ObjectInfo>& objects) {
  std::vector<ObjectInfo> out;
  out.reserve(objects.size());
  for (const auto& o : objects) {
    out.push_back(toJS(o));
  }
  return out;
}

std::vector<PresetInfo> toJS(const std::vector<Slic3r::Mobile::PresetInfo>& presets) {
  std::vector<PresetInfo> out;
  out.reserve(presets.size());
  for (const auto& p : presets) {
    out.push_back(toJS(p));
  }
  return out;
}

SliceOutcome toJS(Slic3r::Mobile::SliceOutcome outcome) {
  switch (outcome) {
    case Slic3r::Mobile::SliceOutcome::Finished:
      return SliceOutcome::FINISHED;
    case Slic3r::Mobile::SliceOutcome::Cancelled:
      return SliceOutcome::CANCELLED;
    case Slic3r::Mobile::SliceOutcome::OutOfMemory:
      return SliceOutcome::OUTOFMEMORY;
    default:
      return SliceOutcome::FAILED;
  }
}

MeshRepairResult toJS(const Session::MeshRepairResult& r) {
  return MeshRepairResult(r.changed, static_cast<double>(r.open_edges_before), static_cast<double>(r.open_edges_after),
                          static_cast<double>(r.vertices_merged), static_cast<double>(r.degenerate_faces),
                          static_cast<double>(r.faces_removed), static_cast<double>(r.faces_reversed),
                          static_cast<double>(r.edges_fixed));
}

// How many TBB workers a slice may use on this device. libslic3r's peak memory grows
// with the worker count (each worker holds its own layer intermediates), and the
// desktop's "every core" default assumes tens of gigabytes. Roughly one worker per
// 1.5 GB of physical memory: 2 on a 4 GB phone, 5 on an 8 GB one, the full core count
// on a 16 GB iPad.
unsigned sliceThreadLimit() {
  const long pages = sysconf(_SC_PHYS_PAGES);
  const long pageSize = sysconf(_SC_PAGESIZE);
  const unsigned cores = std::max(1u, std::thread::hardware_concurrency());
  if (pages <= 0 || pageSize <= 0) return cores;
  const double gib = static_cast<double>(pages) * static_cast<double>(pageSize) / (1024.0 * 1024.0 * 1024.0);
  const auto byMemory = static_cast<unsigned>(gib / 1.5);
  return std::clamp(byMemory, 2u, cores);
}

// Watches the process's memory headroom for the lifetime of one slice and turns an
// imminent kill into a cancellation. iOS does not fail malloc; it terminates the
// process when it crosses its limit, so nothing inside libslic3r can catch that. Two
// signals, either of which cancels the slice with the out-of-memory outcome:
//   - a poller reading os_proc_available_memory() every 150 ms, tripping below a
//     headroom chosen so one burst of layer allocations between two polls still fits;
//   - the kernel's critical memory-pressure notification, which arrives shortly before
//     the kill and can land between two polls.
// Both call into the Session only through cancel_out_of_memory(), which is safe from
// any thread. The pressure source's context is the Session, which outlives this
// object, so a late event after the slice does no harm: the next slice() resets the
// flag.
class MemoryWatchdog {
 public:
  explicit MemoryWatchdog(Session& session) : _session(session) {
#if ORCA_MEMORY_WATCHDOG
    _source = dispatch_source_create(DISPATCH_SOURCE_TYPE_MEMORYPRESSURE, 0, DISPATCH_MEMORYPRESSURE_CRITICAL,
                                     dispatch_get_global_queue(QOS_CLASS_UTILITY, 0));
    if (_source != nullptr) {
      dispatch_set_context(_source, &_session);
      dispatch_source_set_event_handler_f(_source, [](void* context) {
        static_cast<Session*>(context)->cancel_out_of_memory();
      });
      dispatch_resume(_source);
    }
    _poller = std::thread([this] {
      std::unique_lock<std::mutex> lock(_mutex);
      while (!_cv.wait_for(lock, std::chrono::milliseconds(150), [this] { return _stop; })) {
        // 0 means the figure is unavailable (simulator, macOS); never trip on it.
        const size_t available = os_proc_available_memory();
        if (available > 0 && available < kCriticalHeadroom) {
          _session.cancel_out_of_memory();
          return;
        }
      }
    });
#endif
  }

  ~MemoryWatchdog() {
#if ORCA_MEMORY_WATCHDOG
    {
      std::lock_guard<std::mutex> lock(_mutex);
      _stop = true;
    }
    _cv.notify_all();
    if (_poller.joinable()) _poller.join();
    if (_source != nullptr) {
      dispatch_source_cancel(_source);
      dispatch_release(_source);
    }
#endif
  }

  MemoryWatchdog(const MemoryWatchdog&) = delete;
  MemoryWatchdog& operator=(const MemoryWatchdog&) = delete;

 private:
  static constexpr size_t kCriticalHeadroom = 256u * 1024u * 1024u;

  Session& _session;
#if ORCA_MEMORY_WATCHDOG
  dispatch_source_t _source = nullptr;
  std::thread _poller;
  std::mutex _mutex;
  std::condition_variable _cv;
  bool _stop = false;
#endif
};

SliceResult toJS(const Slic3r::Mobile::SliceResult& r) {
  std::vector<SliceWarning> warnings;
  warnings.reserve(r.warnings.size());
  for (const auto& w : r.warnings) {
    warnings.push_back(SliceWarning(w.text, w.critical));
  }
  return SliceResult(toJS(r.outcome), r.error, std::move(warnings));
}

} // namespace

HybridOrcaSession::HybridOrcaSession() : HybridObject(TAG) {
  _session.set_thread_limit(sliceThreadLimit());
  _id = SessionRegistry::add(&_session, &_mutex);
}

HybridOrcaSession::~HybridOrcaSession() {
  // Unregister first, then wait for any reader or long operation before the members die.
  SessionRegistry::remove(_id);
  std::lock_guard<std::mutex> lock(_mutex);
}

double HybridOrcaSession::getId() {
  return static_cast<double>(_id);
}

std::unique_lock<std::mutex> HybridOrcaSession::lockNow() {
  std::unique_lock<std::mutex> lock(_mutex, std::try_to_lock);
  if (!lock.owns_lock()) {
    throw std::runtime_error("OrcaSession is busy; wait for the pending operation or cancel it");
  }
  return lock;
}

bool HybridOrcaSession::getIsSliced() {
  auto lock = lockNow();
  return _session.is_sliced();
}

bool HybridOrcaSession::getIsBusy() {
  std::unique_lock<std::mutex> lock(_mutex, std::try_to_lock);
  return !lock.owns_lock();
}

std::shared_ptr<Promise<std::string>> HybridOrcaSession::loadPresets() {
  return Promise<std::string>::async([this]() -> std::string {
    std::lock_guard<std::mutex> lock(_mutex);
    return _session.load_presets();
  });
}

std::shared_ptr<Promise<std::string>> HybridOrcaSession::reloadPresets() {
  return Promise<std::string>::async([this]() -> std::string {
    std::lock_guard<std::mutex> lock(_mutex);
    return _session.reload_presets();
  });
}

std::vector<PresetInfo> HybridOrcaSession::presets(PresetKind kind) {
  auto lock = lockNow();
  return toJS(_session.presets(toCore(kind)));
}

std::string HybridOrcaSession::selectedPreset(PresetKind kind) {
  auto lock = lockNow();
  return _session.selected_preset(toCore(kind));
}

bool HybridOrcaSession::selectPreset(PresetKind kind, const std::string& name) {
  auto lock = lockNow();
  return _session.select_preset(toCore(kind), name);
}

bool HybridOrcaSession::selectFilament(double extruder, const std::string& name) {
  auto lock = lockNow();
  if (extruder < 0) {
    return false;
  }
  return _session.select_filament(static_cast<size_t>(extruder), name);
}

bool HybridOrcaSession::savePresetAs(PresetKind kind, const std::string& name) {
  auto lock = lockNow();
  return _session.save_preset_as(toCore(kind), name);
}

bool HybridOrcaSession::deletePreset(PresetKind kind, const std::string& name) {
  auto lock = lockNow();
  return _session.delete_preset(toCore(kind), name);
}

std::shared_ptr<Promise<ImportPresetsResult>> HybridOrcaSession::importPresets(const std::string& path, bool overwrite,
                                                                                bool asBundle) {
  return Promise<ImportPresetsResult>::async([this, path, overwrite, asBundle]() -> ImportPresetsResult {
    std::lock_guard<std::mutex> lock(_mutex);
    return toJS(_session.import_presets(path, overwrite, asBundle));
  });
}

std::shared_ptr<Promise<std::string>> HybridOrcaSession::exportPresets(const std::vector<PresetRef>& presets,
                                                                        const std::string& outPath) {
  std::vector<Slic3r::Mobile::PresetRef> refs;
  refs.reserve(presets.size());
  for (const auto& p : presets) {
    refs.push_back({toCore(p.kind), p.name});
  }
  return Promise<std::string>::async([this, refs = std::move(refs), outPath]() -> std::string {
    std::lock_guard<std::mutex> lock(_mutex);
    return _session.export_presets(refs, outPath);
  });
}

void HybridOrcaSession::setCloudUser(const std::string& userId) {
  auto lock = lockNow();
  _session.set_cloud_user(userId);
}

std::string HybridOrcaSession::getCloudUser() {
  auto lock = lockNow();
  return _session.cloud_user();
}

std::vector<CloudPresetState> HybridOrcaSession::cloudDirtyPresets() {
  auto lock = lockNow();
  std::vector<CloudPresetState> out;
  for (const auto& s : _session.cloud_dirty_presets()) {
    out.push_back(CloudPresetState(toJS(s.kind), s.name, s.setting_id, s.base_id, s.sync_info,
                                   static_cast<double>(s.updated_time)));
  }
  return out;
}

std::string HybridOrcaSession::cloudPushPayload(PresetKind kind, const std::string& name) {
  auto lock = lockNow();
  return _session.cloud_push_payload(toCore(kind), name);
}

void HybridOrcaSession::cloudMarkSynced(PresetKind kind, const std::string& name, const std::string& settingId,
                                        double updatedTime, const std::string& syncInfo) {
  auto lock = lockNow();
  _session.cloud_mark_synced(toCore(kind), name, settingId, static_cast<long long>(updatedTime), syncInfo);
}

std::shared_ptr<Promise<CloudApplyResult>> HybridOrcaSession::cloudApplyPull(const std::string& upsertsJson) {
  return Promise<CloudApplyResult>::async([this, upsertsJson]() -> CloudApplyResult {
    std::lock_guard<std::mutex> lock(_mutex);
    const auto r = _session.cloud_apply_pull(upsertsJson);
    return CloudApplyResult(r.error, toJS(r.unresolved));
  });
}

std::vector<PendingDelete> HybridOrcaSession::cloudPendingDeletes() {
  auto lock = lockNow();
  std::vector<PendingDelete> out;
  for (const auto& d : _session.cloud_pending_deletes()) {
    out.push_back(PendingDelete(d.setting_id, d.info_path));
  }
  return out;
}

void HybridOrcaSession::cloudConfirmDeleted(const std::string& settingId) {
  auto lock = lockNow();
  _session.cloud_confirm_deleted(settingId);
}

std::string HybridOrcaSession::presetFile(PresetKind kind, const std::string& name) {
  auto lock = lockNow();
  return _session.preset_file(toCore(kind), name);
}

BedInfo HybridOrcaSession::bed() {
  auto lock = lockNow();
  const Slic3r::Mobile::BedInfo bed = _session.bed();
  std::vector<Vec2> shape;
  shape.reserve(bed.shape.size());
  for (const auto& p : bed.shape) {
    shape.push_back(Vec2(p[0], p[1]));
  }
  return BedInfo(std::move(shape), bed.height, bed.custom_texture, bed.custom_model);
}

std::string HybridOrcaSession::option(const std::string& key) {
  auto lock = lockNow();
  return _session.option(key);
}

bool HybridOrcaSession::setOption(const std::string& key, const std::string& value) {
  auto lock = lockNow();
  return _session.set_option(key, value);
}

std::vector<std::string> HybridOrcaSession::modifiedOptions(PresetKind kind) {
  auto lock = lockNow();
  return _session.modified_options(toCore(kind));
}

void HybridOrcaSession::discardModifiedOptions(PresetKind kind) {
  auto lock = lockNow();
  _session.discard_modified_options(toCore(kind));
}

std::shared_ptr<Promise<std::string>> HybridOrcaSession::saveProject(const std::string& path) {
  return Promise<std::string>::async([this, path]() -> std::string {
    std::lock_guard<std::mutex> lock(_mutex);
    return _session.save_project(path);
  });
}

std::shared_ptr<Promise<std::vector<PlateInfo>>> HybridOrcaSession::openProject(const std::string& path) {
  return Promise<std::vector<PlateInfo>>::async([this, path]() -> std::vector<PlateInfo> {
    std::lock_guard<std::mutex> lock(_mutex);
    std::vector<PlateInfo> out;
    for (const auto& p : _session.open_project(path))
      out.push_back(PlateInfo(static_cast<double>(p.id), static_cast<double>(p.object_count), p.is_sliced, p.is_current));
    return out;
  });
}

std::shared_ptr<Promise<std::vector<ObjectInfo>>> HybridOrcaSession::importModel(const std::string& path) {
  return Promise<std::vector<ObjectInfo>>::async([this, path]() -> std::vector<ObjectInfo> {
    std::lock_guard<std::mutex> lock(_mutex);
    return toJS(_session.import(path));
  });
}

std::vector<ObjectInfo> HybridOrcaSession::objects() {
  auto lock = lockNow();
  return toJS(_session.objects());
}

bool HybridOrcaSession::removeObject(double id) {
  auto lock = lockNow();
  return _session.remove_object(static_cast<unsigned long>(id));
}

bool HybridOrcaSession::setTransform(double id, const Vec3& position, const Vec3& rotation, const Vec3& scale) {
  auto lock = lockNow();
  return _session.set_transform(static_cast<unsigned long>(id), toCore(position), toCore(rotation), toCore(scale));
}

ObjectInfo HybridOrcaSession::duplicateObject(double id) {
  auto lock = lockNow();
  return toJS(_session.duplicate_object(static_cast<unsigned long>(id)));
}

bool HybridOrcaSession::mirrorObject(double id, MirrorAxis axis) {
  auto lock = lockNow();
  const char c = axis == MirrorAxis::X ? 'x' : axis == MirrorAxis::Y ? 'y' : 'z';
  return _session.mirror_object(static_cast<unsigned long>(id), c);
}

bool HybridOrcaSession::layFlat(double id) {
  auto lock = lockNow();
  return _session.lay_flat(static_cast<unsigned long>(id));
}

bool HybridOrcaSession::autoOrient(double id) {
  auto lock = lockNow();
  return _session.auto_orient(static_cast<unsigned long>(id));
}

PickHit HybridOrcaSession::pickAt(const Vec3& rayOrigin, const Vec3& rayDirection) {
  auto lock = lockNow();
  const Slic3r::Mobile::Session::PickHit hit = _session.pick_at(toCore(rayOrigin), toCore(rayDirection));
  return PickHit(static_cast<double>(hit.id), toJS(hit.position));
}

void HybridOrcaSession::clear() {
  auto lock = lockNow();
  _session.clear();
}

std::vector<double> HybridOrcaSession::cutObject(double id, double z) {
  auto lock = lockNow();
  std::vector<double> out;
  for (unsigned long new_id : _session.cut_object(static_cast<unsigned long>(id), z))
    out.push_back(static_cast<double>(new_id));
  return out;
}

std::shared_ptr<Promise<bool>> HybridOrcaSession::simplifyObject(double id, double targetTriangles) {
  return Promise<bool>::async([this, id, targetTriangles]() -> bool {
    std::lock_guard<std::mutex> lock(_mutex);
    return _session.simplify_object(static_cast<unsigned long>(id),
                                    static_cast<unsigned long>(std::max(0.0, targetTriangles)));
  });
}

std::shared_ptr<Promise<MeshRepairResult>> HybridOrcaSession::repairObject(double id) {
  return Promise<MeshRepairResult>::async([this, id]() -> MeshRepairResult {
    std::lock_guard<std::mutex> lock(_mutex);
    return toJS(_session.repair_object(static_cast<unsigned long>(id)));
  });
}

std::shared_ptr<Promise<void>> HybridOrcaSession::arrange() {
  return Promise<void>::async([this]() {
    std::lock_guard<std::mutex> lock(_mutex);
    _session.arrange();
  });
}

double HybridOrcaSession::addPlate() {
  auto lock = lockNow();
  return static_cast<double>(_session.add_plate());
}

double HybridOrcaSession::duplicatePlate() {
  auto lock = lockNow();
  return static_cast<double>(_session.duplicate_plate());
}

bool HybridOrcaSession::removePlate(double id) {
  auto lock = lockNow();
  return _session.remove_plate(static_cast<unsigned long>(id));
}

bool HybridOrcaSession::selectPlate(double id) {
  auto lock = lockNow();
  return _session.select_plate(static_cast<unsigned long>(id));
}

double HybridOrcaSession::getCurrentPlate() {
  auto lock = lockNow();
  return static_cast<double>(_session.current_plate());
}

std::vector<PlateInfo> HybridOrcaSession::plateList() {
  auto lock = lockNow();
  std::vector<PlateInfo> out;
  for (const auto& p : _session.plates())
    out.push_back(PlateInfo(static_cast<double>(p.id), static_cast<double>(p.object_count), p.is_sliced, p.is_current));
  return out;
}

double HybridOrcaSession::getExtruderCount() {
  auto lock = lockNow();
  return static_cast<double>(_session.extruder_count());
}

std::vector<std::string> HybridOrcaSession::selectedFilaments() {
  auto lock = lockNow();
  return _session.selected_filaments();
}

double HybridOrcaSession::objectExtruder(double id) {
  auto lock = lockNow();
  return static_cast<double>(_session.object_extruder(static_cast<unsigned long>(id)));
}

bool HybridOrcaSession::setObjectExtruder(double id, double extruder) {
  auto lock = lockNow();
  return _session.set_object_extruder(static_cast<unsigned long>(id), static_cast<int>(extruder));
}

void HybridOrcaSession::takeSnapshot(const std::string& label) {
  auto lock = lockNow();
  _session.take_snapshot(label);
}

bool HybridOrcaSession::undo() {
  auto lock = lockNow();
  return _session.undo();
}

bool HybridOrcaSession::redo() {
  auto lock = lockNow();
  return _session.redo();
}

bool HybridOrcaSession::getCanUndo() {
  auto lock = lockNow();
  return _session.can_undo();
}

bool HybridOrcaSession::getCanRedo() {
  auto lock = lockNow();
  return _session.can_redo();
}

std::string HybridOrcaSession::undoLabel() {
  auto lock = lockNow();
  return _session.undo_label();
}

std::string HybridOrcaSession::redoLabel() {
  auto lock = lockNow();
  return _session.redo_label();
}

std::vector<SnapshotInfo> HybridOrcaSession::snapshotHistory() {
  auto lock = lockNow();
  std::vector<SnapshotInfo> out;
  for (const auto& s : _session.snapshot_history())
    out.push_back(SnapshotInfo(s.label, s.current));
  return out;
}

std::shared_ptr<Promise<SliceResult>> HybridOrcaSession::slice(const std::function<void(const Progress&)>& onProgress) {
  // A void-returning JS callback is dispatched to the JS thread by Nitro, so it is safe
  // to call from the pool thread the slice runs on.
  return Promise<SliceResult>::async([this, onProgress]() -> SliceResult {
    std::lock_guard<std::mutex> lock(_mutex);
    MemoryWatchdog watchdog(_session);
    return toJS(_session.slice([&onProgress](const Slic3r::Mobile::Progress& p) {
      onProgress(Progress(static_cast<double>(p.percent), p.message));
    }));
  });
}

void HybridOrcaSession::cancel() {
  _session.cancel();
}

std::shared_ptr<Promise<std::string>> HybridOrcaSession::exportGCode(const std::string& path) {
  return Promise<std::string>::async([this, path]() -> std::string {
    std::lock_guard<std::mutex> lock(_mutex);
    return _session.export_gcode(path);
  });
}

SliceStatistics HybridOrcaSession::statistics() {
  auto lock = lockNow();
  const Slic3r::Mobile::SliceStatistics s = _session.statistics();
  return SliceStatistics(s.print_time_s, s.filament_mm3, s.filament_g, s.filament_cost, static_cast<double>(s.layer_count));
}

} // namespace margelo::nitro::orca
