#include "SLAImport.hpp"

#include <sstream>

#include "libslic3r/SlicesToTriangleMesh.hpp"
#include "libslic3r/MarchingSquares.hpp"
#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/MTUtils.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/SLA/RasterBase.hpp"
#include "libslic3r/miniz_extension.hpp"

#include <boost/property_tree/ini_parser.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/algorithm/string.hpp>

#include <wx/image.h>
#include <wx/mstream.h>

namespace marchsq {

// Specialize this struct to register a raster type for the Marching squares alg
template<> struct _RasterTraits<wxImage> {
    using Rst = wxImage;
    
    // The type of pixel cell in the raster
    using ValueType = uint8_t;
    
    // Value at a given position
    static uint8_t get(const Rst &rst, size_t row, size_t col)
    {
        return rst.GetRed(col, row);
    }

    // Number of rows and cols of the raster
    static size_t rows(const Rst &rst) { return rst.GetHeight(); }
    static size_t cols(const Rst &rst) { return rst.GetWidth(); }
};

} // namespace marchsq

namespace Slic3r {

namespace {

struct ArchiveData {
    boost::property_tree::ptree profile, config;
    std::vector<sla::EncodedRaster> images;
};

static const constexpr char *CONFIG_FNAME  = "config.ini";
static const constexpr char *PROFILE_FNAME = "prusaslicer.ini";

boost::property_tree::ptree read_ini(const mz_zip_archive_file_stat &entry,
                                     MZ_Archive &                    zip)
{
    std::string buf(size_t(entry.m_uncomp_size), '\0');
    
    if (!mz_zip_reader_extract_file_to_mem(&zip.arch, entry.m_filename,
                                           buf.data(), buf.size(), 0))
        throw std::runtime_error(zip.get_errorstr());
    
    boost::property_tree::ptree tree;
    std::stringstream ss(buf);
    boost::property_tree::read_ini(ss, tree);
    return tree;
}

sla::EncodedRaster read_png(const mz_zip_archive_file_stat &entry,
                            MZ_Archive &                    zip,
                            const std::string &             name)
{
    std::vector<uint8_t> buf(entry.m_uncomp_size);

    if (!mz_zip_reader_extract_file_to_mem(&zip.arch, entry.m_filename,
                                           buf.data(), buf.size(), 0))
        throw std::runtime_error(zip.get_errorstr());

    return sla::EncodedRaster(std::move(buf),
                              name.empty() ? entry.m_filename : name);
}

ArchiveData extract_sla_archive(const std::string &zipfname,
                                 const std::string &exclude)
{
    ArchiveData arch;
    
    // Little RAII
    struct Arch: public MZ_Archive {
        Arch(const std::string &fname) {
            if (!open_zip_reader(&arch, fname))
                throw std::runtime_error(get_errorstr());
        }
        
        ~Arch() { close_zip_reader(&arch); }
    } zip (zipfname);

    mz_uint num_entries = mz_zip_reader_get_num_files(&zip.arch);
    
    for (mz_uint i = 0; i < num_entries; ++i)
    {
        mz_zip_archive_file_stat entry;
        
        if (mz_zip_reader_file_stat(&zip.arch, i, &entry))
        {
            std::string name = entry.m_filename;
            boost::algorithm::to_lower(name);
            
            if (boost::algorithm::contains(name, exclude)) continue;
            
            if (name == CONFIG_FNAME) arch.config = read_ini(entry, zip);
            if (name == PROFILE_FNAME) arch.profile = read_ini(entry, zip);
            
            if (boost::filesystem::path(name).extension().string() == ".png") {
                auto it = std::lower_bound(
                    arch.images.begin(), arch.images.end(), sla::EncodedRaster({}, name),
                    [](const sla::EncodedRaster &r1, const sla::EncodedRaster &r2) {
                        return std::less<std::string>()(r1.extension(), r2.extension());
                    });
                
                arch.images.insert(it, read_png(entry, zip, name));
            }
        }
    }
    
    return arch;
}

ExPolygons rings_to_expolygons(const std::vector<marchsq::Ring> &rings,
                               double px_w, double px_h)
{
    ExPolygons polys; polys.reserve(rings.size());
    
    for (const marchsq::Ring &ring : rings) {
        Polygon poly; Points &pts = poly.points;
        pts.reserve(ring.size());
        
        for (const marchsq::Coord &crd : ring)
            pts.emplace_back(scaled(crd.c * px_w), scaled(crd.r * px_h));
        
        polys.emplace_back(poly);
    }
    
    // reverse the raster transformations
    return union_ex(polys);
}

template<class Fn> void foreach_vertex(ExPolygon &poly, Fn &&fn)
{
    for (auto &p : poly.contour.points) fn(p);
    for (auto &h : poly.holes)
        for (auto &p : h.points) fn(p);
}

void invert_raster_trafo(ExPolygons &                  expolys,
                         const sla::RasterBase::Trafo &trafo,
                         coord_t                       width,
                         coord_t                       height)
{
    for (auto &expoly : expolys) {
        if (trafo.mirror_y)
            foreach_vertex(expoly, [height](Point &p) {p.y() = height - p.y(); });
        
        if (trafo.mirror_x)
            foreach_vertex(expoly, [width](Point &p) {p.x() = width - p.x(); });
        
        expoly.translate(-trafo.center_x, -trafo.center_y);
        
        if (trafo.flipXY)
            foreach_vertex(expoly, [](Point &p) { std::swap(p.x(), p.y()); });
        
        if ((trafo.mirror_x + trafo.mirror_y + trafo.flipXY) % 2) {
            expoly.contour.reverse();
            for (auto &h : expoly.holes) h.reverse();
        }
    }
}

struct RasterParams {
    sla::RasterBase::Trafo trafo; // Raster transformations
    coord_t        width, height; // scaled raster dimensions (not resolution)
    double         px_h, px_w;    // pixel dimesions
    marchsq::Coord win;           // marching squares window size
};

RasterParams get_raster_params(const DynamicPrintConfig &cfg)
{
    auto *opt_disp_cols = cfg.option<ConfigOptionInt>("display_pixels_x");
    auto *opt_disp_rows = cfg.option<ConfigOptionInt>("display_pixels_y");
    auto *opt_disp_w    = cfg.option<ConfigOptionFloat>("display_width");
    auto *opt_disp_h    = cfg.option<ConfigOptionFloat>("display_height");
    auto *opt_mirror_x  = cfg.option<ConfigOptionBool>("display_mirror_x");
    auto *opt_mirror_y  = cfg.option<ConfigOptionBool>("display_mirror_y");
    auto *opt_orient    = cfg.option<ConfigOptionEnum<SLADisplayOrientation>>("display_orientation");
    
    if (!opt_disp_cols || !opt_disp_rows || !opt_disp_w || !opt_disp_h ||
        !opt_mirror_x || !opt_mirror_y || !opt_orient)
        throw std::runtime_error("Invalid SL1 file");
    
    RasterParams rstp;
    
    rstp.px_w = opt_disp_w->value / (opt_disp_cols->value - 1);
    rstp.px_h = opt_disp_h->value / (opt_disp_rows->value - 1);
    
    sla::RasterBase::Trafo trafo{opt_orient->value == sladoLandscape ?
                                     sla::RasterBase::roLandscape :
                                     sla::RasterBase::roPortrait,
                                 {opt_mirror_x->value, opt_mirror_y->value}};
    
    rstp.height = scaled(opt_disp_h->value);
    rstp.width  = scaled(opt_disp_w->value);
    
    return rstp;
}

struct SliceParams { double layerh = 0., initial_layerh = 0.; };

SliceParams get_slice_params(const DynamicPrintConfig &cfg)
{
    auto *opt_layerh = cfg.option<ConfigOptionFloat>("layer_height");
    auto *opt_init_layerh = cfg.option<ConfigOptionFloat>("initial_layer_height");
    
    if (!opt_layerh || !opt_init_layerh)
        throw std::runtime_error("Invalid SL1 file");
    
    return SliceParams{opt_layerh->getFloat(), opt_init_layerh->getFloat()};
}

std::vector<ExPolygons> extract_slices_from_sla_archive(
    ArchiveData &            arch,
    const RasterParams &     rstp,
    std::function<bool(int)> progr)
{
    auto jobdir = arch.config.get<std::string>("jobDir");
    for (auto &c : jobdir) c = std::tolower(c);
    
    std::vector<ExPolygons> slices(arch.images.size());

    struct Status
    {
        double          incr, val, prev;
        bool            stop = false;
        tbb::spin_mutex mutex;
    } st {100. / slices.size(), 0., 0.};
    
    tbb::parallel_for(size_t(0), arch.images.size(),
                     [&arch, &slices, &st, &rstp, progr](size_t i) {
        // Status indication guarded with the spinlock
        {
            std::lock_guard<tbb::spin_mutex> lck(st.mutex);
            if (st.stop) return;
    
            st.val += st.incr;
            double curr = std::round(st.val);
            if (curr > st.prev) {
                st.prev = curr;
                st.stop = !progr(int(curr));
            }
        }
        
        auto &buf = arch.images[i];
        wxMemoryInputStream   stream{buf.data(), buf.size()};
        wxImage               img{stream};
    
        auto rings = marchsq::execute(img, 128, rstp.win);
        ExPolygons expolys = rings_to_expolygons(rings, rstp.px_w, rstp.px_h);

        // Invert the raster transformations indicated in
        // the profile metadata
        invert_raster_trafo(expolys, rstp.trafo, rstp.width, rstp.height);
    
        slices[i] = std::move(expolys);
    });
    
    if (st.stop) slices = {};

    return slices;
}

} // namespace

void import_sla_archive(const std::string &zipfname, DynamicPrintConfig &out)
{
    ArchiveData arch = extract_sla_archive(zipfname, "png");
    out.load(arch.profile);
}

void import_sla_archive(
    const std::string &      zipfname,
    Vec2i                    windowsize,
    TriangleMesh &           out,
    DynamicPrintConfig &     profile,
    std::function<bool(int)> progr)
{
    // Ensure minimum window size for marching squares
    windowsize.x() = std::max(2, windowsize.x());
    windowsize.y() = std::max(2, windowsize.y());

    ArchiveData arch = extract_sla_archive(zipfname, "thumbnail");
    profile.load(arch.profile);

    RasterParams rstp = get_raster_params(profile);
    rstp.win          = {windowsize.y(), windowsize.x()};
    
    SliceParams slicp = get_slice_params(profile);
    
    std::vector<ExPolygons> slices =
        extract_slices_from_sla_archive(arch, rstp, progr);
   
    if (!slices.empty())
        out = slices_to_triangle_mesh(slices, 0, slicp.layerh, slicp.initial_layerh);
}

} // namespace Slic3r
