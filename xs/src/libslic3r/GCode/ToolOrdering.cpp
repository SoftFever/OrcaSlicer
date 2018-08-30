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


// Returns true in case that extruder a comes before b (b does not have to be present). False otherwise.
bool LayerTools::is_extruder_order(unsigned int a, unsigned int b) const
{
    if (a==b)
        return false;

    for (auto extruder : extruders) {
        if (extruder == a)
            return true;
        if (extruder == b)
            return false;
    }

    return false;
}


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
    m_print_config_ptr = &print.config;

    PrintObjectPtrs objects = print.get_printable_objects();
    // Initialize the print layers for all objects and all layers.
    coordf_t object_bottom_z = 0.;
    {
        std::vector<coordf_t> zs;
        for (auto object : objects) {
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
    for (auto object : objects)
        this->collect_extruders(*object);

    // Reorder the extruders to minimize tool switches.
    this->reorder_extruders(first_extruder);

    this->fill_wipe_tower_partitions(print.config, object_bottom_z);

    this->collect_extruder_statistics(prime_multi_material);
}


LayerTools& ToolOrdering::tools_for_layer(coordf_t print_z)
{
    auto it_layer_tools = std::lower_bound(m_layer_tools.begin(), m_layer_tools.end(), LayerTools(print_z - EPSILON));
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
        m_layer_tools.emplace_back(LayerTools(0.5 * (zs[i] + zs[j-1]), m_print_config_ptr));
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
                bool something_nonoverriddable = true;

                if (m_print_config_ptr) { // in this case complete_objects is false (see ToolOrdering constructors)
                    something_nonoverriddable = false;
                    for (const auto& eec : layerm->perimeters.entities) // let's check if there are nonoverriddable entities
                        if (!layer_tools.wiping_extrusions().is_overriddable(dynamic_cast<const ExtrusionEntityCollection&>(*eec), *m_print_config_ptr, object, region)) {
                            something_nonoverriddable = true;
                            break;
                        }
                }

                if (something_nonoverriddable)
                        layer_tools.extruders.push_back(region.config.perimeter_extruder.value);

                layer_tools.has_object = true;
            }


            bool has_infill       = false;
            bool has_solid_infill = false;
            bool something_nonoverriddable = false;
            for (const ExtrusionEntity *ee : layerm->fills.entities) {
                // fill represents infill extrusions of a single island.
                const auto *fill = dynamic_cast<const ExtrusionEntityCollection*>(ee);
                ExtrusionRole role = fill->entities.empty() ? erNone : fill->entities.front()->role();
                if (is_solid_infill(role))
                    has_solid_infill = true;
                else if (role != erNone)
                    has_infill = true;

                if (m_print_config_ptr) {
                    if (!something_nonoverriddable && !layer_tools.wiping_extrusions().is_overriddable(*fill, *m_print_config_ptr, object, region))
                        something_nonoverriddable = true;
                }
            }

            if (something_nonoverriddable || !m_print_config_ptr)
            {
                if (has_solid_infill)
                    layer_tools.extruders.push_back(region.config.solid_infill_extruder);
                if (has_infill)
                    layer_tools.extruders.push_back(region.config.infill_extruder);
            }
            if (has_solid_infill || has_infill)
                layer_tools.has_object = true;
        }
    }

    for (auto& layer : m_layer_tools) {
        // Sort and remove duplicates
        sort_remove_duplicates(layer.extruders);

        // make sure that there are some tools for each object layer (e.g. tall wiping object will result in empty extruders vector)
        if (layer.extruders.empty() && layer.has_object)
            layer.extruders.push_back(0); // 0="dontcare" extruder - it will be taken care of in reorder_extruders
    }
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



// This function is called from Print::mark_wiping_extrusions and sets extruder this entity should be printed with (-1 .. as usual)
void WipingExtrusions::set_extruder_override(const ExtrusionEntity* entity, unsigned int copy_id, int extruder, unsigned int num_of_copies)
{
    something_overridden = true;

    auto entity_map_it = (entity_map.insert(std::make_pair(entity, std::vector<int>()))).first; // (add and) return iterator
    auto& copies_vector = entity_map_it->second;
    if (copies_vector.size() < num_of_copies)
        copies_vector.resize(num_of_copies, -1);

    if (copies_vector[copy_id] != -1)
        std::cout << "ERROR: Entity extruder overriden multiple times!!!\n";    // A debugging message - this must never happen.

    copies_vector[copy_id] = extruder;
}


// Finds first non-soluble extruder on the layer
int WipingExtrusions::first_nonsoluble_extruder_on_layer(const PrintConfig& print_config) const
{
    const LayerTools& lt = *m_layer_tools;
    for (auto extruders_it = lt.extruders.begin(); extruders_it != lt.extruders.end(); ++extruders_it)
        if (!print_config.filament_soluble.get_at(*extruders_it))
            return (*extruders_it);

    return (-1);
}

