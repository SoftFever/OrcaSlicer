// Tree supports by Thomas Rahm, losely based on Tree Supports by CuraEngine.
// Original source of Thomas Rahm's tree supports:
// https://github.com/ThomasRahm/CuraEngine
//
// Original CuraEngine copyright:
// Copyright (c) 2021 Ultimaker B.V.
// CuraEngine is released under the terms of the AGPLv3 or higher.

#ifndef slic3r_TreeSupport_hpp
#define slic3r_TreeSupport_hpp

#include "TreeModelVolumes.hpp"
#include "Point.hpp"

#include <boost/container/small_vector.hpp>

#include "BoundingBox.hpp"
#include "Utils.hpp"

// #define TREE_SUPPORT_SHOW_ERRORS

#ifdef SLIC3R_TREESUPPORTS_PROGRESS
    // The various stages of the process can be weighted differently in the progress bar.
    // These weights are obtained experimentally using a small sample size. Sensible weights can differ drastically based on the assumed default settings and model.
    #define TREE_PROGRESS_TOTAL 10000
    #define TREE_PROGRESS_PRECALC_COLL TREE_PROGRESS_TOTAL * 0.1
    #define TREE_PROGRESS_PRECALC_AVO TREE_PROGRESS_TOTAL * 0.4
    #define TREE_PROGRESS_GENERATE_NODES TREE_PROGRESS_TOTAL * 0.1
    #define TREE_PROGRESS_AREA_CALC TREE_PROGRESS_TOTAL * 0.3
    #define TREE_PROGRESS_DRAW_AREAS TREE_PROGRESS_TOTAL * 0.1
    #define TREE_PROGRESS_GENERATE_BRANCH_AREAS TREE_PROGRESS_DRAW_AREAS / 3
    #define TREE_PROGRESS_SMOOTH_BRANCH_AREAS TREE_PROGRESS_DRAW_AREAS / 3
    #define TREE_PROGRESS_FINALIZE_BRANCH_AREAS TREE_PROGRESS_DRAW_AREAS / 3
#endif // SLIC3R_TREESUPPORTS_PROGRESS

namespace Slic3r
{

// Forward declarations
class TreeSupport;
class Print;
class PrintObject;
class SupportGeneratorLayer;
using SupportGeneratorLayerStorage  = std::deque<SupportGeneratorLayer>;
using SupportGeneratorLayersPtr     = std::vector<SupportGeneratorLayer*>;

namespace TreeSupport3D
{

using LayerIndex = int;

static constexpr const double  SUPPORT_TREE_EXPONENTIAL_FACTOR = 1.5;
static constexpr const coord_t SUPPORT_TREE_EXPONENTIAL_THRESHOLD = scaled<coord_t>(1. * SUPPORT_TREE_EXPONENTIAL_FACTOR);
static constexpr const coord_t SUPPORT_TREE_COLLISION_RESOLUTION = scaled<coord_t>(0.5);

// The number of vertices in each circle.
static constexpr const size_t SUPPORT_TREE_CIRCLE_RESOLUTION = 25;
static constexpr const bool SUPPORT_TREE_AVOID_SUPPORT_BLOCKER = true;

enum class InterfacePreference
{
    InterfaceAreaOverwritesSupport,
    SupportAreaOverwritesInterface,
    InterfaceLinesOverwriteSupport,
    SupportLinesOverwriteInterface,
    Nothing
};

struct AreaIncreaseSettings
{
    AreaIncreaseSettings(
        TreeModelVolumes::AvoidanceType type = TreeModelVolumes::AvoidanceType::Fast, coord_t increase_speed = 0, 
        bool increase_radius = false, bool no_error = false, bool use_min_distance = false, bool move = false) :
        increase_speed{ increase_speed }, type{ type }, increase_radius{ increase_radius }, no_error{ no_error }, use_min_distance{ use_min_distance }, move{ move } {}

