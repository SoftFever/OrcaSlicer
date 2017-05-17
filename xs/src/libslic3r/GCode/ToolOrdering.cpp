#include "ToolOrdering.hpp"

namespace Slic3r {
namespace ToolOrdering {

// Collect extruders reuqired to print layers.
static void collect_extruders(const PrintObject &object, std::vector<LayerTools> &layers)
{
    // Collect the support extruders.
    for (auto support_layer : object.support_layers) {
        auto it_layer = std::find(layers.begin(), layers.end(), LayerTools(support_layer->print_z));
        assert(it_layer != layers.end());
        ExtrusionRole role = support_layer->support_fills.role();
        bool         has_support        = role == erMixed || role == erSupportMaterial;
        bool         has_interface      = role == erMixed || role == erSupportMaterialInterface;
        unsigned int extruder_support   = object.config.support_material_extruder.value;
        unsigned int extruder_interface = object.config.support_material_interface_extruder.value;
        if (has_support && has_interface) {
            // If both base and interface supports are to be extruded and one of them will be extruded with a "don't care" extruder,
            // print both with the same extruder to minimize extruder switches.
            if (extruder_support == 0)
                extruder_support = extruder_interface;
            else if (extruder_interface == 0)
                extruder_interface = extruder_support;
        }
        if (has_support)
            it_layer->extruders.push_back(extruder_support);
        if (has_interface)
            it_layer->extruders.push_back(extruder_interface);
    }
    // Collect the object extruders.
    for (auto layer : object.layers) {
        auto it_layer = std::find(layers.begin(), layers.end(), LayerTools(layer->print_z));
        assert(it_layer != layers.end());
        // What extruders are required to print this object layer?
        for (size_t region_id = 0; region_id < object.print()->regions.size(); ++ region_id) {
            const LayerRegion *layerm = layer->regions[region_id];
            if (layerm == nullptr)
                continue;
            const PrintRegion &region = *object.print()->regions[region_id];
            if (! layerm->perimeters.entities.empty())
                it_layer->extruders.push_back(region.config.perimeter_extruder.value);
            bool has_infill       = false; 
            bool has_solid_infill = false;
            for (const ExtrusionEntity *ee : layerm->fills.entities) {
                // fill represents infill extrusions of a single island.
                const auto *fill = dynamic_cast<const ExtrusionEntityCollection*>(ee);
                ExtrusionRole role = fill->entities.empty() ? erNone : fill->entities.front()->role();
                if (is_solid_infill(role))
                    has_solid_infill = true;
                else if (role != erNone)
                    has_infill = true;
            }
            if (has_solid_infill)
                it_layer->extruders.push_back(region.config.solid_infill_extruder);
            if (has_infill)
                it_layer->extruders.push_back(region.config.infill_extruder);
        }
    }

    // Sort and remove duplicates
    for (LayerTools &lt : layers)
        sort_remove_duplicates(lt.extruders);
}

// Reorder extruders to minimize layer changes.
static void reorder_extruders(std::vector<LayerTools> &layers, unsigned int last_extruder_id)
{
    if (layers.empty())
        return;

    if (last_extruder_id == (unsigned int)-1) {
        // The initial print extruder has not been decided yet.
        // Initialize the last_extruder_id with the first non-zero extruder id used for the print.
        last_extruder_id = 0;
        for (size_t i = 0; i < layers.size() && last_extruder_id == 0; ++ i) {
            const LayerTools &lt = layers[i];
            for (unsigned int extruder_id : lt.extruders)
                if (extruder_id > 0) {
                    last_extruder_id = extruder_id;
                    break;
                }
        }
        if (last_extruder_id == 0)
            // Nothing to extrude.
            return;
    } else
        // 1 based index
        ++ last_extruder_id;

    for (LayerTools &lt : layers) {
        if (lt.extruders.empty())
            continue;
        if (lt.extruders.size() == 1 && lt.extruders.front() == 0)
            lt.extruders.front() = last_extruder_id;
        else {
            if (lt.extruders.front() == 0)
                // Pop the "don't care" extruder, the "don't care" region will be merged with the next one.
                lt.extruders.erase(lt.extruders.begin());
            // Reorder the extruders to start with the last one.
            for (size_t i = 1; i < lt.extruders.size(); ++ i)
                if (lt.extruders[i] == last_extruder_id) {
                    // Move the last extruder to the front.
                    memmove(lt.extruders.data() + 1, lt.extruders.data(), i * sizeof(unsigned int));
                    lt.extruders.front() = last_extruder_id;
                    break;
                }
        }
        last_extruder_id = lt.extruders.back();
    }

    // Reindex the extruders, so they are zero based, not 1 based.
    for (LayerTools &lt : layers)
        for (unsigned int &extruder_id : lt.extruders) {
            assert(extruder_id > 0);
            -- extruder_id;
        }
}

static void fill_wipe_tower_partitions(std::vector<LayerTools> &layers)
{
    if (layers.empty())
        return;

    // Count the minimum number of tool changes per layer.
    size_t last_extruder = size_t(-1);
    for (LayerTools &lt : layers) {
        lt.wipe_tower_partitions = lt.extruders.size();
        if (! lt.extruders.empty()) {
            if (last_extruder == size_t(-1) || last_extruder == lt.extruders.front())
                // The first extruder on this layer is equal to the current one, no need to do an initial tool change.
                -- lt.wipe_tower_partitions;
            last_extruder = lt.extruders.back();
        }
    }

    // Propagate the wipe tower partitions down to support the upper partitions by the lower partitions.
    for (int i = int(layers.size()) - 2; i >= 0; -- i)
        layers[i].wipe_tower_partitions = std::max(layers[i + 1].wipe_tower_partitions, layers[i].wipe_tower_partitions);
}

// For the use case when each object is printed separately
// (print.config.complete_objects is true).
std::vector<LayerTools> tool_ordering(const PrintObject &object, unsigned int first_extruder)
{
    // Initialize the print layers for just a single object.
    std::vector<LayerTools> layers;
    {
        std::vector<coordf_t> zs;
        zs.reserve(zs.size() + object.layers.size() + object.support_layers.size());
        for (auto layer : object.layers)
            zs.emplace_back(layer->print_z);
        for (auto layer : object.support_layers)
            zs.emplace_back(layer->print_z);
        sort_remove_duplicates(zs);
        for (coordf_t z : zs)
            layers.emplace_back(LayerTools(z));
    }

    // Collect extruders reuqired to print the layers.
    collect_extruders(object, layers);

    // Reorder the extruders to minimize tool switches.
    reorder_extruders(layers, first_extruder);

    fill_wipe_tower_partitions(layers);
    return layers;
}

// For the use case when all objects are printed at once.
// (print.config.complete_objects is false).
std::vector<LayerTools> tool_ordering(const Print &print, unsigned int first_extruder)
{
    // Initialize the print layers for all objects and all layers. 
    std::vector<LayerTools> layers;
    {
        std::vector<coordf_t> zs;
        for (auto object : print.objects) {
            zs.reserve(zs.size() + object->layers.size() + object->support_layers.size());
            for (auto layer : object->layers)
                zs.emplace_back(layer->print_z);
            for (auto layer : object->support_layers)
                zs.emplace_back(layer->print_z);
        }
        sort_remove_duplicates(zs);
        for (coordf_t z : zs)
            layers.emplace_back(LayerTools(z));
    }

    // Collect extruders reuqired to print the layers.
    for (auto object : print.objects)
        collect_extruders(*object, layers);

    // Reorder the extruders to minimize tool switches.
    reorder_extruders(layers, first_extruder);

    fill_wipe_tower_partitions(layers);
    return layers;
}

unsigned int first_extruder(const std::vector<LayerTools> &layer_tools)
{
    for (const auto &lt : layer_tools)
        if (! lt.extruders.empty())
            return lt.extruders.front();
    return (unsigned int)-1;
}

unsigned int last_extruder(const std::vector<LayerTools> &layer_tools)
{
    for (auto lt_it = layer_tools.rend(); lt_it != layer_tools.rbegin(); ++ lt_it)
        if (! lt_it->extruders.empty())
            return lt_it->extruders.back();
    return (unsigned int)-1;
}

} // namespace ToolOrdering
} // namespace Slic3r