// Finds last non-soluble extruder on the layer
int WipingExtrusions::last_nonsoluble_extruder_on_layer(const PrintConfig& print_config) const
{
    const LayerTools& lt = *m_layer_tools;
    for (auto extruders_it = lt.extruders.rbegin(); extruders_it != lt.extruders.rend(); ++extruders_it)
        if (!print_config.filament_soluble.get_at(*extruders_it))
            return (*extruders_it);

    return (-1);
}


// Decides whether this entity could be overridden
bool WipingExtrusions::is_overriddable(const ExtrusionEntityCollection& eec, const PrintConfig& print_config, const PrintObject& object, const PrintRegion& region) const
{
    if (print_config.filament_soluble.get_at(Print::get_extruder(eec, region)))
        return false;

    if (object.config.wipe_into_objects)
        return true;

    if (!region.config.wipe_into_infill || eec.role() != erInternalInfill)
        return false;

    return true;
}


// Following function iterates through all extrusions on the layer, remembers those that could be used for wiping after toolchange
// and returns volume that is left to be wiped on the wipe tower.
float WipingExtrusions::mark_wiping_extrusions(const Print& print, unsigned int old_extruder, unsigned int new_extruder, float volume_to_wipe)
{
    const LayerTools& lt = *m_layer_tools;
    const float min_infill_volume = 0.f; // ignore infill with smaller volume than this

    if (print.config.filament_soluble.get_at(old_extruder) || print.config.filament_soluble.get_at(new_extruder))
        return volume_to_wipe;      // Soluble filament cannot be wiped in a random infill, neither the filament after it

    // we will sort objects so that dedicated for wiping are at the beginning:
    PrintObjectPtrs object_list = print.get_printable_objects();
    std::sort(object_list.begin(), object_list.end(), [](const PrintObject* a, const PrintObject* b) { return a->config.wipe_into_objects; });

    // We will now iterate through
    //  - first the dedicated objects to mark perimeters or infills (depending on infill_first)
    //  - second through the dedicated ones again to mark infills or perimeters (depending on infill_first)
    //  - then all the others to mark infills (in case that !infill_first, we must also check that the perimeter is finished already
    // this is controlled by the following variable:
    bool perimeters_done = false;

    for (int i=0 ; i<(int)object_list.size() + (perimeters_done ? 0 : 1); ++i) {
        if (!perimeters_done && (i==(int)object_list.size() || !object_list[i]->config.wipe_into_objects)) { // we passed the last dedicated object in list
            perimeters_done = true;
            i=-1;   // let's go from the start again
            continue;
        }

        const auto& object = object_list[i];

        // Finds this layer:
        auto this_layer_it = std::find_if(object->layers.begin(), object->layers.end(), [&lt](const Layer* lay) { return std::abs(lt.print_z - lay->print_z)<EPSILON; });
        if (this_layer_it == object->layers.end())
            continue;
        const Layer* this_layer = *this_layer_it;
        unsigned int num_of_copies = object->_shifted_copies.size();

        for (unsigned int copy = 0; copy < num_of_copies; ++copy) {    // iterate through copies first, so that we mark neighbouring infills to minimize travel moves

            for (size_t region_id = 0; region_id < object->print()->regions.size(); ++ region_id) {
                const auto& region = *object->print()->regions[region_id];

                if (!region.config.wipe_into_infill && !object->config.wipe_into_objects)
                    continue;


                if ((!print.config.infill_first ? perimeters_done : !perimeters_done) || (!object->config.wipe_into_objects && region.config.wipe_into_infill)) {
                    for (const ExtrusionEntity* ee : this_layer->regions[region_id]->fills.entities) {                      // iterate through all infill Collections
                        auto* fill = dynamic_cast<const ExtrusionEntityCollection*>(ee);

                        if (!is_overriddable(*fill, print.config, *object, region))
                            continue;

                        // What extruder would this normally be printed with?
                        unsigned int correct_extruder = Print::get_extruder(*fill, region);

                        if (volume_to_wipe<=0)
                            continue;

                        if (!object->config.wipe_into_objects && !print.config.infill_first && region.config.wipe_into_infill)
                            // In this case we must check that the original extruder is used on this layer before the one we are overridding
                            // (and the perimeters will be finished before the infill is printed):
                            if (!lt.is_extruder_order(region.config.perimeter_extruder - 1, new_extruder))
                                continue;

                        if ((!is_entity_overridden(fill, copy) && fill->total_volume() > min_infill_volume)) {     // this infill will be used to wipe this extruder
                            set_extruder_override(fill, copy, new_extruder, num_of_copies);
                            volume_to_wipe -= fill->total_volume();
                        }
                    }
                }

                // Now the same for perimeters - see comments above for explanation:
                if (object->config.wipe_into_objects && (print.config.infill_first ? perimeters_done : !perimeters_done))
                {
                    for (const ExtrusionEntity* ee : this_layer->regions[region_id]->perimeters.entities) {
                        auto* fill = dynamic_cast<const ExtrusionEntityCollection*>(ee);
                        if (!is_overriddable(*fill, print.config, *object, region))
                            continue;

                        if (volume_to_wipe<=0)
                            continue;

                        if ((!is_entity_overridden(fill, copy) && fill->total_volume() > min_infill_volume)) {
                            set_extruder_override(fill, copy, new_extruder, num_of_copies);
                            volume_to_wipe -= fill->total_volume();
                        }
                    }
                }
            }
        }
    }
    return std::max(0.f, volume_to_wipe);
}