    coord_t         increase_speed;
    // Packing for smaller memory footprint of SupportElementState && SupportElementMerging
    TreeModelVolumes::AvoidanceType type;
    bool            increase_radius  : 1;
    bool            no_error         : 1;
    bool            use_min_distance : 1;
    bool            move             : 1;
    bool operator==(const AreaIncreaseSettings& other) const
    {
        return type             == other.type               &&
               increase_speed   == other.increase_speed     &&
               increase_radius  == other.increase_radius    &&
               no_error         == other.no_error           &&
               use_min_distance == other.use_min_distance   &&
               move             == other.move;
    }
};

struct TreeSupportSettings;

// C++17 does not support in place initializers of bit values, thus a constructor zeroing the bits is provided.
struct SupportElementStateBits {
    SupportElementStateBits() :
        to_buildplate(false),
        to_model_gracious(false),
        use_min_xy_dist(false),
        supports_roof(false),
        can_use_safe_radius(false),
        skip_ovalisation(false),
        deleted(false),
        marked(false)
        {}

    /*!
     * \brief The element trys to reach the buildplate
     */
    bool to_buildplate : 1;

    /*!
     * \brief Will the branch be able to rest completely on a flat surface, be it buildplate or model ?
     */
    bool to_model_gracious : 1;

    /*!
     * \brief Whether the min_xy_distance can be used to get avoidance or similar. Will only be true if support_xy_overrides_z=Z overrides X/Y.
     */
    bool use_min_xy_dist : 1;

    /*!
     * \brief True if this Element or any parent provides support to a support roof.
     */
    bool supports_roof : 1;

    /*!
     * \brief An influence area is considered safe when it can use the holefree avoidance <=> It will not have to encounter holes on its way downward.
     */
    bool can_use_safe_radius : 1;

    /*!
     * \brief Skip the ovalisation to parent and children when generating the final circles.
     */
    bool skip_ovalisation : 1;

    // Not valid anymore, to be deleted.
    bool deleted : 1;

    // General purpose flag marking a visited element.
    bool marked : 1;
};

struct SupportElementState : public SupportElementStateBits
{
    int type;

    /*!
     * \brief The layer this support elements wants reach
     */
    LayerIndex  target_height;

    /*!
     * \brief The position this support elements wants to support on layer=target_height
     */
    Point       target_position;

    /*!
     * \brief The next position this support elements wants to reach. NOTE: This is mainly a suggestion regarding direction inside the influence area.
     */
    Point       next_position;

    /*!
     * \brief The next height this support elements wants to reach
     */
    LayerIndex  layer_idx;

    /*!
     * \brief The Effective distance to top of this element regarding radius increases and collision calculations.
     */
    uint32_t    effective_radius_height;

    /*!
     * \brief The amount of layers this element is below the topmost layer of this branch.
     */
    uint32_t    distance_to_top;

    /*!
     * \brief The resulting center point around which a circle will be drawn later.
     * Will be set by setPointsOnAreas
     */
    Point result_on_layer { std::numeric_limits<coord_t>::max(), std::numeric_limits<coord_t>::max() };
    bool  result_on_layer_is_set() const { return this->result_on_layer != Point{ std::numeric_limits<coord_t>::max(), std::numeric_limits<coord_t>::max() }; }
    void  result_on_layer_reset() { this->result_on_layer = Point{ std::numeric_limits<coord_t>::max(), std::numeric_limits<coord_t>::max() }; }
    /*!
     * \brief The amount of extra radius we got from merging branches that could have reached the buildplate, but merged with ones that can not.
     */
    coord_t     increased_to_model_radius; // how much to model we increased only relevant for merging

    /*!
     * \brief Counter about the times the elephant foot was increased. Can be fractions for merge reasons.
     */
    double      elephant_foot_increases;

    /*!
     * \brief The element trys not to move until this dtt is reached, is set to 0 if the element had to move.
     */
    uint32_t    dont_move_until;

    /*!
     * \brief Settings used to increase the influence area to its current state.
     */
    AreaIncreaseSettings last_area_increase;

    /*!
     * \brief Amount of roof layers that were not yet added, because the branch needed to move.
     */
    uint32_t    missing_roof_layers;

