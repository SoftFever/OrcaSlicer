#ifndef libslic3r_FuzzySkin_hpp_
#define libslic3r_FuzzySkin_hpp_

namespace Slic3r::Feature::FuzzySkin {

void fuzzy_polyline(Points& poly, bool closed, coordf_t slice_z, const FuzzySkinConfig& cfg);

void fuzzy_extrusion_line(Arachne::ExtrusionJunctions& ext_lines, coordf_t slice_z, const FuzzySkinConfig& cfg);

bool should_fuzzify(const FuzzySkinConfig& config, int layer_id, size_t loop_idx, bool is_contour);

} // namespace Slic3r::Feature::FuzzySkin

#endif // libslic3r_FuzzySkin_hpp_
