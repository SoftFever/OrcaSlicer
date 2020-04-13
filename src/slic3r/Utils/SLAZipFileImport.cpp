#include "SLAZipFileImport.hpp"

#include "libslic3r/SlicesToTriangleMesh.hpp"
#include "libslic3r/MarchingSquares.hpp"
#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/MTUtils.hpp"
#include "libslic3r/PrintConfig.hpp"

#include <wx/wfstream.h>
#include <wx/zipstrm.h>
#include <wx/mstream.h>
#include <wx/sstream.h>
#include <wx/image.h>

#include <tbb/parallel_for.h>

#include <boost/property_tree/ini_parser.hpp>

namespace marchsq {

// Specialize this struct to register a raster type for the Marching squares alg
template<> struct _RasterTraits<wxImage> {
    using Rst = wxImage;
    
    // The type of pixel cell in the raster
    using ValueType = uint8_t;
    
    // Value at a given position
    static uint8_t get(const Rst &rst, size_t row, size_t col) { return rst.GetRed(col, row); }
    
    // Number of rows and cols of the raster
    static size_t rows(const Rst &rst) { return rst.GetHeight(); }
    static size_t cols(const Rst &rst) { return rst.GetWidth(); }
};

} // namespace marchsq

namespace Slic3r {

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

TriangleMesh import_model_from_sla_zip(const wxString &zipfname)
{
    wxFileInputStream in(zipfname);
    wxZipInputStream zip(in, wxConvUTF8);
    
    std::map<std::string, wxMemoryOutputStream> files;
    
    while (auto entry = std::unique_ptr<wxZipEntry>(zip.GetNextEntry())) {
        auto fname = wxFileName(entry->GetName());
        wxString name_lo = fname.GetFullName().Lower();
        
        if (fname.IsDir() || name_lo.Contains("thumbnail")) continue;
        
        if (!zip.OpenEntry(*entry))
            throw std::runtime_error("Cannot read archive");
        
        wxMemoryOutputStream &stream = files[name_lo.ToStdString()];
        zip.Read(stream);
        std::cout << name_lo << " read bytes: " << zip.LastRead() << std::endl;
        if (!zip.LastRead()) std::cout << zip.GetLastError() << std::endl;
    }
    
    using boost::property_tree::ptree;
    
    auto load_ini = [&files](const std::string &key, ptree &tree) {
        auto it = files.find(key);
        if (it != files.end()) {
            wxString str;
            wxStringOutputStream oss{&str};
            wxMemoryInputStream inp{it->second};
            oss.Write(inp);
            std::stringstream iss(str.ToStdString());
            boost::property_tree::read_ini(iss, tree);
            files.erase(it);
        } else {
            throw std::runtime_error(key + " is missing");
        }   
    };
    
    ptree profile_tree, config;
    load_ini("prusaslicer.ini", profile_tree);
    load_ini("config.ini", config);    
    
    DynamicPrintConfig profile;
    profile.load(profile_tree);
        
    size_t disp_cols = profile.opt_int("display_pixels_x");
    size_t disp_rows = profile.opt_int("display_pixels_y");
    double disp_w    = profile.opt_float("display_width");
    double disp_h    = profile.opt_float("display_height");
    double px_w      = disp_w / disp_cols;
    double px_h      = disp_h / disp_rows;
    
    auto jobdir = config.get<std::string>("jobDir");
    for (auto &c : jobdir) c = std::tolower(c);
    
    for (auto it = files.begin(); it != files.end();)
        if (it->first.find(jobdir) == std::string::npos ||
            wxFileName(it->first).GetExt().Lower() != "png")
            it = files.erase(it);
        else ++it;

    std::vector<ExPolygons> slices(files.size());
    size_t i = 0;
    for (auto &item : files) {
        wxMemoryOutputStream &imagedata = item.second;
        wxMemoryInputStream stream{imagedata};
        wxImage img{stream, "image/png"};
        
        std::cout << img.GetWidth() << " " << img.GetHeight() << std::endl;
        
        auto rings = marchsq::execute(img, 128);
        slices[i++] = rings_to_expolygons(rings, px_w, px_h);
    }
    
    TriangleMesh out;
    if (!slices.empty()) {
        double lh  = profile.opt_float("layer_height");
        double ilh = profile.opt_float("initial_layer_height");
        out = slices_to_triangle_mesh(slices, 0, lh, ilh);
    }
    
    return out;
}

} // namespace Slic3r