    // called by increase_single_area() and increaseAreas()
    [[nodiscard]] static SupportElementState propagate_down(const SupportElementState &src)
    {
        SupportElementState dst{ src };
        ++ dst.distance_to_top;
        -- dst.layer_idx;
        // set to invalid as we are a new node on a new layer
        dst.result_on_layer_reset();
        dst.skip_ovalisation = false;
        return dst;
    }
};

struct SupportElement
{
    using ParentIndices =
#ifdef NDEBUG
        // To reduce memory allocation in release mode.
        boost::container::small_vector<int32_t, 4>;
#else // NDEBUG
        // To ease debugging.
        std::vector<int32_t>;
#endif // NDEBUG

//    SupportElement(const SupportElementState &state) : SupportElementState(state) {}
    SupportElement(const SupportElementState &state, Polygons &&influence_area) : state(state), influence_area(std::move(influence_area)) {}
    SupportElement(const SupportElementState &state, ParentIndices &&parents, Polygons &&influence_area) :
        state(state), parents(std::move(parents)), influence_area(std::move(influence_area)) {}

    SupportElementState         state;

    /*!
     * \brief All elements in the layer above the current one that are supported by this element
     */
    ParentIndices               parents;

    /*!
     * \brief The resulting influence area.
     * Will only be set in the results of createLayerPathing, and will be nullptr inside!
     */
    Polygons                    influence_area;
};

/*!
 * \brief This struct contains settings used in the tree support. Thanks to this most functions do not need to know of meshes etc. Also makes the code shorter.
 */
struct TreeSupportSettings
{
    TreeSupportSettings() = default; // required for the definition of the config variable in the TreeSupportGenerator class.

    explicit TreeSupportSettings(const TreeSupportMeshGroupSettings& mesh_group_settings)
        : angle(mesh_group_settings.support_tree_angle),
          angle_slow(mesh_group_settings.support_tree_angle_slow),
          support_line_width(mesh_group_settings.support_line_width),
          layer_height(mesh_group_settings.layer_height),
          branch_radius(mesh_group_settings.support_tree_branch_diameter / 2),
          min_radius(mesh_group_settings.support_tree_tip_diameter / 2), // The actual radius is 50 microns larger as the resulting branches will be increased by 50 microns to avoid rounding errors effectively increasing the xydistance
          maximum_move_distance((angle < M_PI / 2.) ? (coord_t)(tan(angle) * layer_height) : std::numeric_limits<coord_t>::max()),
          maximum_move_distance_slow((angle_slow < M_PI / 2.) ? (coord_t)(tan(angle_slow) * layer_height) : std::numeric_limits<coord_t>::max()),
          support_bottom_layers(mesh_group_settings.support_bottom_enable ? (mesh_group_settings.support_bottom_height + layer_height / 2) / layer_height : 0),
          tip_layers(std::max((branch_radius - min_radius) / (support_line_width / 3), branch_radius / layer_height)), // Ensure lines always stack nicely even if layer height is large
          diameter_angle_scale_factor(sin(mesh_group_settings.support_tree_branch_diameter_angle) * layer_height / branch_radius),
          max_to_model_radius_increase(mesh_group_settings.support_tree_max_diameter_increase_by_merges_when_support_to_model / 2),
          min_dtt_to_model(round_up_divide(mesh_group_settings.support_tree_min_height_to_model, layer_height)),
          increase_radius_until_radius(mesh_group_settings.support_tree_branch_diameter / 2),
          increase_radius_until_layer(increase_radius_until_radius <= branch_radius ? tip_layers * (increase_radius_until_radius / branch_radius) : (increase_radius_until_radius - branch_radius) / (branch_radius * diameter_angle_scale_factor)),
          support_rests_on_model(! mesh_group_settings.support_material_buildplate_only),
          xy_distance(mesh_group_settings.support_xy_distance),
          xy_min_distance(std::min(mesh_group_settings.support_xy_distance, mesh_group_settings.support_xy_distance_overhang)),
          bp_radius(mesh_group_settings.support_tree_bp_diameter / 2),
          diameter_scale_bp_radius(std::min(sin(0.7) * layer_height / branch_radius, 1.0 / (branch_radius / (support_line_width / 2.0)))), // Either 40? or as much as possible so that 2 lines will overlap by at least 50%, whichever is smaller.
          z_distance_top_layers(round_up_divide(mesh_group_settings.support_top_distance, layer_height)),
          z_distance_bottom_layers(round_up_divide(mesh_group_settings.support_bottom_distance, layer_height)),
          performance_interface_skip_layers(round_up_divide(mesh_group_settings.support_interface_skip_height, layer_height)),
//              support_infill_angles(mesh_group_settings.support_infill_angles),
          support_roof_angles(mesh_group_settings.support_roof_angles),
          roof_pattern(mesh_group_settings.support_roof_pattern),
          support_pattern(mesh_group_settings.support_pattern),
          support_roof_line_width(mesh_group_settings.support_roof_line_width),
          support_line_spacing(mesh_group_settings.support_line_spacing),
          support_bottom_offset(mesh_group_settings.support_bottom_offset),
          support_wall_count(mesh_group_settings.support_wall_count),
          resolution(mesh_group_settings.resolution),
          support_roof_line_distance(mesh_group_settings.support_roof_line_distance), // in the end the actual infill has to be calculated to subtract interface from support areas according to interface_preference.
          settings(mesh_group_settings),
          min_feature_size(mesh_group_settings.min_feature_size)


