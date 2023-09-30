#ifndef TREESUPPORT_H
#define TREESUPPORT_H

#include <forward_list>
#include <unordered_set>
#include "ExPolygon.hpp"
#include "Point.hpp"
#include "Slicing.hpp"
#include "MinimumSpanningTree.hpp"
#include "tbb/concurrent_unordered_map.h"
#include "Flow.hpp"
#include "PrintConfig.hpp"
#include "Fill/Lightning/Generator.hpp"

#ifndef SQ
#define SQ(x) ((x)*(x))
#endif

namespace Slic3r
{
class PrintObject;
class TreeSupport;
class SupportLayer;

struct LayerHeightData
{
    coordf_t print_z       = 0;
    coordf_t height        = 0;
    size_t   next_layer_nr = 0;
    LayerHeightData()      = default;
    LayerHeightData(coordf_t z, coordf_t h, size_t next_layer) : print_z(z), height(h), next_layer_nr(next_layer) {}
};

struct TreeNode {
    Vec3f pos;
    std::vector<int> children;  // index of children in the storing vector
    std::vector<int> parents;  // index of parents in the storing vector
    TreeNode(Point pt, float z) {
        pos = { float(unscale_(pt.x())),float(unscale_(pt.y())),z };
    }
};

/*!
 * \brief Lazily generates tree guidance volumes.
 *
 * \warning This class is not currently thread-safe and should not be accessed in OpenMP blocks
 */
class TreeSupportData
{
public:
    TreeSupportData() = default;
    /*!
     * \brief Construct the TreeSupportData object
     *
     * \param xy_distance The required clearance between the model and the
     * tree branches.
     * \param max_move The maximum allowable movement between nodes on
     * adjacent layers
     * \param radius_sample_resolution Sample size used to round requested node radii.
     * \param collision_resolution
     */
    TreeSupportData(const PrintObject& object, coordf_t max_move, coordf_t radius_sample_resolution, coordf_t collision_resolution);

    TreeSupportData(TreeSupportData&&) = default;
    TreeSupportData& operator=(TreeSupportData&&) = default;

    TreeSupportData(const TreeSupportData&) = delete;
    TreeSupportData& operator=(const TreeSupportData&) = delete;

    /*!
     * \brief Creates the areas that have to be avoided by the tree's branches.
     *
     * The result is a 2D area that would cause nodes of radius \p radius to
     * collide with the model.
     *
     * \param radius The radius of the node of interest
     * \param layer The layer of interest
     * \return Polygons object
     */
    const ExPolygons& get_collision(coordf_t radius, size_t layer_idx) const;

    /*!
     * \brief Creates the areas that have to be avoided by the tree's branches
     * in order to reach the build plate.
     *
     * The result is a 2D area that would cause nodes of radius \p radius to
     * collide with the model or be unable to reach the build platform.
     *
     * The input collision areas are inset by the maximum move distance and
     * propagated upwards.
     *
     * \param radius The radius of the node of interest
     * \param layer The layer of interest
     * \return Polygons object
     */
    const ExPolygons& get_avoidance(coordf_t radius, size_t layer_idx, int recursions=0) const;

    Polygons get_contours(size_t layer_nr) const;
    Polygons get_contours_with_holes(size_t layer_nr) const;

    std::vector<LayerHeightData> layer_heights;

    std::vector<TreeNode> tree_nodes;

private:
    /*!
     * \brief Convenience typedef for the keys to the caches
     */
    struct RadiusLayerPair {
        coordf_t radius;
        size_t layer_nr;
        int recursions;
        
    };
    struct RadiusLayerPairEquality {
        constexpr bool operator()(const RadiusLayerPair& _Left, const RadiusLayerPair& _Right) const {
            return _Left.radius == _Right.radius && _Left.layer_nr == _Right.layer_nr;
        }
    };
    struct RadiusLayerPairHash {
        size_t operator()(const RadiusLayerPair& elem) const {
            return std::hash<coord_t>()(elem.radius) ^ std::hash<coord_t>()(elem.layer_nr * 7919);
        }
    };

    /*!
     * \brief Round \p radius upwards to a multiple of m_radius_sample_resolution
     *
     * \param radius The radius of the node of interest
     */
    coordf_t ceil_radius(coordf_t radius) const;

