#include "Session.hpp"

#include "OrcaMobile.hpp"
#include "PresetArchive.hpp"
#include "Thumbnails.hpp"
#include "UndoStack.hpp"

#include "libslic3r/AABBTreeIndirect.hpp"
#include "libslic3r/AppConfig.hpp"
#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/CutUtils.hpp"
#include "libslic3r/Format/bbs_3mf.hpp"
#include "libslic3r/Geometry.hpp"
#include "libslic3r/Config.hpp"
#include "libslic3r/GCode/GCodeProcessor.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/ModelArrange.hpp"
#include "libslic3r/Orient.hpp"
#include "libslic3r/Preset.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/PrintBase.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/QuadricEdgeCollapse.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/Utils.hpp"

#include <admesh/stl.h>
#include <tbb/global_control.h>

#include <boost/filesystem.hpp>
#include <boost/nowide/cstdio.hpp>
#include <boost/nowide/fstream.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <cfloat>
#include <cmath>
#include <limits>
#include <optional>
#include <set>
#include <stdexcept>

namespace Slic3r { namespace Mobile {

struct Session::Impl
{
    PresetBundle         bundle;
    // The app config the preset bundle reads its selections and `preset_folder` (the
    // cloud user id) from. Reloaded from disk on every load_presets; `user_id` is
    // re-applied on top because the app, not the config file, owns the login.
    AppConfig            app_config;
    std::string          user_id;
    // Presets are project-scoped: switching plates does not re-pick the printer or
    // the filament. Every per-plate mutable — model, print output, undo history — is
    // in `Plate`, and the vector below holds them keyed by a monotonic id so the
    // JS side can reference a plate by a value that survives another plate's removal.
    struct Plate
    {
        unsigned long        id = 0;
        Model                model;
        Print                print;
        GCodeProcessorResult gcode_result;
        UndoStack            undo;
        bool                 sliced   = false;
        bool                 exported = false;
    };
    std::vector<std::unique_ptr<Plate>> plates;
    unsigned long                       next_plate_id = 1;
    unsigned long                       current_id    = 0;

    // Set by cancel() from any thread; slice() and simplify_object() reset them on
    // entry. `oom` records that the cancel came from cancel_out_of_memory().
    std::atomic<bool>                   cancel_requested { false };
    std::atomic<bool>                   oom              { false };
    unsigned                            thread_limit = 0;

    Plate& current() { return *find_or_default(); }
    const Plate& current() const { return const_cast<Impl*>(this)->current(); }
    Plate* find_plate(unsigned long id)
    {
        for (auto& p : plates) if (p->id == id) return p.get();
        return nullptr;
    }
    Plate* find_or_default()
    {
        if (auto* p = find_plate(current_id)) return p;
        return plates.empty() ? nullptr : plates.front().get();
    }

    PresetCollection& collection(PresetKind kind)
    {
        switch (kind) {
        case PresetKind::Printer:  return bundle.printers;
        case PresetKind::Filament: return bundle.filaments;
        default:                   return bundle.prints;
        }
    }
    const PresetCollection& collection(PresetKind kind) const { return const_cast<Impl*>(this)->collection(kind); }

    // The collection whose presets carry `key`, or nullptr for keys outside the presets.
    PresetCollection* owner_of(const std::string& key)
    {
        auto contains = [&key](const std::vector<std::string>& keys) {
            return std::find(keys.begin(), keys.end(), key) != keys.end();
        };
        if (contains(Preset::print_options()))    return &bundle.prints;
        if (contains(Preset::filament_options())) return &bundle.filaments;
        if (contains(Preset::printer_options()))  return &bundle.printers;
        return nullptr;
    }

    ModelObject* find_object(unsigned long id) const
    {
        for (ModelObject* o : current().model.objects)
            if (o->id().id == id)
                return o;
        return nullptr;
    }

    ObjectInfo info(const ModelObject& o) const
    {
        ObjectInfo out;
        out.id   = static_cast<unsigned long>(o.id().id);
        out.name = o.name;
        if (! o.instances.empty()) {
            const ModelInstance& inst = *o.instances.front();
            const Vec3d offset = inst.get_offset(), rotation = inst.get_rotation(), scale = inst.get_scaling_factor();
            out.position = { offset.x(), offset.y(), offset.z() };
            out.rotation = { rotation.x(), rotation.y(), rotation.z() };
            out.scale    = { scale.x(), scale.y(), scale.z() };
        }
        const Vec3d size = o.bounding_box_exact().size();
        out.size      = { size.x(), size.y(), size.z() };
        out.triangles = static_cast<unsigned long>(o.facets_count());
        for (const ModelVolume* v : o.volumes)
            if (v->is_model_part())
                out.open_edges += static_cast<unsigned long>(std::max(0, v->mesh().stats().open_edges));
        return out;
    }

    BoundingBoxf bed_bbox() const
    {
        BoundingBoxf bb;
        // Same guard as Session::bed(): full_config() dereferences null when
        // no printer preset has been picked yet. A 200×200 mm default lets
        // the model be imported and centered before the user picks a printer.
        if (bundle.printers.get_edited_preset().is_default) {
            bb.merge(Vec2d(0.0, 0.0));
            bb.merge(Vec2d(200.0, 200.0));
            return bb;
        }
        for (const Point& p : get_bed_shape(bundle.full_config()))
            bb.merge(Vec2d(unscaled<double>(p.x()), unscaled<double>(p.y())));
        return bb;
    }