    {
        layer_start_bp_radius = (bp_radius - branch_radius) / (branch_radius * diameter_scale_bp_radius);

        if (TreeSupportSettings::soluble) {
            // safeOffsetInc can only work in steps of the size xy_min_distance in the worst case => xy_min_distance has to be a bit larger than 0 in this worst case and should be large enough for performance to not suffer extremely
            // When for all meshes the z bottom and top distance is more than one layer though the worst case is xy_min_distance + min_feature_size
            // This is not the best solution, but the only one to ensure areas can not lag though walls at high maximum_move_distance.
            xy_min_distance = std::max(xy_min_distance, scaled<coord_t>(0.1));
            xy_distance     = std::max(xy_distance, xy_min_distance);
        }
        

//            const std::unordered_map<std::string, InterfacePreference> interface_map = { { "support_area_overwrite_interface_area", InterfacePreference::SupportAreaOverwritesInterface }, { "interface_area_overwrite_support_area", InterfacePreference::InterfaceAreaOverwritesSupport }, { "support_lines_overwrite_interface_area", InterfacePreference::SupportLinesOverwriteInterface }, { "interface_lines_overwrite_support_area", InterfacePreference::InterfaceLinesOverwriteSupport }, { "nothing", InterfacePreference::Nothing } };
//            interface_preference = interface_map.at(mesh_group_settings.get<std::string>("support_interface_priority"));
//FIXME this was the default
//            interface_preference = InterfacePreference::SupportLinesOverwriteInterface;
        interface_preference = InterfacePreference::SupportAreaOverwritesInterface;
    }

private:
    double angle;
    double angle_slow;
    std::vector<coord_t> known_z;

public:
    // some static variables dependent on other meshes that are not currently processed.
    // Has to be static because TreeSupportConfig will be used in TreeModelVolumes as this reduces redundancy.
    inline static bool soluble = false;
    /*!
     * \brief Width of a single line of support.
     */
    coord_t support_line_width;
    /*!
     * \brief Height of a single layer
     */
    coord_t layer_height;
    /*!
     * \brief Radius of a branch when it has left the tip.
     */
    coord_t branch_radius;
    /*!
     * \brief smallest allowed radius, required to ensure that even at DTT 0 every circle will still be printed
     */
    coord_t min_radius;
    /*!
     * \brief How far an influence area may move outward every layer at most.
     */
    coord_t maximum_move_distance;
    /*!
     * \brief How far every influence area will move outward every layer if possible.
     */
    coord_t maximum_move_distance_slow;
    /*!
     * \brief Amount of bottom layers. 0 if disabled.
     */
    size_t support_bottom_layers;
    /*!
     * \brief Amount of effectiveDTT increases are required to reach branch radius.
     */
    size_t tip_layers;
    /*!
     * \brief Factor by which to increase the branch radius.
     */
    double diameter_angle_scale_factor;
    /*!
     * \brief How much a branch resting on the model may grow in radius by merging with branches that can reach the buildplate.
     */
    coord_t max_to_model_radius_increase;
    /*!
     * \brief If smaller (in layers) than that, all branches to model will be deleted
     */
    size_t min_dtt_to_model;
    /*!
     * \brief Increase radius in the resulting drawn branches, even if the avoidance does not allow it. Will be cut later to still fit.
     */
    coord_t increase_radius_until_radius;
    /*!
     * \brief Same as increase_radius_until_radius, but contains the DTT at which the radius will be reached.
     */
    size_t increase_radius_until_layer;
    /*!
     * \brief True if the branches may connect to the model.
     */
    bool support_rests_on_model;
    /*!
     * \brief How far should support be from the model.
     */
    coord_t xy_distance;
    /*!
     * \brief Radius a branch should have when reaching the buildplate.
     */
    coord_t bp_radius;
    /*!
     * \brief The layer index at which an increase in radius may be required to reach the bp_radius.
     */
    coord_t layer_start_bp_radius;
    /*!
     * \brief Factor by which to increase the branch radius to reach the required bp_radius at layer 0. Note that this radius increase will not happen in the tip, to ensure the tip is structurally sound.
     */
    double diameter_scale_bp_radius;
    /*!
     * \brief minimum xy_distance. Only relevant when Z overrides XY, otherwise equal to xy_distance-
     */
    coord_t xy_min_distance;
    /*!
     * \brief Amount of layers distance required the top of the support to the model
     */
    size_t z_distance_top_layers;
    /*!
     * \brief Amount of layers distance required from the top of the model to the bottom of a support structure.
     */
    size_t z_distance_bottom_layers;
    /*!
     * \brief used for performance optimization at the support floor. Should have no impact on the resulting tree.
     */
    size_t performance_interface_skip_layers;
    /*!
     * \brief User specified angles for the support infill.
     */
//        std::vector<double> support_infill_angles;
    /*!
     * \brief User specified angles for the support roof infill.
     */
    std::vector<double> support_roof_angles;
    /*!
     * \brief Pattern used in the support roof. May contain non relevant data if support roof is disabled.
     */
    SupportMaterialInterfacePattern roof_pattern;
    /*!
     * \brief Pattern used in the support infill.
     */
    SupportMaterialPattern support_pattern;
    /*!
     * \brief Line width of the support roof.
     */
    coord_t support_roof_line_width;
    /*!
     * \brief Distance between support infill lines.
     */
    coord_t support_line_spacing;
    /*!
     * \brief Offset applied to the support floor area.
     */
    coord_t support_bottom_offset;
    /*
     * \brief Amount of walls the support area will have.
     */
    int support_wall_count;
    /*
     * \brief Maximum allowed deviation when simplifying.
     */
    coord_t resolution;
    /*
     * \brief Distance between the lines of the roof.
     */
    coord_t support_roof_line_distance;
    /*
     * \brief How overlaps of an interface area with a support area should be handled.
     */
    InterfacePreference interface_preference;