// Called after all toolchanges on a layer were mark_infill_overridden. There might still be overridable entities,
// that were not actually overridden. If they are part of a dedicated object, printing them with the extruder
// they were initially assigned to might mean violating the perimeter-infill order. We will therefore go through
// them again and make sure we override it.
void WipingExtrusions::ensure_perimeters_infills_order(const Print& print)
{
    const LayerTools& lt = *m_layer_tools;
    unsigned int first_nonsoluble_extruder = first_nonsoluble_extruder_on_layer(print.config);
    unsigned int last_nonsoluble_extruder = last_nonsoluble_extruder_on_layer(print.config);

    PrintObjectPtrs printable_objects = print.get_printable_objects();
    for (const PrintObject* object : printable_objects) {
        // Finds this layer:
        auto this_layer_it = std::find_if(object->layers.begin(), object->layers.end(), [&lt](const Layer* lay) { return std::abs(lt.print_z - lay->print_z)<EPSILON; });
        if (this_layer_it == object->layers.end())
            continue;
        const Layer* this_layer = *this_layer_it;
        unsigned int num_of_copies = object->_shifted_copies.size();

        for (unsigned int copy = 0; copy < num_of_copies; ++copy) {    // iterate through copies first, so that we mark neighbouring infills to minimize travel moves
            for (size_t region_id = 0; region_id < object->print()->regions.size(); ++ region_id) {
                const auto& region = *object->print()->regions[region_id];

                if (!region.config.wipe_into_infill && !object->config.wipe_into_objects)
                    continue;

                for (const ExtrusionEntity* ee : this_layer->regions[region_id]->fills.entities) {                      // iterate through all infill Collections
                    auto* fill = dynamic_cast<const ExtrusionEntityCollection*>(ee);

                    if (!is_overriddable(*fill, print.config, *object, region)
                     || is_entity_overridden(fill, copy) )
                        continue;

                    // This infill could have been overridden but was not - unless we do something, it could be
                    // printed before its perimeter, or not be printed at all (in case its original extruder has
                    // not been added to LayerTools
                    // Either way, we will now force-override it with something suitable:
                    if (print.config.infill_first
                    || object->config.wipe_into_objects  // in this case the perimeter is overridden, so we can override by the last one safely
                    || lt.is_extruder_order(region.config.perimeter_extruder - 1, last_nonsoluble_extruder    // !infill_first, but perimeter is already printed when last extruder prints
                    || std::find(lt.extruders.begin(), lt.extruders.end(), region.config.infill_extruder - 1) == lt.extruders.end()) // we have to force override - this could violate infill_first (FIXME)
                      )
                        set_extruder_override(fill, copy, (print.config.infill_first ? first_nonsoluble_extruder : last_nonsoluble_extruder), num_of_copies);
                    else {
                        // In this case we can (and should) leave it to be printed normally.
                        // Force overriding would mean it gets printed before its perimeter.
                    }
                }

                // Now the same for perimeters - see comments above for explanation:
                for (const ExtrusionEntity* ee : this_layer->regions[region_id]->perimeters.entities) {                      // iterate through all perimeter Collections
                    auto* fill = dynamic_cast<const ExtrusionEntityCollection*>(ee);
                    if (!is_overriddable(*fill, print.config, *object, region)
                     || is_entity_overridden(fill, copy) )
                        continue;

                    set_extruder_override(fill, copy, (print.config.infill_first ? last_nonsoluble_extruder : first_nonsoluble_extruder), num_of_copies);
                }
            }
        }
    }
}







// Following function is called from process_layer and returns pointer to vector with information about which extruders should be used for given copy of this entity.
// It first makes sure the pointer is valid (creates the vector if it does not exist) and contains a record for each copy
// It also modifies the vector in place and changes all -1 to correct_extruder_id (at the time the overrides were created, correct extruders were not known,
// so -1 was used as "print as usual".
// The resulting vector has to keep track of which extrusions are the ones that were overridden and which were not. In the extruder is used as overridden,
// its number is saved as it is (zero-based index). Usual extrusions are saved as -number-1 (unfortunately there is no negative zero).
const std::vector<int>* WipingExtrusions::get_extruder_overrides(const ExtrusionEntity* entity, int correct_extruder_id, int num_of_copies)
{
    auto entity_map_it = entity_map.find(entity);
    if (entity_map_it == entity_map.end())
        entity_map_it = (entity_map.insert(std::make_pair(entity, std::vector<int>()))).first;

    // Now the entity_map_it should be valid, let's make sure the vector is long enough:
    entity_map_it->second.resize(num_of_copies, -1);

    // Each -1 now means "print as usual" - we will replace it with actual extruder id (shifted it so we don't lose that information):
    std::replace(entity_map_it->second.begin(), entity_map_it->second.end(), -1, -correct_extruder_id-1);

    return &(entity_map_it->second);
}
    

} // namespace Slic3r