    /*!
     * \brief Calculate the collision areas at the radius and layer indicated
     * by \p key.
     *
     * \param key The radius and layer of the node of interest
     */
    const ExPolygons& calculate_collision(const RadiusLayerPair& key) const;

    /*!
     * \brief Calculate the avoidance areas at the radius and layer indicated
     * by \p key.
     *
     * \param key The radius and layer of the node of interest
     */
    const ExPolygons& calculate_avoidance(const RadiusLayerPair& key) const;


public:
    bool is_slim = false;
    /*!
     * \brief The required clearance between the model and the tree branches
     */
    coordf_t m_xy_distance;

    /*!
     * \brief The maximum distance that the centrepoint of a tree branch may
     * move in consequtive layers
     */
    coordf_t m_max_move;

    /*!
     * \brief Sample resolution for radius values.
     *
     * The radius will be rounded (upwards) to multiples of this value before
     * calculations are done when collision, avoidance and internal model
     * Polygons are requested.
     */
    coordf_t m_radius_sample_resolution;

    /*!
     * \brief Storage for layer outlines of the meshes.
     */
    std::vector<ExPolygons> m_layer_outlines;

    // union contours of all layers below
    std::vector<ExPolygons> m_layer_outlines_below;

    /*!
     * \brief Caches for the collision, avoidance and internal model polygons
     * at given radius and layer indices.
     *
     * These are mutable to allow modification from const function. This is
     * generally considered OK as the functions are still logically const
     * (ie there is no difference in behaviour for the user betweeen
     * calculating the values each time vs caching the results).
     * 
     * coconut: previously stl::unordered_map is used which seems problematic with tbb::parallel_for.
     * So we change to tbb::concurrent_unordered_map
     */
    mutable tbb::concurrent_unordered_map<RadiusLayerPair, ExPolygons, RadiusLayerPairHash, RadiusLayerPairEquality> m_collision_cache;
    mutable tbb::concurrent_unordered_map<RadiusLayerPair, ExPolygons, RadiusLayerPairHash, RadiusLayerPairEquality> m_avoidance_cache;

    friend TreeSupport;
};

struct LineHash {
    size_t operator()(const Line& line) const {
        return (std::hash<coord_t>()(line.a(0)) ^ std::hash<coord_t>()(line.b(1))) * 102 +
            (std::hash<coord_t>()(line.a(1)) ^ std::hash<coord_t>()(line.b(0))) * 10222;
    }
};

/*!
 * \brief Generates a tree structure to support your models.
 */
class TreeSupport
{
public:
    /*!
     * \brief Creates an instance of the tree support generator.
     *
     * \param storage The data storage to get global settings from.
     */
    TreeSupport(PrintObject& object, const SlicingParameters &slicing_params);

    /*!
     * \brief Create the areas that need support.
     *
     * These areas are stored inside the given SliceDataStorage object.
     * \param storage The data storage where the mesh data is gotten from and
     * where the resulting support areas are stored.
     */
    void generate();

    void detect_overhangs(bool detect_first_sharp_tail_only=false);

    enum NodeType {
        eCircle,
        eSquare,
        ePolygon
    };

    /*!
     * \brief Represents the metadata of a node in the tree.
     */
    struct Node
    {
        static constexpr Node* NO_PARENT = nullptr;

        Node()
         : distance_to_top(0)
         , position(Point(0, 0))
         , obj_layer_nr(0)
         , support_roof_layers_below(0)
         , support_floor_layers_above(0)
         , to_buildplate(true)
         , parent(nullptr)
         , print_z(0.0)
         , height(0.0)
        {}

        // when dist_mm_to_top_==0, new node's dist_mm_to_top=parent->dist_mm_to_top + parent->height;
        Node(const Point position, const int distance_to_top, const int obj_layer_nr, const int support_roof_layers_below, const bool to_buildplate, Node* parent,
             coordf_t     print_z_, coordf_t height_, coordf_t dist_mm_to_top_=0)
         : distance_to_top(distance_to_top)
         , position(position)
         , obj_layer_nr(obj_layer_nr)
         , support_roof_layers_below(support_roof_layers_below)
         , support_floor_layers_above(0)
         , to_buildplate(to_buildplate)
         , parent(parent)
         , print_z(print_z_)
         , height(height_)
         , dist_mm_to_top(dist_mm_to_top_)
        {
            if (parent) {
                type     = parent->type;
                overhang = parent->overhang;
                if (dist_mm_to_top==0)
                    dist_mm_to_top = parent->dist_mm_to_top + parent->height;
                parent->child = this;
                for (auto& neighbor : parent->merged_neighbours)
                    neighbor->child = this;
            }
        }

#ifdef DEBUG // Clear the delete node's data so if there's invalid access after, we may get a clue by inspecting that node.
        ~Node()
        {
            parent = nullptr;
            merged_neighbours.clear();
        }
#endif // DEBUG