    // Find the index of a preset by name in a collection, or size_t(-1).
    static size_t index_of(const PresetCollection& c, const std::string& name)
    {
        size_t idx = 0;
        for (const Preset& p : c.get_presets()) {
            if (p.name == name)
                return idx;
            ++ idx;
        }
        return size_t(-1);
    }
};

Session::Session() : m_impl(std::make_unique<Impl>())
{
    // Seed with one blank plate and pin the current-plate cursor to it so every
    // pre-multi-plate caller keeps working as-is. Every add_plate later reuses
    // the same pattern.
    auto plate = std::make_unique<Impl::Plate>();
    plate->id = m_impl->next_plate_id++;
    plate->undo.take_snapshot("New project", plate->model);
    m_impl->current_id = plate->id;
    m_impl->plates.push_back(std::move(plate));
}
Session::~Session() = default;

unsigned long Session::add_plate()
{
    Impl& im   = *m_impl;
    auto  plate = std::make_unique<Impl::Plate>();
    plate->id  = im.next_plate_id++;
    plate->undo.take_snapshot("New plate", plate->model);
    const unsigned long id = plate->id;
    im.plates.push_back(std::move(plate));
    im.current_id = id;
    return id;
}

unsigned long Session::duplicate_plate()
{
    Impl& im    = *m_impl;
    // Model has a copy constructor that also shares meshes via shared_ptr under
    // the hood (see ModelVolume::m_mesh) — cloning a full plate costs an object /
    // instance / config copy but no mesh duplication. Same trick UndoStack uses.
    auto  plate = std::make_unique<Impl::Plate>();
    plate->id  = im.next_plate_id++;
    plate->model = im.current().model;
    plate->undo.take_snapshot("Duplicated plate", plate->model);
    const unsigned long id = plate->id;
    im.plates.push_back(std::move(plate));
    im.current_id = id;
    return id;
}

bool Session::remove_plate(unsigned long id)
{
    Impl& im = *m_impl;
    if (im.plates.size() <= 1) return false; // never leave the project plate-less
    auto it = std::find_if(im.plates.begin(), im.plates.end(),
                           [id](const std::unique_ptr<Impl::Plate>& p) { return p->id == id; });
    if (it == im.plates.end()) return false;
    const bool was_current = (im.current_id == id);
    im.plates.erase(it);
    // If we just removed the current plate, focus the next one so callers do not
    // land on an invalid id. Prefer the plate at the removed index; fall back to
    // the new last one when the deletion was at the tail.
    if (was_current) {
        const size_t idx = static_cast<size_t>(it - im.plates.begin());
        const size_t next = std::min(idx, im.plates.size() - 1);
        im.current_id = im.plates[next]->id;
    }
    return true;
}

bool Session::select_plate(unsigned long id)
{
    Impl& im = *m_impl;
    if (im.find_plate(id) == nullptr) return false;
    im.current_id = id;
    return true;
}

unsigned long Session::current_plate() const { return m_impl->current_id; }

std::vector<Session::PlateInfo> Session::plates() const
{
    const Impl& im = *m_impl;
    std::vector<PlateInfo> out;
    out.reserve(im.plates.size());
    for (const auto& p : im.plates) {
        PlateInfo info;
        info.id           = p->id;
        info.object_count = static_cast<unsigned long>(p->model.objects.size());
        info.is_sliced    = p->sliced;
        info.is_current   = (p->id == im.current_id);
        out.push_back(info);
    }
    return out;
}

std::size_t Session::extruder_count() const
{
    return m_impl->bundle.filament_presets.size();
}

std::vector<std::string> Session::selected_filaments() const
{
    return m_impl->bundle.filament_presets;
}

int Session::object_extruder(unsigned long id) const
{
    const Impl&        im  = *m_impl;
    const ModelObject* obj = im.find_object(id);
    if (obj == nullptr) return 0;
    if (! obj->config.has("extruder")) return 0;
    return obj->config.opt_int("extruder");
}

bool Session::set_object_extruder(unsigned long id, int extruder)
{
    Impl&        im  = *m_impl;
    ModelObject* obj = im.find_object(id);
    if (obj == nullptr) return false;
    if (extruder <= 0) {
        obj->config.erase("extruder");
    } else {
        obj->config.set_key_value("extruder", new ConfigOptionInt(extruder));
    }
    im.current().sliced = false;
    im.current().undo.take_snapshot("Assign extruder", im.current().model);
    return true;
}

static std::string load_presets_into(PresetBundle& bundle, AppConfig& app_config, const std::string& user_id, bool force_refresh)
{
    // Start every load from a clean bundle. PresetBundle::load_system_presets_from_json
    // only resets `this` when the OrcaFilamentLibrary vendor happens to be the first one
    // loaded (via load_vendor_configs_from_json's LoadSystem-triggered reset). Any user
    // who has only picked one or two printers has no OrcaFilamentLibrary in
    // data/system, so a second load_presets accumulates every vendor's presets on top
    // of the previous call's — which is what surfaces as "Found duplicated settings in
    // vendor <Name>'s json file lists" after a Remove-and-reload cycle. delete_files
    // stays false so any user preset on disk survives.
    bundle.reset(/*delete_files=*/false);
    bundle.setup_directories();

    namespace fs = boost::filesystem;
    const fs::path system_dir = fs::path(data_dir()) / "system";

    // System presets are read from data_dir/system, where the desktop's updater
    // installs the vendors the user enabled from resources/profiles. On a normal
    // reload we only install the vendors whose version bumped; on a forced
    // refresh (e.g. right after the app installed or removed a vendor) we wipe
    // the mirror so a same-version reinstall really re-copies, including a
    // vendor whose files were corrupt from a previous partial download, and a
    // vendor removed from resources drops out of the mirror too.
    if (force_refresh && fs::exists(system_dir)) {
        boost::system::error_code ec;
        fs::remove_all(system_dir, ec);
        // ignore ec: a partial wipe is corrected by the reinstall that follows
    }
    // Recreate the (possibly empty) mirror directory so load_presets's
    // directory_iterator does not throw when no vendor exists in resources
    // (i.e. right after the last remaining vendor was uninstalled).
    boost::system::error_code ec;
    fs::create_directories(system_dir, ec);

    std::vector<std::string> to_install;
    for (const std::string& vendor : vendor_names_in(fs::path(resources_dir()) / "profiles"))
        if (! is_vendor_installed(vendor) || installed_vendor_version(vendor) < resource_vendor_version(vendor))
            to_install.push_back(vendor);
    if (! to_install.empty())
        install_vendor_bundles_from_resources(to_install);

    app_config.reset();
    if (! app_config.load_if_exists().empty())
        app_config.reset();
    app_config.set("preset_folder", user_id);
    std::string errors;
    bundle.load_presets(app_config, ForwardCompatibilitySubstitutionRule::Enable, PresetBundle::PresetPreferences(), &errors);
    return errors;
}

std::string Session::load_presets() { return load_presets_into(m_impl->bundle, m_impl->app_config, m_impl->user_id, /*force_refresh=*/false); }

std::string Session::reload_presets() { return load_presets_into(m_impl->bundle, m_impl->app_config, m_impl->user_id, /*force_refresh=*/true); }

std::vector<PresetInfo> Session::presets(PresetKind kind) const
{
    const Impl&             im = *m_impl;
    const PresetCollection& c  = im.collection(kind);
    const std::string       selected = this->selected_preset(kind);
    std::vector<PresetInfo> out;
    for (const Preset& p : c.get_presets()) {
        if (p.is_default)
            continue;
        PresetInfo info;
        info.name          = p.name;
        info.vendor        = p.vendor ? p.vendor->id : std::string();
        info.is_system     = p.is_system;
        info.is_user       = p.is_user();
        info.is_visible    = p.is_visible;
        info.is_compatible = p.is_compatible;
        info.is_selected   = p.name == selected;
        out.push_back(std::move(info));
    }
    return out;
}

std::string Session::selected_preset(PresetKind kind) const
{
    const Impl& im = *m_impl;
    if (kind == PresetKind::Filament)
        return im.bundle.filament_presets.empty() ? std::string() : im.bundle.filament_presets.front();
    return im.collection(kind).get_selected_preset().name;
}

bool Session::select_preset(PresetKind kind, const std::string& name)
{
    if (kind == PresetKind::Filament)
        return this->select_filament(0, name);
    Impl&             im  = *m_impl;
    PresetCollection& c   = im.collection(kind);
    const size_t      idx = Impl::index_of(c, name);
    if (idx == size_t(-1))
        return false;
    // select_preset_by_name() only accepts presets the desktop marked as installed; the
    // app decides what to show, so select by index instead.
    c.select_preset(idx);
    if (kind == PresetKind::Printer) {
        im.bundle.update_multi_material_filament_presets();
        im.bundle.update_compatible(PresetSelectCompatibleType::Never);
    }
    im.current().sliced = false;
    return true;
}

bool Session::select_filament(size_t extruder, const std::string& name)
{
    Impl&        im  = *m_impl;
    const size_t idx = Impl::index_of(im.bundle.filaments, name);
    if (idx == size_t(-1) || extruder >= im.bundle.filament_presets.size())
        return false;
    im.bundle.set_filament_preset(extruder, name);
    if (extruder == 0)
        im.bundle.filaments.select_preset(idx);
    im.current().sliced = false;
    return true;
}

BedInfo Session::bed() const
{
    namespace fs = boost::filesystem;
    const Impl& im = *m_impl;
    BedInfo out;
    // Callers (notably the Metal viewport) may ask for the bed before any
    // vendor bundle is installed. PresetBundle::full_config() walks the
    // filament presets against the printer's extruder list and crashes when
    // that has never been populated. A default 200×200 mm plate keeps the
    // viewport drawable until a printer preset is picked.
    if (im.bundle.printers.get_edited_preset().is_default) {
        out.shape.push_back({ 0.0, 0.0 });
        out.shape.push_back({ 200.0, 0.0 });
        out.shape.push_back({ 200.0, 200.0 });
        out.shape.push_back({ 0.0, 200.0 });
        out.height = 200.0;
        return out;
    }
    const DynamicPrintConfig cfg = im.bundle.full_config();
    for (const Point& p : get_bed_shape(cfg))
        out.shape.push_back({ unscaled<double>(p.x()), unscaled<double>(p.y()) });
    out.height = cfg.opt_float("printable_height");
    // Custom bed texture / model. The desktop reads these off the printer preset
    // as filenames relative to the vendor's profile folder; a user may also
    // point at an absolute path. Resolve so the renderer can just open the file.
    auto resolve = [&](const std::string& raw) -> std::string {
        if (raw.empty()) return {};
        fs::path p(raw);
        if (p.is_absolute()) return p.string();
        const std::string vendor = im.bundle.printers.get_edited_preset().get_printer_id();
        if (vendor.empty()) return {};
        return (fs::path(resources_dir()) / "profiles" / vendor / raw).string();
    };
    out.custom_texture = resolve(cfg.opt_string("bed_custom_texture"));
    out.custom_model   = resolve(cfg.opt_string("bed_custom_model"));
    // Fall back to the vendor's default bed model / texture on the printer
    // model when the user did not override them. The desktop's PresetUtils
    // does this via VendorProfile::PrinterModel — most vendors (Bambu,
    // Qidi, Prusa, Elegoo, Voron etc.) supply their bed art there, not as
    // a `bed_custom_*` option on the preset itself.
    if (out.custom_model.empty()) {
        const std::string vendor_model_path = PresetUtils::system_printer_bed_model(im.bundle.printers.get_edited_preset());
        if (! vendor_model_path.empty() && fs::exists(vendor_model_path))
            out.custom_model = vendor_model_path;
    }
    if (out.custom_texture.empty()) {
        const std::string vendor_texture_path = PresetUtils::system_printer_bed_texture(im.bundle.printers.get_edited_preset());
        if (! vendor_texture_path.empty() && fs::exists(vendor_texture_path))
            out.custom_texture = vendor_texture_path;
    }
    if (! out.custom_model.empty() && fs::exists(out.custom_model)) {
        // Preload the bed STL so the renderer never has to hit disk. Silently
        // skip on parse failure — the polygon + grid fallback still draws a
        // usable plate.
        TriangleMesh mesh;
        if (mesh.ReadSTLFile(out.custom_model.c_str(), /*repair=*/false, nullptr) && ! mesh.its.indices.empty()) {
            // Vendors author their bed STLs with the origin at the plate
            // centre (top surface at Z=0, geometry extending ~180 mm to each
            // side and ~30 mm below). Desktop's Bed3D::update_model_offset
            // draws them by translating the model matrix to the printable
            // area's centre and z=-0.03 to dodge z-fighting with the grid.
            // The mobile bridge hands the renderer world-space triangles in
            // one pass, so bake that same offset into the vertex positions
            // here — otherwise the plate sits at the negative-XY corner of
            // the printable grid instead of under it.
            const BoundingBoxf bb   = im.bed_bbox();
            const Vec2d        c    = bb.defined ? bb.center() : Vec2d(0., 0.);
            const float        offx = static_cast<float>(c.x());
            const float        offy = static_cast<float>(c.y());
            constexpr float    offz = -0.03f;
            const indexed_triangle_set& its = mesh.its;
            out.model_mesh.triangles = static_cast<unsigned long>(its.indices.size());
            out.model_mesh.positions.reserve(its.indices.size() * 9);
            out.model_mesh.normals.reserve(its.indices.size() * 9);
            for (const stl_triangle_vertex_indices& face : its.indices) {
                const Vec3f& a = its.vertices[face(0)];
                const Vec3f& b = its.vertices[face(1)];
                const Vec3f& c = its.vertices[face(2)];
                Vec3f        n = (b - a).cross(c - a);
                const float  len = n.norm();
                n = len > 0.f ? Vec3f(n / len) : Vec3f(0.f, 0.f, 1.f);
                for (const Vec3f* v : { &a, &b, &c }) {
                    out.model_mesh.positions.insert(out.model_mesh.positions.end(),
                                                    { v->x() + offx, v->y() + offy, v->z() + offz });
                    out.model_mesh.normals.insert(out.model_mesh.normals.end(), { n.x(), n.y(), n.z() });
                }
            }
        }
    }
    return out;
}

std::string Session::option(const std::string& key) const
{
    Impl& im = *m_impl;
    if (const PresetCollection* c = im.owner_of(key)) {
        if (const ConfigOption* opt = c->get_edited_preset().config.option(key))
            return opt->serialize();
    }
    return std::string();
}

bool Session::set_option(const std::string& key, const std::string& value)
{
    Impl&             im = *m_impl;
    PresetCollection* c  = im.owner_of(key);
    if (c == nullptr)
        return false;
    try {
        c->get_edited_preset().config.set_deserialize_strict(key, value);
    } catch (const std::exception&) {
        return false;
    }
    im.current().sliced = false;
    return true;
}

std::vector<std::string> Session::modified_options(PresetKind kind) const
{
    return m_impl->collection(kind).current_dirty_options();
}

void Session::discard_modified_options(PresetKind kind)
{
    m_impl->collection(kind).discard_current_changes();
    m_impl->current().sliced = false;
}

bool Session::save_preset_as(PresetKind kind, const std::string& name)
{
    if (name.empty())
        return false;
    PresetCollection& c = m_impl->collection(kind);
    // Refuse to overwrite a preset the collection considers non-editable, matching
    // save_current_preset's own early return; ask up front so the caller gets a bool.
    if (const Preset* existing = c.find_preset(name, false); existing != nullptr && ! existing->can_overwrite())
        return false;
    c.save_current_preset(name, /*detach=*/false, /*save_to_project=*/false, nullptr);
    m_impl->current().sliced = false;
    return true;
}

bool Session::delete_preset(PresetKind kind, const std::string& name)
{
    PresetCollection& c = m_impl->collection(kind);
    const Preset* p = c.find_preset(name, false);
    if (p == nullptr || ! p->is_user())
        return false;
    const bool ok = c.delete_preset(name, /*force=*/false);
    if (ok)
        m_impl->current().sliced = false;
    return ok;
}

std::string Session::preset_file(PresetKind kind, const std::string& name) const
{
    const Preset* p = m_impl->collection(kind).find_preset(name, false);
    return p != nullptr ? p->file : std::string();
}

static Preset::Type preset_type(PresetKind kind)
{
    switch (kind) {
    case PresetKind::Printer:  return Preset::TYPE_PRINTER;
    case PresetKind::Filament: return Preset::TYPE_FILAMENT;
    default:                   return Preset::TYPE_PRINT;
    }
}

ImportPresetsResult Session::import_presets(const std::string& path, bool overwrite, bool as_bundle)
{
    namespace fs = boost::filesystem;
    Impl&               im = *m_impl;
    ImportPresetsResult out;

    const std::vector<ArchiveEntry> entries = scan_preset_archive(path);
    if (entries.empty()) {
        out.skipped.push_back({ fs::path(path).filename().string(), "invalid", std::string(), std::string() });
        return out;
    }

    // PresetBundle::import_presets asks once per clashing user preset. Returning 0 ("no")
    // keeps it asking, so every conflict is collected; 3 is "yes to all".
    std::vector<std::string> files { path };
    auto confirm = [&](const std::string& name) {
        out.conflicts.push_back(name);
        return overwrite ? 3 : 0;
    };
    im.bundle.import_presets(files, confirm, ForwardCompatibilitySubstitutionRule::Enable, im.app_config, /*ignore_bundle_structure=*/! as_bundle);

    // Imported files come back by their (temporary) path; archive entries are matched
    // on the base name, which the extraction preserves.
    std::set<std::string> imported;
    for (const std::string& f : files)
        imported.insert(fs::path(f).filename().string());

    for (const ArchiveEntry& e : entries) {
        if (imported.count(e.file_name)) {
            out.imported.push_back({ e.kind, e.name });
            continue;
        }
        if (! e.valid) {
            out.skipped.push_back({ e.name.empty() ? e.file_name : e.name, "invalid", e.inherits, std::string() });
            continue;
        }
        if (std::find(out.conflicts.begin(), out.conflicts.end(), e.name) != out.conflicts.end())
            continue; // reported in `conflicts`
        PresetCollection& c = im.collection(e.kind);
        if (const Preset* existing = c.find_preset(e.name, false); existing != nullptr && (existing->is_system || existing->is_default)) {
            out.skipped.push_back({ e.name, "system_name", e.inherits, std::string() });
        } else if (! e.inherits.empty() && c.find_preset2(e.inherits, true) == nullptr) {
            out.skipped.push_back({ e.name, "missing_parent", e.inherits, PresetBundle::find_preset_vendor(e.inherits, preset_type(e.kind)) });
        } else {
            out.skipped.push_back({ e.name, "invalid", e.inherits, std::string() });
        }
    }
    if (! out.imported.empty()) {
        im.bundle.update_compatible(PresetSelectCompatibleType::Never);
        if (Impl::Plate* plate = im.find_or_default())
            plate->sliced = false;
    }
    return out;
}

std::string Session::export_presets(const std::vector<PresetRef>& presets, const std::string& out_path)
{
    return write_preset_archive(m_impl->bundle, presets, out_path);
}

void Session::set_cloud_user(const std::string& user_id)
{
    Impl& im   = *m_impl;
    im.user_id = user_id;
    im.app_config.set("preset_folder", user_id);
    im.bundle.update_user_presets_directory(user_id.empty() ? std::string(DEFAULT_USER_FOLDER_NAME) : user_id);
}

std::string Session::cloud_user() const { return m_impl->user_id; }

std::vector<CloudPresetState> Session::cloud_dirty_presets()
{
    Impl& im = *m_impl;
    if (! im.user_id.empty())
        im.bundle.check_and_fix_user_presets_syncinfo(im.user_id);
    std::vector<CloudPresetState> out;
    for (PresetKind kind : { PresetKind::Printer, PresetKind::Filament, PresetKind::Process }) {
        for (const Preset& p : im.collection(kind).get_presets()) {
            if (! p.is_user())
                continue;
            const bool never_synced = p.setting_id.empty() && p.sync_info.empty();
            const bool flagged      = p.sync_info == "create" || p.sync_info == "update" || p.sync_info == "hold";
            if (! never_synced && ! flagged)
                continue;
            out.push_back({ kind, p.name, p.setting_id, p.base_id, p.sync_info, p.updated_time });
        }
    }
    return out;
}

std::string Session::cloud_push_payload(PresetKind kind, const std::string& name)
{
    using nlohmann::json;
    Impl&   im = *m_impl;
    Preset* p  = im.collection(kind).find_preset(name, false);
    if (p == nullptr || ! p->is_user())
        return std::string();
    std::map<std::string, std::string> kv;
    if (im.bundle.get_differed_values_to_update(*p, kv) != 0)
        return std::string();
    json content = json::object();
    for (const auto& [key, value] : kv)
        if (key != ORCA_JSON_KEY_UPDATE_TIME)
            content[key] = value;
    content[BBL_JSON_KEY_NAME] = p->name;
    json out;
    out["content"]      = content;
    out["updated_time"] = p->updated_time;
    out["setting_id"]   = p->setting_id;
    out["sync_info"]    = p->sync_info;
    return out.dump();
}

void Session::cloud_mark_synced(PresetKind kind, const std::string& name, const std::string& setting_id, long long updated_time, const std::string& sync_info)
{
    m_impl->collection(kind).set_sync_info_and_save(name, setting_id, sync_info, updated_time);
}

CloudApplyResult Session::cloud_apply_pull(const std::string& upserts_json)
{
    using nlohmann::json;
    Impl&            im = *m_impl;
    CloudApplyResult out;

    json upserts;
    try {
        upserts = json::parse(upserts_json);
    } catch (const std::exception& e) {
        out.error = std::string("Could not read the sync response: ") + e.what();
        return out;
    }
    if (! upserts.is_array()) {
        out.error = "The sync response is not a list";
        return out;
    }

    // Same shape OrcaCloudServiceAgent::get_user_presets hands the bundle: every content
    // value as a string, plus the id, owner and server time from the envelope.
    std::map<std::string, std::map<std::string, std::string>> presets;
    for (const json& item : upserts) {
        if (! item.is_object())
            continue;
        std::map<std::string, std::string> values;
        const json content = item.value("content", json::object());
        if (content.is_object())
            for (auto it = content.begin(); it != content.end(); ++it)
                values[it.key()] = it->is_string() ? it->get<std::string>() : it->dump();
        const std::string id = item.value("id", std::string());
        if (! values.count(BBL_JSON_KEY_SETTING_ID))
            values[BBL_JSON_KEY_SETTING_ID] = id;
        if (! values.count(BBL_JSON_KEY_USER_ID))
            values[BBL_JSON_KEY_USER_ID] = im.user_id;
        if (! values.count(ORCA_JSON_KEY_UPDATE_TIME))
            values[ORCA_JSON_KEY_UPDATE_TIME] = std::to_string(item.value("updated_time", 0LL));
        std::string name = content.is_object() ? content.value(BBL_JSON_KEY_NAME, std::string()) : std::string();
        if (name.empty())
            name = item.value("name", std::string());
        if (name.empty())
            name = id;
        if (name.empty())
            continue;
        presets[name] = std::move(values);
    }

    // Presets whose parent is not installed are reported, but stay in the map: the
    // bundle skips them on load, and their presence protects a local copy from the
    // "removed on the server" sweep.
    for (const auto& [name, values] : presets) {
        auto inherits = values.find(BBL_JSON_KEY_INHERITS);
        if (inherits == values.end() || inherits->second.empty())
            continue;
        const std::string& parent = inherits->second;
        if (parent.find("fdm_") != std::string::npos || parent.find("@System") != std::string::npos || parent.find("@base") != std::string::npos)
            continue;
        auto type = values.find(BBL_JSON_KEY_TYPE);
        if (type == values.end())
            continue;
        PresetKind kind = type->second == PRESET_IOT_PRINTER_TYPE  ? PresetKind::Printer :
                          type->second == PRESET_IOT_FILAMENT_TYPE ? PresetKind::Filament : PresetKind::Process;
        if (im.collection(kind).find_preset2(parent, true) == nullptr)
            out.unresolved.push_back({ name, "missing_parent", parent, PresetBundle::find_preset_vendor(parent, preset_type(kind)) });
    }

    try {
        im.bundle.load_user_presets(im.app_config, presets, ForwardCompatibilitySubstitutionRule::Enable);
        std::map<std::string, std::string> nothing_to_delete;
        im.bundle.save_user_presets(im.app_config, nothing_to_delete);
    } catch (const std::exception& e) {
        out.error = e.what();
    }
    if (Impl::Plate* plate = im.find_or_default())
        plate->sliced = false;
    return out;
}

std::vector<PendingDelete> Session::cloud_pending_deletes() const
{
    namespace fs = boost::filesystem;
    const Impl&                im = *m_impl;
    std::vector<PendingDelete> out;
    const fs::path user_dir = fs::path(data_dir()) / PRESET_USER_DIR / (im.user_id.empty() ? std::string(DEFAULT_USER_FOLDER_NAME) : im.user_id);
    for (const char* type : { PRESET_PRINT_NAME, PRESET_FILAMENT_NAME, PRESET_PRINTER_NAME }) {
        boost::system::error_code ec;
        const fs::path            dir = user_dir / type;
        if (! fs::is_directory(dir, ec))
            continue;
        for (fs::directory_iterator it(dir, ec), end; ! ec && it != end; it.increment(ec)) {
            if (it->path().extension() != ".info")
                continue;
            fs::path preset_file = it->path();
            preset_file.replace_extension(".json");
            if (fs::exists(preset_file, ec))
                continue; // the preset is still there; its state lives in the collection
            Preset probe(Preset::TYPE_INVALID, std::string());
            probe.load_info(it->path().string());
            if (probe.setting_id.empty())
                continue;
            out.push_back({ probe.setting_id, it->path().string() });
        }
    }
    return out;
}

void Session::cloud_confirm_deleted(const std::string& setting_id)
{
    for (const PendingDelete& d : cloud_pending_deletes())
        if (d.setting_id == setting_id)
            boost::nowide::remove(d.info_path.c_str());
}

static const char* type_name(ConfigOptionType type)
{
    switch (ConfigOptionType(int(type) & ~int(coVectorType))) {
    case coFloat:          return "float";
    case coInt:            return "int";
    case coString:         return "string";
    case coPercent:        return "percent";
    case coFloatOrPercent: return "float_or_percent";
    case coPoint:          return "point";
    case coBool:           return "bool";
    case coEnum:           return "enum";
    default:               return "other";
    }
}

std::string Session::option_definitions_json()
{
    using nlohmann::json;
    auto kind_of = [](const std::string& key) -> const char* {
        auto contains = [&key](const std::vector<std::string>& keys) {
            return std::find(keys.begin(), keys.end(), key) != keys.end();
        };
        if (contains(Preset::print_options()))    return "process";
        if (contains(Preset::filament_options())) return "filament";
        if (contains(Preset::printer_options()))  return "printer";
        return "other";
    };

    json out = json::array();
    for (const auto& [key, def] : print_config_def.options) {
        json j;
        j["key"]      = key;
        j["kind"]     = kind_of(key);
        j["type"]     = type_name(def.type);
        j["vector"]   = (def.type & coVectorType) != 0;
        j["nullable"] = def.nullable;
        j["readonly"] = def.readonly;
        j["label"]    = def.label;
        j["fullLabel"] = def.full_label;
        j["category"] = def.category;
        j["tooltip"]  = def.tooltip;
        j["unit"]     = def.sidetext;
        j["mode"]     = int(def.mode);
        if (def.min != -FLT_MAX) j["min"] = def.min;
        if (def.max !=  FLT_MAX) j["max"] = def.max;
        if (def.default_value)   j["default"] = def.default_value->serialize();
        if (! def.enum_values.empty()) {
            j["enumValues"] = def.enum_values;
            j["enumLabels"] = def.enum_labels;
        }
        out.push_back(std::move(j));
    }
    return out.dump();
}

std::string Session::save_project(const std::string& path)
{
    Impl& im = *m_impl;
    // Merge every plate's objects into one Model — the .3mf format stores a single
    // model tree and describes plate membership as (object_index, instance_index)
    // pairs on the side. We track the mapping while we merge so we can construct a
    // matching PlateData for each plate.
    Model merged;
    struct PlateSlot { int obj_idx; int inst_idx; };
    std::vector<std::vector<PlateSlot>> per_plate(im.plates.size());
    for (size_t pi = 0; pi < im.plates.size(); ++pi) {
        const Impl::Plate& plate = *im.plates[pi];
        for (const ModelObject* src : plate.model.objects) {
            ModelObject* dst = merged.add_object(*src);
            const int obj_idx = static_cast<int>(merged.objects.size()) - 1;
            const int n_inst  = std::max<int>(1, static_cast<int>(dst->instances.size()));
            for (int inst_idx = 0; inst_idx < n_inst; ++inst_idx)
                per_plate[pi].push_back({ obj_idx, inst_idx });
        }
    }
    if (merged.objects.empty())
        throw std::runtime_error("Nothing on the plate to save.");

    // PlateData owns nothing itself here — StoreParams::plate_data_list is a vector
    // of non-owning pointers, so we keep the actual objects alive via unique_ptr.
    std::vector<std::unique_ptr<PlateData>> owner;
    owner.reserve(im.plates.size());
    PlateDataPtrs plate_data_list;
    plate_data_list.reserve(im.plates.size());
    for (size_t pi = 0; pi < im.plates.size(); ++pi) {
        std::set<std::pair<int, int>> objects_and_instances;
        for (const PlateSlot& s : per_plate[pi])
            objects_and_instances.emplace(s.obj_idx, s.inst_idx);
        auto pd = std::make_unique<PlateData>(static_cast<int>(pi + 1), objects_and_instances, /*lock_state=*/false);
        plate_data_list.push_back(pd.get());
        owner.push_back(std::move(pd));
    }

    DynamicPrintConfig cfg = im.bundle.full_config();
    StoreParams params;
    params.path            = path;
    params.model           = &merged;
    params.config          = &cfg;
    params.plate_data_list = plate_data_list;
    params.strategy        = SaveStrategy::Zip64 | SaveStrategy::Silence;
    // Thumbnails come later — the mobile renderer would have to produce them from
    // outside the core, and single-nozzle projects re-open fine without them.
    if (! store_bbs_3mf(params))
        throw std::runtime_error("Could not write 3mf file to " + path);
    return path;
}

std::vector<Session::PlateInfo> Session::open_project(const std::string& path)
{
    Impl& im = *m_impl;
    DynamicPrintConfig        project_config;
    ConfigSubstitutionContext substitutions(ForwardCompatibilitySubstitutionRule::Enable);
    PlateDataPtrs             plate_data_list;
    Model loaded = Model::read_from_file(path, &project_config, &substitutions,
                                         LoadStrategy::LoadModel | LoadStrategy::LoadConfig | LoadStrategy::AddDefaultInstances,
                                         &plate_data_list);
    if (loaded.objects.empty()) {
        release_PlateData_list(plate_data_list);
        throw std::runtime_error("No objects found in " + path);
    }

    // Rebuild the plate list from scratch so opening a project truly replaces the
    // current project rather than accumulating on top of it.
    im.plates.clear();

    auto make_plate = [&](std::vector<int> obj_indices) {
        auto plate = std::make_unique<Impl::Plate>();
        plate->id = im.next_plate_id++;
        for (int oi : obj_indices) {
            if (oi >= 0 && static_cast<size_t>(oi) < loaded.objects.size())
                plate->model.add_object(*loaded.objects[oi]);
        }
        plate->undo.take_snapshot("Open project", plate->model);
        im.plates.push_back(std::move(plate));
    };

    if (plate_data_list.empty()) {
        // Single-plate .3mf (or a non-project mesh format upstream picked open):
        // put every object on one fresh plate.
        std::vector<int> all(loaded.objects.size());
        for (int i = 0; i < static_cast<int>(loaded.objects.size()); ++i) all[i] = i;
        make_plate(std::move(all));
    } else {
        for (const PlateData* pd : plate_data_list) {
            if (pd == nullptr) continue;
            std::set<int> unique_obj;
            for (const auto& pair : pd->objects_and_instances)
                unique_obj.insert(pair.first);
            make_plate(std::vector<int>(unique_obj.begin(), unique_obj.end()));
        }
        // If none of the plate data referenced any objects (a malformed 3mf), fall
        // back to a single-plate open so we do not land the session plate-less.
        if (im.plates.empty()) {
            std::vector<int> all(loaded.objects.size());
            for (int i = 0; i < static_cast<int>(loaded.objects.size()); ++i) all[i] = i;
            make_plate(std::move(all));
        }
    }
    release_PlateData_list(plate_data_list);

    im.current_id = im.plates.front()->id;
    return this->plates();
}

std::vector<ObjectInfo> Session::import(const std::string& path)
{
    Impl& im = *m_impl;
    DynamicPrintConfig        project_config;
    ConfigSubstitutionContext substitutions(ForwardCompatibilitySubstitutionRule::Enable);
    Model loaded = Model::read_from_file(path, &project_config, &substitutions,
                                         LoadStrategy::LoadModel | LoadStrategy::LoadConfig | LoadStrategy::AddDefaultInstances);
    if (loaded.objects.empty())
        throw std::runtime_error("No objects found in " + path);

    const BoundingBoxf      bed = im.bed_bbox();
    const Vec2d             center = bed.defined ? bed.center() : Vec2d(0., 0.);
    std::vector<ObjectInfo> out;
    for (const ModelObject* src : loaded.objects) {
        ModelObject* obj = im.current().model.add_object(*src);
        if (obj->instances.empty())
            obj->add_instance();
        obj->center_around_origin(false);
        obj->ensure_on_bed();
        ModelInstance* inst = obj->instances.front();
        inst->set_offset(Vec3d(center.x(), center.y(), inst->get_offset().z()));
        obj->invalidate_bounding_box();
        out.push_back(im.info(*obj));
    }
    im.current().sliced = false;
    im.current().undo.take_snapshot("Import", im.current().model);
    return out;
}

std::vector<ObjectInfo> Session::objects() const
{
    std::vector<ObjectInfo> out;
    for (const ModelObject* o : m_impl->current().model.objects)
        out.push_back(m_impl->info(*o));
    return out;
}

bool Session::remove_object(unsigned long id)
{
    Impl& im = *m_impl;
    for (size_t i = 0; i < im.current().model.objects.size(); ++ i)
        if (im.current().model.objects[i]->id().id == id) {
            im.current().model.delete_object(i);
            im.current().sliced = false;
            im.current().undo.take_snapshot("Delete object", im.current().model);
            return true;
        }
    return false;
}

bool Session::set_transform(unsigned long id, const Vec3& position, const Vec3& rotation, const Vec3& scale)
{
    // Runs on every frame of a gesture; snapshotting here would push hundreds of
    // entries per drag. The bridge calls take_snapshot("Move object" / "Rotate" /
    // "Scale") once when the gesture ends, matching desktop's per-action label.
    Impl&        im  = *m_impl;
    ModelObject* obj = im.find_object(id);
    if (obj == nullptr || obj->instances.empty())
        return false;
    ModelInstance* inst = obj->instances.front();
    inst->set_offset(Vec3d(position[0], position[1], position[2]));
    inst->set_rotation(Vec3d(rotation[0], rotation[1], rotation[2]));
    inst->set_scaling_factor(Vec3d(scale[0], scale[1], scale[2]));
    obj->invalidate_bounding_box();
    im.current().sliced = false;
    return true;
}

ObjectInfo Session::duplicate_object(unsigned long id)
{
    Impl&        im  = *m_impl;
    ModelObject* src = im.find_object(id);
    if (src == nullptr || src->instances.empty())
        return {};
    // Deep-copy so id() differs and the new object can be selected /
    // transformed independently. The desktop Duplicate action offsets by
    // half the object's XY extent so the copy lands next to the original.
    ModelObject* dup = im.current().model.add_object(*src);
    if (dup->instances.empty())
        dup->add_instance();
    const BoundingBoxf3 bb = src->instance_bounding_box(0);
    const double dx = std::max(10.0, (bb.max.x() - bb.min.x()) * 0.6);
    ModelInstance* inst = dup->instances.front();
    Vec3d offset = inst->get_offset();
    offset.x() += dx;
    inst->set_offset(offset);
    dup->invalidate_bounding_box();
    im.current().sliced = false;
    im.current().undo.take_snapshot("Selection-clone", im.current().model);
    return im.info(*dup);
}

bool Session::mirror_object(unsigned long id, char axis)
{
    Impl&        im  = *m_impl;
    ModelObject* obj = im.find_object(id);
    if (obj == nullptr || obj->instances.empty()) return false;
    ModelInstance* inst = obj->instances.front();
    Vec3d s = inst->get_scaling_factor();
    switch (axis) {
        case 'x': case 'X': s.x() = -s.x(); break;
        case 'y': case 'Y': s.y() = -s.y(); break;
        case 'z': case 'Z': s.z() = -s.z(); break;
        default: return false;
    }
    inst->set_scaling_factor(s);
    obj->invalidate_bounding_box();
    im.current().sliced = false;
    im.current().undo.take_snapshot("Mirror", im.current().model);
    return true;
}

bool Session::lay_flat(unsigned long id)     { return this->auto_orient(id); }

bool Session::auto_orient(unsigned long id)
{
    Impl&        im  = *m_impl;
    ModelObject* obj = im.find_object(id);
    if (obj == nullptr || obj->instances.empty()) return false;
    Slic3r::orientation::orient(obj->instances.front());
    obj->invalidate_bounding_box();
    im.current().sliced = false;
    im.current().undo.take_snapshot("Orient", im.current().model);
    return true;
}

std::vector<unsigned long> Session::cut_object(unsigned long id, double z)
{
    Impl&        im  = *m_impl;
    ModelObject* obj = im.find_object(id);
    if (obj == nullptr || obj->instances.empty()) return {};

    // The Cut helper expects the transform to bring the cut plane to world Z=0,
    // matching what Plater::cut does for the desktop gizmo: shift the object down
    // by (z - instance_offset.z) so the plane is where the caller asked.
    const Vec3d       inst_offset = obj->instances.front()->get_offset();
    const Transform3d cut_matrix  = Geometry::translation_transform(Vec3d(0, 0, z) - inst_offset);
    const auto        attrs       = ModelObjectCutAttribute::KeepUpper
                                  | ModelObjectCutAttribute::KeepLower
                                  | ModelObjectCutAttribute::PlaceOnCutLower;

    Cut cut(obj, /*instance_idx=*/0, cut_matrix, attrs);
    ModelObjectPtrs pieces = cut.perform_with_plane();
    if (pieces.empty()) return {};

    // Replace the source object with the new pieces on the same plate. The
    // returned pointers are already fresh ModelObjects — copy them into our
    // Model so they take ownership and get new IDs assigned by add_object.
    auto& objs = im.current().model.objects;
    const auto it = std::find(objs.begin(), objs.end(), obj);
    if (it == objs.end()) return {};
    const size_t src_idx = static_cast<size_t>(it - objs.begin());
    im.current().model.delete_object(src_idx);

    std::vector<unsigned long> out;
    out.reserve(pieces.size());
    for (const ModelObject* p : pieces) {
        ModelObject* dst = im.current().model.add_object(*p);
        out.push_back(static_cast<unsigned long>(dst->id().id));
    }
    im.current().sliced = false;
    im.current().undo.take_snapshot("Cut", im.current().model);
    return out;
}

namespace {

// Install a volume's new mesh the way the desktop Simplify gizmo does after it
// replaces one: the convex hulls and the id are derived from the mesh.
void replace_volume_mesh(ModelVolume& vol, TriangleMesh&& mesh)
{
    vol.set_mesh(std::move(mesh));
    vol.calculate_convex_hull();
    vol.invalidate_convex_hull_2d();
    vol.set_new_unique_id();
}

} // namespace

bool Session::simplify_object(unsigned long id, unsigned long target_triangles)
{
    Impl&        im  = *m_impl;
    ModelObject* obj = im.find_object(id);
    if (obj == nullptr || target_triangles == 0) return false;

    std::vector<ModelVolume*> parts;
    size_t                    total = 0;
    for (ModelVolume* v : obj->volumes)
        if (v->is_model_part()) {
            parts.push_back(v);
            total += v->mesh().its.indices.size();
        }
    if (parts.empty() || total <= target_triangles) return false;

    im.cancel_requested = false;
    struct Cancelled {};
    const auto throw_if_cancelled = [&im]() { if (im.cancel_requested) throw Cancelled{}; };

    // Collapse every part into a fresh mesh first and only then install them, so a
    // cancel half-way leaves the object as it was.
    std::vector<std::pair<ModelVolume*, TriangleMesh>> results;
    try {
        for (ModelVolume* v : parts) {
            const size_t n = v->mesh().its.indices.size();
            // Each part's share of the budget; never below a tetrahedron so a small
            // part next to a big one does not vanish.
            const auto wanted = static_cast<uint32_t>(std::max<double>(4.0, std::floor(double(n) / double(total) * double(target_triangles))));
            if (wanted >= n) continue;
            indexed_triangle_set its = v->mesh().its;
            float max_error = std::numeric_limits<float>::max();
            its_quadric_edge_collapse(its, wanted, &max_error, throw_if_cancelled);
            if (its.indices.empty()) continue;
            results.emplace_back(v, TriangleMesh(std::move(its)));
        }
    } catch (const Cancelled&) {
        return false;
    }
    if (results.empty()) return false;

    for (auto& [v, mesh] : results)
        replace_volume_mesh(*v, std::move(mesh));
    obj->invalidate_bounding_box();
    obj->ensure_on_bed();
    im.current().sliced = false;
    im.current().undo.take_snapshot("Simplify mesh", im.current().model);
    return true;
}

Session::MeshRepairResult Session::repair_object(unsigned long id)
{
    Impl&            im  = *m_impl;
    MeshRepairResult out;
    ModelObject*     obj = im.find_object(id);
    if (obj == nullptr) return out;

    for (ModelVolume* v : obj->volumes) {
        if (! v->is_model_part()) continue;
        const TriangleMesh& src = v->mesh();
        out.open_edges_before += static_cast<unsigned long>(std::max(0, src.stats().open_edges));
        if (src.its.indices.empty()) {
            continue;
        }

        // Pass 1, on the indexed set: unite duplicate vertices and drop zero-area
        // faces. admesh's nearby-edge pass below only snaps within a tolerance, so
        // exact duplicates are cheaper to fold here first.
        indexed_triangle_set its = src.its;
        out.vertices_merged  += static_cast<unsigned long>(std::max(0, its_merge_vertices(its)));
        out.degenerate_faces += static_cast<unsigned long>(std::max(0, its_remove_degenerate_faces(its)));
        its_compactify_vertices(its);

        // Pass 2, through admesh: the repair TriangleMesh::ReadSTLFile runs on import
        // (nearby-edge snapping, unconnected-face removal, normal directions and
        // values). It works on a facet soup, so expand the indexed set into one.
        stl_file stl;
        stl.stats.type                = inmemory;
        stl.stats.number_of_facets    = static_cast<uint32_t>(its.indices.size());
        stl.stats.original_num_facets = static_cast<int>(its.indices.size());
        stl_allocate(&stl);
        for (size_t i = 0; i < its.indices.size(); ++i) {
            stl_facet& f = stl.facet_start[i];
            for (int k = 0; k < 3; ++k)
                f.vertex[k] = its.vertices[its.indices[i](k)];
            f.extra[0] = f.extra[1] = 0;
            stl_calculate_normal(f.normal, &f);
            stl_normalize_vector(f.normal);
        }
        stl_get_size(&stl);
        TriangleMesh repaired;
        repaired.from_stl(stl, /*repair=*/true);

        out.faces_removed  += static_cast<unsigned long>(std::max(0, stl.stats.facets_removed));
        out.faces_reversed += static_cast<unsigned long>(std::max(0, stl.stats.facets_reversed));
        out.edges_fixed    += static_cast<unsigned long>(std::max(0, stl.stats.edges_fixed));
        out.open_edges_after += static_cast<unsigned long>(std::max(0, repaired.stats().open_edges));

        const bool changed = out.vertices_merged > 0 || out.degenerate_faces > 0 || stl.stats.facets_removed > 0
                          || stl.stats.facets_reversed > 0 || stl.stats.edges_fixed > 0
                          || repaired.its.indices.size() != src.its.indices.size();
        if (changed && ! repaired.its.indices.empty()) {
            replace_volume_mesh(*v, std::move(repaired));
            out.changed = true;
        }
    }

    if (out.changed) {
        obj->invalidate_bounding_box();
        obj->ensure_on_bed();
        im.current().sliced = false;
        im.current().undo.take_snapshot("Repair mesh", im.current().model);
    } else {
        out.open_edges_after = out.open_edges_before;
    }
    return out;
}

Session::PickHit Session::pick_at(const Vec3& ray_origin, const Vec3& ray_direction) const
{
    // Iterate every visible instance, transform the world-space ray into
    // instance-local space, then ask the shared AABB tree for the first
    // hit. Keep the closest world-space intersection.
    const Impl& im = *m_impl;
    PickHit best;
    double  best_t = std::numeric_limits<double>::infinity();
    const Vec3d origin(ray_origin[0], ray_origin[1], ray_origin[2]);
    const Vec3d direction(ray_direction[0], ray_direction[1], ray_direction[2]);
    for (const ModelObject* obj : im.current().model.objects) {
        if (obj == nullptr) continue;
        const TriangleMesh raw = obj->raw_mesh();
        if (raw.its.indices.empty()) continue;
        const auto tree = AABBTreeIndirect::build_aabb_tree_over_indexed_triangle_set(raw.its.vertices, raw.its.indices);
        for (const ModelInstance* inst : obj->instances) {
            if (inst == nullptr || ! inst->printable) continue;
            const Transform3d trafo = inst->get_matrix();
            const Transform3d inv   = trafo.inverse();
            const Vec3d local_o = inv * origin;
            const Vec3d local_d = inv.linear() * direction;
            igl::Hit<float> hit;
            if (! AABBTreeIndirect::intersect_ray_first_hit(raw.its.vertices, raw.its.indices, tree, local_o, local_d, hit)) continue;
            const Vec3d world = trafo * (local_o + local_d * double(hit.t));
            const double t = (world - origin).norm();
            if (t < best_t) {
                best_t = t;
                best.id = static_cast<unsigned long>(obj->id().id);
                best.position = { world.x(), world.y(), world.z() };
            }
        }
    }
    return best;
}

void Session::clear()
{
    m_impl->current().model.clear_objects();
    m_impl->current().sliced = false;
    m_impl->current().undo.take_snapshot("Clear plate", m_impl->current().model);
}

void Session::arrange()
{
    Impl&                    im  = *m_impl;
    const DynamicPrintConfig cfg = im.bundle.full_config();
    ArrangeParams            params(scaled(min_object_distance(cfg)));
    params.printable_height = float(cfg.opt_float("printable_height"));
    arrange_objects(im.current().model, BoundingBox(get_bed_shape(cfg)), params);
    for (ModelObject* o : im.current().model.objects)
        o->invalidate_bounding_box();
    im.current().sliced = false;
    im.current().undo.take_snapshot("Arrange", im.current().model);
}

void Session::take_snapshot(std::string label)
{
    m_impl->current().undo.take_snapshot(std::move(label), m_impl->current().model);
}

bool Session::undo()
{
    if (! m_impl->current().undo.undo(m_impl->current().model))
        return false;
    m_impl->current().sliced = false;
    return true;
}

bool Session::redo()
{
    if (! m_impl->current().undo.redo(m_impl->current().model))
        return false;
    m_impl->current().sliced = false;
    return true;
}

bool Session::can_undo() const { return m_impl->current().undo.can_undo(); }
bool Session::can_redo() const { return m_impl->current().undo.can_redo(); }
std::string Session::undo_label() const { return m_impl->current().undo.undo_label(); }
std::string Session::redo_label() const { return m_impl->current().undo.redo_label(); }

std::vector<Session::SnapshotInfo> Session::snapshot_history() const
{
    std::vector<SnapshotInfo> out;
    for (const UndoStack::SnapshotInfo& s : m_impl->current().undo.snapshots())
        out.push_back({ s.label, s.current });
    return out;
}

SliceResult Session::slice(const ProgressFn& on_progress)
{
    Impl&       im = *m_impl;
    SliceResult result;
    im.current().sliced   = false;
    im.current().exported = false;
    im.cancel_requested   = false;
    im.oom                = false;

    // Drop the previous result before the new one is built. A GCodeProcessorResult
    // holds one vertex per move and the processed Print holds every layer, so a
    // re-slice would otherwise peak at old plus new; a phone has no room for that.
    im.current().gcode_result = GCodeProcessorResult();

    // Scoped for the slice only, so an export or arrange afterwards is unaffected.
    std::optional<tbb::global_control> thread_cap;
    if (im.thread_limit > 0)
        thread_cap.emplace(tbb::global_control::max_allowed_parallelism, im.thread_limit);

    std::vector<SliceWarning> warnings;
    im.current().print.restart();
    im.current().print.set_status_callback([&warnings, &on_progress](const PrintBase::SlicingStatus& s) {
        if (s.warning_step != -1) {
            if (! s.text.empty())
                warnings.push_back({ s.text, s.warning_level != PrintStateBase::WarningLevel::NON_CRITICAL });
        } else if (s.percent >= 0 && on_progress) {
            on_progress(Progress{ s.percent, s.text });
        }
    });

    try {
        im.current().print.apply(im.current().model, im.bundle.full_config());
        std::vector<StringObjectException> validation_warnings;
        const StringObjectException        err = im.current().print.validate(&validation_warnings);
        for (const StringObjectException& w : validation_warnings)
            warnings.push_back({ w.string, false });
        if (! err.string.empty()) {
            result.outcome = SliceOutcome::Failed;
            result.error   = err.string;
        } else {
            im.current().print.process();
            result.outcome = SliceOutcome::Finished;
            im.current().sliced      = true;
        }
    } catch (const CanceledException&) {
        result.outcome = im.oom ? SliceOutcome::OutOfMemory : SliceOutcome::Cancelled;
    } catch (const std::bad_alloc&) {
        // Rare on iOS, where the kernel usually kills the process before malloc fails,
        // but the fragmentation case does surface here.
        result.outcome = SliceOutcome::OutOfMemory;
    }  catch (const std::exception& e) {
        result.outcome = SliceOutcome::Failed;
        result.error   = e.what();
    }

    if (result.outcome == SliceOutcome::OutOfMemory) {
        result.error = "The device is running out of memory. Simplify or scale down the model and slice again.";
        // Give the memory back now: the half-built layers are useless and the user is
        // about to be shown a dialog, not another slice. apply() rebuilds next time.
        im.current().print.clear();
    }

    im.current().print.set_status_silent();
    result.warnings = std::move(warnings);
    return result;
}

void Session::cancel()
{
    m_impl->cancel_requested = true;
    m_impl->current().print.cancel();
}

void Session::cancel_out_of_memory()
{
    m_impl->oom = true;
    this->cancel();
}

void Session::set_thread_limit(unsigned threads)
{
    m_impl->thread_limit = threads;
}

bool Session::is_sliced() const
{
    return m_impl->current().sliced;
}

std::string Session::export_gcode(const std::string& path)
{
    Impl& im = *m_impl;
    if (! im.current().sliced)
        throw std::runtime_error("The print is not sliced");
    // Embed thumbnails so Moonraker / Mainsail / Fluidd / OctoPrint can
    // show a preview. GCode::do_export asks the callback once per size
    // configured on the printer preset (`thumbnails`, default
    // "48x48/PNG,300x300/PNG"). We render every requested size top-down
    // through the mobile CPU rasterizer — no GL context on this thread.
    const Model& model = im.current().model;
    const BedInfo bed = this->bed();
    ThumbnailsGeneratorCallback thumbnail_cb = [&](const ThumbnailsParams& params) {
        ThumbnailsList out;
        out.reserve(params.sizes.size());
        for (const Vec2d& size : params.sizes) {
            const unsigned int w = std::max(1u, (unsigned int) std::round(size.x()));
            const unsigned int h = std::max(1u, (unsigned int) std::round(size.y()));
            out.push_back(render_plate_thumbnail(model, bed, w, h));
        }
        return out;
    };
    const std::string written = im.current().print.export_gcode(path, &im.current().gcode_result, thumbnail_cb);
    im.current().exported = true;
    return written;
}

SliceStatistics Session::statistics() const
{
    const Impl&     im = *m_impl;
    SliceStatistics out;
    if (! im.current().exported)
        return out;
    const PrintEstimatedStatistics& stats = im.current().gcode_result.print_statistics;
    out.print_time_s = stats.modes[static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Normal)].time;
    for (const auto& [extruder, volume] : stats.total_volumes_per_extruder) {
        out.filament_mm3 += volume;
        if (extruder < im.current().gcode_result.filament_densities.size()) {
            // density is g/cm3, volume mm3
            const double grams = volume * im.current().gcode_result.filament_densities[extruder] * 0.001;
            out.filament_g += grams;
            if (extruder < im.current().gcode_result.filament_costs.size())
                out.filament_cost += grams * 0.001 * im.current().gcode_result.filament_costs[extruder];
        }
    }
    for (const PrintObject* po : im.current().print.objects())
        out.layer_count = std::max<unsigned long>(out.layer_count, static_cast<unsigned long>(po->layer_count()));
    return out;
}

MeshData Session::mesh(unsigned long id) const
{
    MeshData           out;
    const ModelObject* obj = m_impl->find_object(id);
    if (obj == nullptr)
        return out;

    // ModelObject::mesh() is the object's volumes placed by every instance, so it is
    // already in world space.
    const TriangleMesh mesh = obj->mesh();

    const indexed_triangle_set& its = mesh.its;
    out.triangles = static_cast<unsigned long>(its.indices.size());
    out.positions.reserve(its.indices.size() * 9);
    out.normals.reserve(its.indices.size() * 9);
    for (const stl_triangle_vertex_indices& face : its.indices) {
        const Vec3f& a = its.vertices[face(0)];
        const Vec3f& b = its.vertices[face(1)];
        const Vec3f& c = its.vertices[face(2)];
        Vec3f        n = (b - a).cross(c - a);
        const float  len = n.norm();
        n = len > 0.f ? Vec3f(n / len) : Vec3f(0.f, 0.f, 1.f);
        for (const Vec3f* v : { &a, &b, &c }) {
            out.positions.insert(out.positions.end(), { v->x(), v->y(), v->z() });
            out.normals.insert(out.normals.end(), { n.x(), n.y(), n.z() });
        }
    }
    return out;
}

static PreviewRole preview_role(ExtrusionRole role)
{
    switch (role) {
    case erPerimeter:                return PreviewRole::Perimeter;
    case erExternalPerimeter:        return PreviewRole::ExternalPerimeter;
    case erOverhangPerimeter:        return PreviewRole::OverhangPerimeter;
    case erInternalInfill:           return PreviewRole::InternalInfill;
    case erSolidInfill:              return PreviewRole::SolidInfill;
    case erTopSolidInfill:           return PreviewRole::TopSolidInfill;
    case erBottomSurface:            return PreviewRole::BottomSurface;
    case erIroning:                  return PreviewRole::Ironing;
    case erBridgeInfill:             return PreviewRole::BridgeInfill;
    case erInternalBridgeInfill:     return PreviewRole::InternalBridgeInfill;
    case erGapFill:                  return PreviewRole::GapFill;
    case erSkirt:                    return PreviewRole::Skirt;
    case erBrim:                     return PreviewRole::Brim;
    case erSupportMaterial:          return PreviewRole::Support;
    case erSupportMaterialInterface: return PreviewRole::SupportInterface;
    case erSupportTransition:        return PreviewRole::SupportTransition;
    case erWipeTower:                return PreviewRole::WipeTower;
    case erCustom:                   return PreviewRole::Custom;
    case erMixed:                    return PreviewRole::Mixed;
    default:                         return PreviewRole::None;
    }
}

static PreviewMoveType preview_move_type(EMoveType type)
{
    switch (type) {
    case EMoveType::Retract:      return PreviewMoveType::Retract;
    case EMoveType::Unretract:    return PreviewMoveType::Unretract;
    case EMoveType::Seam:         return PreviewMoveType::Seam;
    case EMoveType::Tool_change:  return PreviewMoveType::ToolChange;
    case EMoveType::Color_change: return PreviewMoveType::ColorChange;
    case EMoveType::Pause_Print:  return PreviewMoveType::PausePrint;
    case EMoveType::Custom_GCode: return PreviewMoveType::CustomGCode;
    case EMoveType::Travel:       return PreviewMoveType::Travel;
    case EMoveType::Wipe:         return PreviewMoveType::Wipe;
    case EMoveType::Extrude:      return PreviewMoveType::Extrude;
    default:                      return PreviewMoveType::Noop;
    }
}

// "#RRGGBB" or "#RRGGBBAA" to RGBA floats; anything else is opaque grey.
static std::array<float, 4> parse_color(const std::string& text)
{
    std::array<float, 4> rgba = { 0.5f, 0.5f, 0.5f, 1.f };
    if ((text.size() == 7 || text.size() == 9) && text[0] == '#') {
        auto hex = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        };
        for (size_t i = 0; i < (text.size() - 1) / 2; ++ i) {
            const int hi = hex(text[1 + 2 * i]), lo = hex(text[2 + 2 * i]);
            if (hi < 0 || lo < 0)
                return { 0.5f, 0.5f, 0.5f, 1.f };
            rgba[i] = float(hi * 16 + lo) / 255.f;
        }
    }
    return rgba;
}