    /*
     * \brief The infill class wants a settings object. This one will be the correct one for all settings it uses.
     */
    TreeSupportMeshGroupSettings settings;

    /*
     * \brief Minimum thickness of any model features.
     */
    coord_t min_feature_size;

  public:
    bool operator==(const TreeSupportSettings& other) const
    {
        return branch_radius == other.branch_radius && tip_layers == other.tip_layers && diameter_angle_scale_factor == other.diameter_angle_scale_factor && layer_start_bp_radius == other.layer_start_bp_radius && bp_radius == other.bp_radius && diameter_scale_bp_radius == other.diameter_scale_bp_radius && min_radius == other.min_radius && xy_min_distance == other.xy_min_distance && // as a recalculation of the collision areas is required to set a new min_radius.
               xy_distance - xy_min_distance == other.xy_distance - other.xy_min_distance && // if the delta of xy_min_distance and xy_distance is different the collision areas have to be recalculated.
               support_rests_on_model == other.support_rests_on_model && increase_radius_until_layer == other.increase_radius_until_layer && min_dtt_to_model == other.min_dtt_to_model && max_to_model_radius_increase == other.max_to_model_radius_increase && maximum_move_distance == other.maximum_move_distance && maximum_move_distance_slow == other.maximum_move_distance_slow && z_distance_bottom_layers == other.z_distance_bottom_layers && support_line_width == other.support_line_width && 
               support_line_spacing == other.support_line_spacing && support_roof_line_width == other.support_roof_line_width && // can not be set on a per-mesh basis currently, so code to enable processing different roof line width in the same iteration seems useless.
               support_bottom_offset == other.support_bottom_offset && support_wall_count == other.support_wall_count && support_pattern == other.support_pattern && roof_pattern == other.roof_pattern && // can not be set on a per-mesh basis currently, so code to enable processing different roof patterns in the same iteration seems useless.
               support_roof_angles == other.support_roof_angles && 
               //support_infill_angles == other.support_infill_angles && 
               increase_radius_until_radius == other.increase_radius_until_radius && support_bottom_layers == other.support_bottom_layers && layer_height == other.layer_height && z_distance_top_layers == other.z_distance_top_layers && resolution == other.resolution && // Infill generation depends on deviation and resolution.
               support_roof_line_distance == other.support_roof_line_distance && interface_preference == other.interface_preference
               && min_feature_size == other.min_feature_size // interface_preference should be identical to ensure the tree will correctly interact with the roof.
               // The infill class now wants the settings object and reads a lot of settings, and as the infill class is used to calculate support roof lines for interface-preference. Not all of these may be required to be identical, but as I am not sure, better safe than sorry
#if 0
                && (interface_preference == InterfacePreference::InterfaceAreaOverwritesSupport || interface_preference == InterfacePreference::SupportAreaOverwritesInterface
                // Perimeter generator parameters
                   || 
                        (settings.get<bool>("fill_outline_gaps") == other.settings.get<bool>("fill_outline_gaps") && 
                         settings.get<coord_t>("min_bead_width") == other.settings.get<coord_t>("min_bead_width") && 
                         settings.get<double>("wall_transition_angle") == other.settings.get<double>("wall_transition_angle") && 
                         settings.get<coord_t>("wall_transition_length") == other.settings.get<coord_t>("wall_transition_length") && 
                         settings.get<Ratio>("wall_split_middle_threshold") == other.settings.get<Ratio>("wall_split_middle_threshold") && 
                         settings.get<Ratio>("wall_add_middle_threshold") == other.settings.get<Ratio>("wall_add_middle_threshold") && 
                         settings.get<int>("wall_distribution_count") == other.settings.get<int>("wall_distribution_count") && 
                         settings.get<coord_t>("wall_transition_filter_distance") == other.settings.get<coord_t>("wall_transition_filter_distance") && 
                         settings.get<coord_t>("wall_transition_filter_deviation") == other.settings.get<coord_t>("wall_transition_filter_deviation") && 
                         settings.get<coord_t>("wall_line_width_x") == other.settings.get<coord_t>("wall_line_width_x") && 
                         settings.get<int>("meshfix_maximum_extrusion_area_deviation") == other.settings.get<int>("meshfix_maximum_extrusion_area_deviation"))
                    )
#endif
            ;
    }

