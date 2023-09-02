///|/ Copyright (c) Prusa Research 2023 Vojtěch Bubník @bubnikv
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#if 1 //#ifdef SLIC3R_DEBUG

#include "../ClipperUtils.hpp"
#include "../SVG.hpp"
#include "../Layer.hpp"

#include "SupportLayer.hpp"

namespace Slic3r::FFFSupport {

const char* support_surface_type_to_color_name(const SupporLayerType surface_type)
{
    switch (surface_type) {
        case SupporLayerType::TopContact:     return "rgb(255,0,0)"; // "red";
        case SupporLayerType::TopInterface:   return "rgb(0,255,0)"; // "green";
        case SupporLayerType::Base:           return "rgb(0,0,255)"; // "blue";
        case SupporLayerType::BottomInterface:return "rgb(255,255,128)"; // yellow 
        case SupporLayerType::BottomContact:  return "rgb(255,0,255)"; // magenta
        case SupporLayerType::RaftInterface:  return "rgb(0,255,255)";
        case SupporLayerType::RaftBase:       return "rgb(128,128,128)";
        case SupporLayerType::Unknown:        return "rgb(128,0,0)"; // maroon
        default:                              return "rgb(64,64,64)";
    };
}

Point export_support_surface_type_legend_to_svg_box_size()
{
    return Point(scale_(1.+10.*8.), scale_(3.)); 
}

void export_support_surface_type_legend_to_svg(SVG &svg, const Point &pos)
{
    // 1st row
    coord_t pos_x0 = pos(0) + scale_(1.);
    coord_t pos_x = pos_x0;
    coord_t pos_y = pos(1) + scale_(1.5);
    coord_t step_x = scale_(10.);
    svg.draw_legend(Point(pos_x, pos_y), "top contact"    , support_surface_type_to_color_name(SupporLayerType::TopContact));
    pos_x += step_x;
    svg.draw_legend(Point(pos_x, pos_y), "top iface"      , support_surface_type_to_color_name(SupporLayerType::TopInterface));
    pos_x += step_x;
    svg.draw_legend(Point(pos_x, pos_y), "base"           , support_surface_type_to_color_name(SupporLayerType::Base));
    pos_x += step_x;
    svg.draw_legend(Point(pos_x, pos_y), "bottom iface"   , support_surface_type_to_color_name(SupporLayerType::BottomInterface));
    pos_x += step_x;
    svg.draw_legend(Point(pos_x, pos_y), "bottom contact" , support_surface_type_to_color_name(SupporLayerType::BottomContact));
    // 2nd row
    pos_x = pos_x0;
    pos_y = pos(1)+scale_(2.8);
    svg.draw_legend(Point(pos_x, pos_y), "raft interface" , support_surface_type_to_color_name(SupporLayerType::RaftInterface));
    pos_x += step_x;
    svg.draw_legend(Point(pos_x, pos_y), "raft base"      , support_surface_type_to_color_name(SupporLayerType::RaftBase));
    pos_x += step_x;
    svg.draw_legend(Point(pos_x, pos_y), "unknown"        , support_surface_type_to_color_name(SupporLayerType::Unknown));
    pos_x += step_x;
    svg.draw_legend(Point(pos_x, pos_y), "intermediate"   , support_surface_type_to_color_name(SupporLayerType::Intermediate));
}

void export_print_z_polygons_to_svg(const char *path, SupportGeneratorLayer ** const layers, int n_layers)
{
    BoundingBox bbox;
    for (int i = 0; i < n_layers; ++ i)
        bbox.merge(get_extents(layers[i]->polygons));
    Point legend_size = export_support_surface_type_legend_to_svg_box_size();
    Point legend_pos(bbox.min(0), bbox.max(1));
    bbox.merge(Point(std::max(bbox.min(0) + legend_size(0), bbox.max(0)), bbox.max(1) + legend_size(1)));
    SVG svg(path, bbox);
    const float transparency = 0.5f;
    for (int i = 0; i < n_layers; ++ i)
        svg.draw(union_ex(layers[i]->polygons), support_surface_type_to_color_name(layers[i]->layer_type), transparency);
    for (int i = 0; i < n_layers; ++ i)
        svg.draw(to_polylines(layers[i]->polygons), support_surface_type_to_color_name(layers[i]->layer_type));
    export_support_surface_type_legend_to_svg(svg, legend_pos);
    svg.Close();
}

void export_print_z_polygons_and_extrusions_to_svg(
    const char                        *path, 
    SupportGeneratorLayer ** const     layers, 
    int                                n_layers,
    SupportLayer                      &support_layer)
{
    BoundingBox bbox;
    for (int i = 0; i < n_layers; ++ i)
        bbox.merge(get_extents(layers[i]->polygons));
    Point legend_size = export_support_surface_type_legend_to_svg_box_size();
    Point legend_pos(bbox.min(0), bbox.max(1));
    bbox.merge(Point(std::max(bbox.min(0) + legend_size(0), bbox.max(0)), bbox.max(1) + legend_size(1)));
    SVG svg(path, bbox);
    const float transparency = 0.5f;
    for (int i = 0; i < n_layers; ++ i)
        svg.draw(union_ex(layers[i]->polygons), support_surface_type_to_color_name(layers[i]->layer_type), transparency);
    for (int i = 0; i < n_layers; ++ i)
        svg.draw(to_polylines(layers[i]->polygons), support_surface_type_to_color_name(layers[i]->layer_type));

    Polygons polygons_support, polygons_interface;
    support_layer.support_fills.polygons_covered_by_width(polygons_support, float(SCALED_EPSILON));
//    support_layer.support_interface_fills.polygons_covered_by_width(polygons_interface, SCALED_EPSILON);
    svg.draw(union_ex(polygons_support), "brown");
    svg.draw(union_ex(polygons_interface), "black");

    export_support_surface_type_legend_to_svg(svg, legend_pos);
    svg.Close();
}

} // namespace Slic3r

#endif /* SLIC3R_DEBUG */
