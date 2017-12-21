#include "Print.hpp"
#include "ToolOrdering.hpp"

// #define SLIC3R_DEBUG

// Make assert active if SLIC3R_DEBUG
#ifdef SLIC3R_DEBUG
    #define DEBUG
    #define _DEBUG
    #undef NDEBUG
#endif

#include <cassert>
#include <limits>

namespace Slic3r {

// For the use case when each object is printed separately
// (print.config.complete_objects is true).
ToolOrdering::ToolOrdering(const PrintObject &object, unsigned int first_extruder, bool prime_multi_material)
{
    if (object.layers.empty())
        return;

    // Initialize the print layers for just a single object.
    {
        std::vector<coordf_t> zs;
        zs.reserve(zs.size() + object.layers.size() + object.support_layers.size());
        for (auto layer : object.layers)
            zs.emplace_back(layer->print_z);
        for (auto layer : object.support_layers)
            zs.emplace_back(layer->print_z);
        this->initialize_layers(zs);
    }

    // Collect extruders reuqired to print the layers.
    this->collect_extruders(object);

    // Reorder the extruders to minimize tool switches.
    this->reorder_extruders(first_extruder);

    this->fill_wipe_tower_partitions(object.print()->config, object.layers.front()->print_z - object.layers.front()->height);

    this->collect_extruder_statistics(prime_multi_material);
}

// For the use case when all objects are printed at once.
// (print.config.complete_objects is false).
ToolOrdering::ToolOrdering(const Print &print, unsigned int first_extruder, bool prime_multi_material)
{
    // Initialize the print layers for all objects and all layers.
    coordf_t object_bottom_z = 0.;
    {
        std::vector<coordf_t> zs;
        for (auto object : print.objects) {
            zs.reserve(zs.size() + object->layers.size() + object->support_layers.size());
            for (auto layer : object->layers)
                zs.emplace_back(layer->print_z);
            for (auto layer : object->support_layers)
                zs.emplace_back(layer->print_z);
            if (! object->layers.empty())
                object_bottom_z = object->layers.front()->print_z - object->layers.front()->height;
        }
        this->initialize_layers(zs);
    }

    // Collect extruders reuqired to print the layers.
    for (auto object : print.objects)
        this->collect_extruders(*object);

    // Reorder the extruders to minimize tool switches.
    this->reorder_extruders(first_extruder);

    this->fill_wipe_tower_partitions(print.config, object_bottom_z);

    this->collect_extruder_statistics(prime_multi_material);
}

ToolOrdering::LayerTools&  ToolOrdering::tools_for_layer(coordf_t print_z)
{
    auto it_layer_tools = std::lower_bound(m_layer_tools.begin(), m_layer_tools.end(), ToolOrdering::LayerTools(print_z - EPSILON));
    assert(it_layer_tools != m_layer_tools.end());
    coordf_t dist_min = std::abs(it_layer_tools->print_z - print_z);
	for (++ it_layer_tools; it_layer_tools != m_layer_tools.end(); ++it_layer_tools) {
        coordf_t d = std::abs(it_layer_tools->print_z - print_z);
        if (d >= dist_min)
            break;
        dist_min = d;
    }
    -- it_layer_tools;
    assert(dist_min < EPSILON);
    return *it_layer_tools;
}

void ToolOrdering::initialize_layers(std::vector<coordf_t> &zs)
{
    sort_remove_duplicates(zs);
    // Merge numerically very close Z values.
    for (size_t i = 0; i < zs.size();) {
        // Find the last layer with roughly the same print_z.
        size_t j = i + 1;
        coordf_t zmax = zs[i] + EPSILON;
        for (; j < zs.size() && zs[j] <= zmax; ++ j) ;
        // Assign an average print_z to the set of layers with nearly equal print_z.
        m_layer_tools.emplace_back(LayerTools(0.5 * (zs[i] + zs[j-1])));
        i = j;
    }
}

// Collect extruders reuqired to print layers.
void ToolOrdering::collect_extruders(const PrintObject &object)
{
    // Collect the support extruders.
    for (auto support_layer : object.support_layers) {
        LayerTools   &layer_tools = this->tools_for_layer(support_layer->print_z);
        ExtrusionRole role = support_layer->support_fills.role();
        bool         has_support        = role == erMixed || role == erSupportMaterial;
        bool         has_interface      = role == erMixed || role == erSupportMaterialInterface;
        unsigned int extruder_support   = object.config.support_material_extruder.value;
        unsigned int extruder_interface = object.config.support_material_interface_extruder.value;
        if (has_support)
            layer_tools.extruders.push_back(extruder_support);
        if (has_interface)
            layer_tools.extruders.push_back(extruder_interface);
        if (has_support || has_interface)
            layer_tools.has_support = true;
    }
    // Collect the object extruders.
    for (auto layer : object.layers) {
        LayerTools &layer_tools = this->tools_for_layer(layer->print_z);
        // What extruders are required to print this object layer?
        for (size_t region_id = 0; region_id < object.print()->regions.size(); ++ region_id) {
			const LayerRegion *layerm = (region_id < layer->regions.size()) ? layer->regions[region_id] : nullptr;
            if (layerm == nullptr)
                continue;
            const PrintRegion &region = *object.print()->regions[region_id];
            if (! layerm->perimeters.entities.empty()) {
                layer_tools.extruders.push_back(region.config.perimeter_extruder.value);
                layer_tools.has_object = true;
            }
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
                layer_tools.extruders.push_back(region.config.solid_infill_extruder);
            if (has_infill)
                layer_tools.extruders.push_back(region.config.infill_extruder);
            if (has_solid_infill || has_infill)
                layer_tools.has_object = true;
        }
    }

    // Sort and remove duplicates
    for (LayerTools &lt : m_layer_tools)
        sort_remove_duplicates(lt.extruders);
}

// Reorder extruders to minimize layer changes.
void ToolOrdering::reorder_extruders(unsigned int last_extruder_id)
{
    if (m_layer_tools.empty())
        return;

    if (last_extruder_id == (unsigned int)-1) {
        // The initial print extruder has not been decided yet.
        // Initialize the last_extruder_id with the first non-zero extruder id used for the print.
        last_extruder_id = 0;
        for (size_t i = 0; i < m_layer_tools.size() && last_extruder_id == 0; ++ i) {
            const LayerTools &lt = m_layer_tools[i];
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

    for (LayerTools &lt : m_layer_tools) {
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
    for (LayerTools &lt : m_layer_tools)
        for (unsigned int &extruder_id : lt.extruders) {
            assert(extruder_id > 0);
            -- extruder_id;
        }
}

void ToolOrdering::fill_wipe_tower_partitions(const PrintConfig &config, coordf_t object_bottom_z)
{
    if (m_layer_tools.empty())
        return;

    // Count the minimum number of tool changes per layer.
    size_t last_extruder = size_t(-1);
    for (LayerTools &lt : m_layer_tools) {
        lt.wipe_tower_partitions = lt.extruders.size();
        if (! lt.extruders.empty()) {
            if (last_extruder == size_t(-1) || last_extruder == lt.extruders.front())
                // The first extruder on this layer is equal to the current one, no need to do an initial tool change.
                -- lt.wipe_tower_partitions;
            last_extruder = lt.extruders.back();
        }
    }

    // Propagate the wipe tower partitions down to support the upper partitions by the lower partitions.
    for (int i = int(m_layer_tools.size()) - 2; i >= 0; -- i)
        m_layer_tools[i].wipe_tower_partitions = std::max(m_layer_tools[i + 1].wipe_tower_partitions, m_layer_tools[i].wipe_tower_partitions);

    //FIXME this is a hack to get the ball rolling.
    for (LayerTools &lt : m_layer_tools)
        lt.has_wipe_tower = (lt.has_object && lt.wipe_tower_partitions > 0) || lt.print_z < object_bottom_z + EPSILON;

    // Test for a raft, insert additional wipe tower layer to fill in the raft separation gap.
    double max_layer_height = std::numeric_limits<double>::max();
    for (size_t i = 0; i < config.nozzle_diameter.values.size(); ++ i) {
        double mlh = config.max_layer_height.values[i];
        if (mlh == 0.)
            mlh = 0.75 * config.nozzle_diameter.values[i];
        max_layer_height = std::min(max_layer_height, mlh);
    }
    for (size_t i = 0; i + 1 < m_layer_tools.size(); ++ i) {
        const LayerTools &lt      = m_layer_tools[i];
        const LayerTools &lt_next = m_layer_tools[i + 1];
        if (lt.print_z < object_bottom_z + EPSILON && lt_next.print_z >= object_bottom_z + EPSILON) {
            // lt is the last raft layer. Find the 1st object layer.
            size_t j = i + 1;
            for (; j < m_layer_tools.size() && ! m_layer_tools[j].has_wipe_tower; ++ j);
            if (j < m_layer_tools.size()) {
                const LayerTools &lt_object = m_layer_tools[j];
                coordf_t gap = lt_object.print_z - lt.print_z;
                assert(gap > 0.f);
                if (gap > max_layer_height + EPSILON) {
                    // Insert one additional wipe tower layer between lh.print_z and lt_object.print_z.
                    LayerTools lt_new(0.5f * (lt.print_z + lt_object.print_z));
                    // Find the 1st layer above lt_new.
                    for (j = i + 1; j < m_layer_tools.size() && m_layer_tools[j].print_z < lt_new.print_z - EPSILON; ++ j);
					if (std::abs(m_layer_tools[j].print_z - lt_new.print_z) < EPSILON) {
						m_layer_tools[j].has_wipe_tower = true;
					} else {
						LayerTools &lt_extra = *m_layer_tools.insert(m_layer_tools.begin() + j, lt_new);
                        LayerTools &lt_prev  = m_layer_tools[j - 1];
                        LayerTools &lt_next  = m_layer_tools[j + 1];
                        assert(! lt_prev.extruders.empty() && ! lt_next.extruders.empty());
                        assert(lt_prev.extruders.back() == lt_next.extruders.front());
						lt_extra.has_wipe_tower = true;
                        lt_extra.extruders.push_back(lt_next.extruders.front());
						lt_extra.wipe_tower_partitions = lt_next.wipe_tower_partitions;
					}
                }
            }
            break;
        }
    }

    // Calculate the wipe_tower_layer_height values.
    coordf_t wipe_tower_print_z_last = 0.;
    for (LayerTools &lt : m_layer_tools)
        if (lt.has_wipe_tower) {
            lt.wipe_tower_layer_height = lt.print_z - wipe_tower_print_z_last;
            wipe_tower_print_z_last = lt.print_z;
        }
}

void ToolOrdering::collect_extruder_statistics(bool prime_multi_material)
{
    m_first_printing_extruder = (unsigned int)-1;
    for (const auto &lt : m_layer_tools)
        if (! lt.extruders.empty()) {
            m_first_printing_extruder = lt.extruders.front();
            break;
        }

    m_last_printing_extruder = (unsigned int)-1;
    for (auto lt_it = m_layer_tools.rbegin(); lt_it != m_layer_tools.rend(); ++ lt_it)
        if (! lt_it->extruders.empty()) {
            m_last_printing_extruder = lt_it->extruders.back();
            break;
        }

    m_all_printing_extruders.clear();
    for (const auto &lt : m_layer_tools) {
        append(m_all_printing_extruders, lt.extruders);
        sort_remove_duplicates(m_all_printing_extruders);
    }

    if (prime_multi_material && ! m_all_printing_extruders.empty()) {
        // Reorder m_all_printing_extruders in the sequence they will be primed, the last one will be m_first_printing_extruder.
        // Then set m_first_printing_extruder to the 1st extruder primed.
        m_all_printing_extruders.erase(
            std::remove_if(m_all_printing_extruders.begin(), m_all_printing_extruders.end(), 
                [ this ](const unsigned int eid) { return eid == m_first_printing_extruder; }),
            m_all_printing_extruders.end());
        m_all_printing_extruders.emplace_back(m_first_printing_extruder);
        m_first_printing_extruder = m_all_printing_extruders.front();
    }
}

} // namespace Slic3r