        /*!
         * \brief The number of layers to go to the top of this branch.
         * Negative value means it's a virtual node between support and overhang, which doesn't need to be extruded.
         */
        int distance_to_top;
        coordf_t dist_mm_to_top = 0;  // dist to bottom contact in mm

        /*!
         * \brief The position of this node on the layer.
         */
        Point            position;
        Point            movement; // movement towards neighbor center or outline
        mutable double   radius        = 0.0;
        mutable double   max_move_dist = 0.0;
        NodeType         type          = eCircle;
        bool             is_merged     = false; // this node is generated by merging upper nodes
        bool             is_corner     = false;
        bool             is_processed  = false;
        const ExPolygon *overhang      = nullptr; // when type==ePolygon, set this value to get original overhang area

        /*!
         * \brief The direction of the skin lines above the tip of the branch.
         *
         * This determines in which direction we should reduce the width of the
         * branch.
         */
        bool skin_direction;

        /*!
         * \brief The number of support roof layers below this one.
         *
         * When a contact point is created, it is determined whether the mesh
         * needs to be supported with support roof or not, since that is a
         * per-mesh setting. This is stored in this variable in order to track
         * how far we need to extend that support roof downwards.
         */
        int support_roof_layers_below;
        int support_floor_layers_above;
        int obj_layer_nr;

        /*!
         * \brief Whether to try to go towards the build plate.
         *
         * If the node is inside the collision areas, it has no choice but to go
         * towards the model. If it is not inside the collision areas, it must
         * go towards the build plate to prevent a scar on the surface.
         */
        bool to_buildplate;

        /*!
         * \brief The originating node for this one, one layer higher.
         *
         * In order to prune branches that can't have any support (because they
         * can't be on the model and the path to the buildplate isn't clear),
         * the entire branch needs to be known.
         */
        Node *parent;
        Node *child = nullptr;

        /*!
        * \brief All neighbours (on the same layer) that where merged into this node.
        *
        * In order to prune branches that can't have any support (because they
        * can't be on the model and the path to the buildplate isn't clear),
        * the entire branch needs to be known.
        */
        std::list<Node*> merged_neighbours;

        coordf_t print_z;
        coordf_t height;

        bool operator==(const Node& other) const
        {
            return position == other.position;
        }
    };

    struct SupportParams
    {
        Flow first_layer_flow;
        Flow support_material_flow;
        Flow support_material_interface_flow;
        Flow support_material_bottom_interface_flow;
        coordf_t support_extrusion_width;
        // Is merging of regions allowed? Could the interface & base support regions be printed with the same extruder?
        bool can_merge_support_regions;

        coordf_t support_layer_height_min;
        //	coordf_t	support_layer_height_max;

        coordf_t gap_xy;

        float    base_angle;
        float    interface_angle;
        coordf_t interface_spacing;
        coordf_t interface_density;
        coordf_t support_spacing;
        coordf_t support_density;

        InfillPattern base_fill_pattern;
        InfillPattern interface_fill_pattern;
        InfillPattern contact_fill_pattern;
        bool          with_sheath;
        const double thresh_big_overhang = SQ(scale_(10));
    };

    int  avg_node_per_layer = 0;
    float nodes_angle       = 0;
    bool  has_overhangs = false;
    bool  has_sharp_tails = false;
    bool  has_cantilever = false;
    double max_cantilever_dist = 0;
    SupportType support_type;
    SupportMaterialStyle support_style;