PreviewData Session::preview() const
{
    const Impl& im = *m_impl;
    PreviewData out;
    if (! im.current().exported)
        return out;

    const GCodeProcessorResult& result = im.current().gcode_result;
    out.print_time_s = result.print_statistics.modes[static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Normal)].time;
    for (const std::string& color : result.extruder_colors)
        out.tool_colors.push_back(parse_color(color));

    out.vertices.reserve(result.moves.size());
    for (const GCodeProcessorResult::MoveVertex& move : result.moves) {
        PreviewVertex v;
        v.position[0]     = move.position.x();
        v.position[1]     = move.position.y();
        v.position[2]     = move.position.z();
        v.width           = move.width;
        v.height          = move.height;
        v.feedrate        = move.feedrate;
        v.fan_speed       = move.fan_speed;
        v.temperature     = move.temperature;
        v.volumetric_rate = move.volumetric_rate();
        v.time            = move.time[static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Normal)];
        v.layer_id        = move.layer_id;
        v.extruder_id     = move.extruder_id;
        v.color_id        = move.cp_color_id;
        v.role            = preview_role(move.extrusion_role);
        v.type            = preview_move_type(move.type);
        out.vertices.push_back(v);

        if (move.type == EMoveType::Extrude) {
            if (out.layer_zs.size() <= move.layer_id)
                out.layer_zs.resize(move.layer_id + 1, 0.f);
            out.layer_zs[move.layer_id] = std::max(out.layer_zs[move.layer_id], move.position.z());
        }
    }
    // Derive per-layer time from the running `time` counter: each layer's
    // duration is the max time seen on it minus the max seen on the previous.
    // Approximation, but matches how libvgcode surfaces it and is enough to
    // colour-scale extrusions by layer duration.
    if (! out.vertices.empty()) {
        std::vector<float> maxTimePerLayer(out.layer_zs.size(), 0.f);
        for (const PreviewVertex& v : out.vertices)
            if (v.layer_id < maxTimePerLayer.size())
                maxTimePerLayer[v.layer_id] = std::max(maxTimePerLayer[v.layer_id], v.time);
        out.layer_times.resize(maxTimePerLayer.size(), 0.f);
        float prev = 0.f;
        for (size_t i = 0; i < maxTimePerLayer.size(); ++i) {
            out.layer_times[i] = std::max(0.f, maxTimePerLayer[i] - prev);
            if (maxTimePerLayer[i] > prev) prev = maxTimePerLayer[i];
        }
    }
    return out;
}

}} // namespace Slic3r::Mobile