    /*!
     * \brief Get the Distance to top regarding the real radius this part will have. This is different from distance_to_top, which is can be used to calculate the top most layer of the branch.
     * \param elem[in] The SupportElement one wants to know the effectiveDTT
     * \return The Effective DTT.
     */
    [[nodiscard]] inline size_t getEffectiveDTT(const SupportElementState &elem) const
    {
        return elem.effective_radius_height < increase_radius_until_layer ? (elem.distance_to_top < increase_radius_until_layer ? elem.distance_to_top : increase_radius_until_layer) : elem.effective_radius_height;
    }

    /*!
     * \brief Get the Radius part will have based on numeric values.
     * \param distance_to_top[in] The effective distance_to_top of the element
     * \param elephant_foot_increases[in] The elephant_foot_increases of the element.
     * \return The radius an element with these attributes would have.
     */
    [[nodiscard]] inline coord_t getRadius(size_t distance_to_top, const double elephant_foot_increases = 0) const
    {
        return (distance_to_top <= tip_layers ? min_radius + (branch_radius - min_radius) * distance_to_top / tip_layers : // tip
                       branch_radius + // base
                           branch_radius * (distance_to_top - tip_layers) * diameter_angle_scale_factor)
               + // gradual increase
               branch_radius * elephant_foot_increases * (std::max(diameter_scale_bp_radius - diameter_angle_scale_factor, 0.0));
    }