    std::unique_ptr<FillLightning::Generator> generator;
    std::unordered_map<double, size_t> printZ_to_lightninglayer;
private:
    /*!
     * \brief Generator for model collision, avoidance and internal guide volumes
     *
     * Lazily computes volumes as needed.
     *  \warning This class is NOT currently thread-safe and should not be accessed in OpenMP blocks
     */
    std::shared_ptr<TreeSupportData> m_ts_data;
    PrintObject    *m_object;
    const PrintObjectConfig *m_object_config;
    SlicingParameters        m_slicing_params;
    // Various precomputed support parameters to be shared with external functions.
    SupportParams   m_support_params;
    size_t          m_raft_layers = 0;
    size_t          m_highest_overhang_layer = 0;
    std::vector<std::vector<MinimumSpanningTree>> m_spanning_trees;
    std::vector< std::unordered_map<Line, bool, LineHash>> m_mst_line_x_layer_contour_caches;
    coordf_t MAX_BRANCH_RADIUS = 10.0;
    coordf_t MAX_BRANCH_RADIUS_FIRST_LAYER = 12.0;
    coordf_t MIN_BRANCH_RADIUS = 0.5;
    float tree_support_branch_diameter_angle = 5.0;
    bool  is_strong = false;
    bool  is_slim                            = false;
    bool  with_infill                        = false;


    /*!
     * \brief Polygons representing the limits of the printable area of the
     * machine
     */
    ExPolygon m_machine_border;

    /*!
     * \brief Draws circles around each node of the tree into the final support.
     *
     * This also handles the areas that have to become support roof, support
     * bottom, the Z distances, etc.
     *
     * \param storage[in, out] The settings storage to get settings from and to
     * save the resulting support polygons to.
     * \param contact_nodes The nodes to draw as support.
     */
    void draw_circles(const std::vector<std::vector<Node*>>& contact_nodes);

    /*!
     * \brief Drops down the nodes of the tree support towards the build plate.
     *
     * This is where the cleverness of tree support comes in: The nodes stay on
     * their 2D layers but on the next layer they are slightly shifted. This
     * causes them to move towards each other as they are copied to lower layers
     * which ultimately results in a 3D tree.
     *
     * \param contact_nodes[in, out] The nodes in the space that need to be
     * dropped down. The nodes are dropped to lower layers inside the same
     * vector of layers.
     */
    void drop_nodes(std::vector<std::vector<Node *>> &contact_nodes);

    void smooth_nodes(std::vector<std::vector<Node *>> &contact_nodes);

    void adjust_layer_heights(std::vector<std::vector<Node*>>& contact_nodes);

    /*! BBS: MusangKing: maximum layer height
     * \brief Optimize the generation of tree support by pre-planning the layer_heights
     * 
    */

    std::vector<LayerHeightData> plan_layer_heights(std::vector<std::vector<Node *>> &contact_nodes);
    /*!
     * \brief Creates points where support contacts the model.
     *
     * A set of points is created for each layer.
     * \param mesh The mesh to get the overhang areas to support of.
     * \param contact_nodes[out] A vector of mappings from contact points to
     * their tree nodes.
     * \param collision_areas For every layer, the areas where a generated
     * contact point would immediately collide with the model due to the X/Y
     * distance.
     * \return For each layer, a list of points where the tree should connect
     * with the model.
     */
    void generate_contact_points(std::vector<std::vector<Node*>>& contact_nodes);

    /*!
     * \brief Add a node to the next layer.
     *
     * If a node is already at that position in the layer, the nodes are merged.
     */
    void insert_dropped_node(std::vector<Node*>& nodes_layer, Node* node);
    void create_tree_support_layers();
    void generate_toolpaths();
    Polygons spanning_tree_to_polygon(const std::vector<MinimumSpanningTree>& spanning_trees, Polygons layer_contours, int layer_nr);
    Polygons contact_nodes_to_polygon(const std::vector<Node*>& contact_nodes, Polygons layer_contours, int layer_nr, std::vector<double>& radiis, std::vector<bool>& is_interface);
    coordf_t calc_branch_radius(coordf_t base_radius, size_t layers_to_top, size_t tip_layers, double diameter_angle_scale_factor);
    coordf_t calc_branch_radius(coordf_t base_radius, coordf_t mm_to_top, double diameter_angle_scale_factor);

    // similar to SupportMaterial::trim_support_layers_by_object
    Polygons get_trim_support_regions(
        const PrintObject& object,
        SupportLayer* support_layer_ptr,
        const coordf_t       gap_extra_above,
        const coordf_t       gap_extra_below,
        const coordf_t       gap_xy);
};

}

#endif /* TREESUPPORT_H */