    /*!
     * \brief Get the Radius, that this element will have.
     * \param elem[in] The Element.
     * \return The radius the element has.
     */
    [[nodiscard]] inline coord_t getRadius(const SupportElementState &elem) const
        { return getRadius(getEffectiveDTT(elem), elem.elephant_foot_increases); }
    [[nodiscard]] inline coord_t getRadius(const SupportElement &elem) const
        { return this->getRadius(elem.state); }

    /*!
     * \brief Get the collision Radius of this Element. This can be smaller then the actual radius, as the drawAreas will cut off areas that may collide with the model.
     * \param elem[in] The Element.
     * \return The collision radius the element has.
     */
    [[nodiscard]] inline coord_t getCollisionRadius(const SupportElementState &elem) const
    {
        return getRadius(elem.effective_radius_height, elem.elephant_foot_increases);
    }

    /*!
     * \brief Get the Radius an element should at least have at a given layer.
     * \param layer_idx[in] The layer.
     * \return The radius every element should aim to achieve.
     */
    [[nodiscard]] inline coord_t recommendedMinRadius(LayerIndex layer_idx) const
    {
        double scale = (layer_start_bp_radius - int(layer_idx)) * diameter_scale_bp_radius;
        return scale > 0 ? branch_radius + branch_radius * scale : 0;
    }

    /*!
     * \brief Return on which z in microns the layer will be printed. Used only for support infill line generation.
     * \param layer_idx[in] The layer.
     * \return The radius every element should aim to achieve.
     */
    [[nodiscard]] inline coord_t getActualZ(LayerIndex layer_idx)
    {
        return layer_idx < coord_t(known_z.size()) ? known_z[layer_idx] : (layer_idx - known_z.size()) * layer_height + known_z.size() ? known_z.back() : 0;
    }

    /*!
     * \brief Set the z every Layer is printed at. Required for getActualZ to work
     * \param z[in] The z every LayerIndex is printed. Vector is used as a map<LayerIndex,coord_t> with the index of each element being the corresponding LayerIndex
     * \return The radius every element should aim to achieve.
     */
    void setActualZ(std::vector<coord_t>& z)
    {
        known_z = z;
    }
};

void tree_supports_show_error(std::string_view message, bool critical);

using SupportElements = std::deque<SupportElement>;
void create_layer_pathing(const TreeModelVolumes& volumes, const TreeSupportSettings& config, std::vector<SupportElements>& move_bounds, std::function<void()> throw_on_cancel);

void create_nodes_from_area(const TreeModelVolumes& volumes, const TreeSupportSettings& config, std::vector<SupportElements>& move_bounds, std::function<void()> throw_on_cancel);

indexed_triangle_set draw_branches(PrintObject& print_object, const TreeModelVolumes& volumes, const TreeSupportSettings& config, std::vector<SupportElements>& move_bounds, std::function<void()> throw_on_cancel);

void slice_branches(PrintObject& print_object, const TreeModelVolumes& volumes, const TreeSupportSettings& config, const std::vector<Polygons>& overhangs, std::vector<SupportElements>& move_bounds, const indexed_triangle_set& cummulative_mesh, SupportGeneratorLayersPtr& bottom_contacts, SupportGeneratorLayersPtr& top_contacts, SupportGeneratorLayersPtr& intermediate_layers, SupportGeneratorLayerStorage& layer_storage, std::function<void()> throw_on_cancel);

} // namespace TreeSupport3D

void generate_tree_support_3D(PrintObject &print_object, TreeSupport* tree_support, std::function<void()> throw_on_cancel = []{});

} // namespace Slic3r

#endif /* slic3r_TreeSupport_hpp */
