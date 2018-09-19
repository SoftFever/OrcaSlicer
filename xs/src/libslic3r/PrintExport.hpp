#ifndef PRINTEXPORT_HPP
#define PRINTEXPORT_HPP

#include "Print.hpp"

// For png export of the sliced model
#include <fstream>
#include <sstream>

#include <boost/log/trivial.hpp>

#include "Rasterizer/Rasterizer.hpp"
#include <tbb/parallel_for.h>
#include <tbb/spin_mutex.h>//#include "tbb/mutex.h"

namespace Slic3r {

enum class FilePrinterFormat {
    PNG,
    SVG
};

/*
 * Interface for a file printer of the slices. Implementation can be an SVG
 * or PNG printer or any other format.
 *
 * The format argument specifies the output format of the printer and it enables
 * different implementations of this class template for each supported format.
 *
 */
template<FilePrinterFormat format, class LayerFormat = void>
class FilePrinter {
public:

    void print_config(const Print&);

    // Draw an ExPolygon which is a polygon inside a slice on the specified layer.
    void draw_polygon(const ExPolygon& p, unsigned lyr);

    // Tell the printer how many layers should it consider.
    void layers(unsigned layernum);

    // Get the number of layers in the print.
    unsigned layers() const;

    /* Switch to a particular layer. If there where less layers then the
     * specified layer number than an appropriate number of layers will be
     * allocated in the printer.
     */
    void begin_layer(unsigned layer);

    // Allocate a new layer on top of the last and switch to it.
    void begin_layer();

    /*
     * Finish the selected layer. It means that no drawing is allowed on that
     * layer anymore. This fact can be used to prepare the file system output
     * data like png comprimation and so on.
     */
    void finish_layer(unsigned layer);

    // Finish the top layer.
    void finish_layer();

    // Save all the layers into the file (or dir) specified in the path argument
    void save(const std::string& path);

    // Save only the selected layer to the file specified in path argument.
    void save_layer(unsigned lyr, const std::string& path);
};

template<class T = void> struct VeryFalse { static const bool value = false; };

// This has to be explicitly implemented in the gui layer or a default zlib
// based implementation is needed.
template<class Backend> class LayerWriter {
public:

    LayerWriter(const std::string& /*zipfile_path*/) {
        static_assert(VeryFalse<Backend>::value,
                      "No layer writer implementation provided!");
    }

    void next_entry(const std::string& /*fname*/) {}

    std::string get_name() { return ""; }

    bool is_ok() { return false; }

    template<class T> LayerWriter& operator<<(const T& /*arg*/) {
        return *this;
    }

    void close() {}
};

// Implementation for PNG raster output
// Be aware that if a large number of layers are allocated, it can very well
// exhaust the available memory especially on 32 bit platform.
template<class LyrFormat> class FilePrinter<FilePrinterFormat::PNG, LyrFormat> {

    struct Layer {
        Raster first;
        std::stringstream second;

        Layer() {}

        Layer(const Layer&) = delete;
        Layer(Layer&& m):
            first(std::move(m.first))/*, second(std::move(m.second))*/ {}
    };

    // We will save the compressed PNG data into stringstreams which can be done
    // in parallel. Later we can write every layer to the disk sequentially.
    std::vector<Layer> m_layers_rst;
    Raster::Resolution m_res;
    Raster::PixelDim m_pxdim;
    const Print *m_print = nullptr;
    double m_exp_time_s = .0, m_exp_time_first_s = .0;

    std::string createIniContent(const std::string& projectname) {
        double layer_height = m_print?
                    m_print->default_object_config().layer_height.getFloat() :
                    0.05;

        using std::string;
        using std::to_string;

        auto expt_str = to_string(m_exp_time_s);
        auto expt_first_str = to_string(m_exp_time_first_s);
        auto stepnum_str = to_string(static_cast<unsigned>(800*layer_height));
        auto layerh_str = to_string(layer_height);

        return string(
        "action = print\n"
        "jobDir = ") + projectname + "\n" +
        "expTime = " + expt_str + "\n"
        "expTimeFirst = " + expt_first_str + "\n"
        "stepNum = " + stepnum_str + "\n"
        "wifiOn = 1\n"
        "tiltSlow = 60\n"
        "tiltFast = 15\n"
        "numFade = 10\n"
        "startdelay = 0\n"
        "layerHeight = " + layerh_str + "\n"
        "noteInfo = "
        "expTime="+expt_str+"+resinType=generic+layerHeight="
                  +layerh_str+"+printer=DWARF3\n";
    }

    // Change this to TOP_LEFT if you want correct PNG orientation
    static const Raster::Origin ORIGIN = Raster::Origin::BOTTOM_LEFT;

public:
    inline FilePrinter(double width_mm, double height_mm,
                       unsigned width_px, unsigned height_px,
                       double exp_time, double exp_time_first):
        m_res(width_px, height_px),
        m_pxdim(width_mm/width_px, height_mm/height_px),
        m_exp_time_s(exp_time),
        m_exp_time_first_s(exp_time_first)
    {
    }

    FilePrinter(const FilePrinter& ) = delete;
    FilePrinter(FilePrinter&& m):
        m_layers_rst(std::move(m.m_layers_rst)),
        m_res(m.m_res),
        m_pxdim(m.m_pxdim) {}

    inline void layers(unsigned cnt) { if(cnt > 0) m_layers_rst.resize(cnt); }
    inline unsigned layers() const { return unsigned(m_layers_rst.size()); }

    void print_config(const Print& printconf) { m_print = &printconf; }

    inline void draw_polygon(const ExPolygon& p, unsigned lyr) {
        assert(lyr < m_layers_rst.size());
        m_layers_rst[lyr].first.draw(p);
    }

    inline void begin_layer(unsigned lyr) {
        if(m_layers_rst.size() <= lyr) m_layers_rst.resize(lyr+1);
        m_layers_rst[lyr].first.reset(m_res, m_pxdim, ORIGIN);
    }

    inline void begin_layer() {
        m_layers_rst.emplace_back();
        m_layers_rst.front().first.reset(m_res, m_pxdim, ORIGIN);
    }

    inline void finish_layer(unsigned lyr_id) {
        assert(lyr_id < m_layers_rst.size());
        m_layers_rst[lyr_id].first.save(m_layers_rst[lyr_id].second,
                                       Raster::Compression::PNG);
        m_layers_rst[lyr_id].first.reset();
    }

    inline void finish_layer() {
        if(!m_layers_rst.empty()) {
            m_layers_rst.back().first.save(m_layers_rst.back().second,
                                          Raster::Compression::PNG);
            m_layers_rst.back().first.reset();
        }
    }

    inline void save(const std::string& path) {
        try {
            LayerWriter<LyrFormat> writer(path);

            std::string project = writer.get_name();

            writer.next_entry("config.ini");
            writer << createIniContent(project);

            for(unsigned i = 0; i < m_layers_rst.size(); i++) {
                if(m_layers_rst[i].second.rdbuf()->in_avail() > 0) {
                    char lyrnum[6];
                    std::sprintf(lyrnum, "%.5d", i);
                    auto zfilename = project + lyrnum + ".png";
                    writer.next_entry(zfilename);
                    writer << m_layers_rst[i].second.rdbuf();
                    m_layers_rst[i].second.str("");
                }
            }

            writer.close();
        } catch(std::exception& e) {
            BOOST_LOG_TRIVIAL(error) << e.what();
            return;
        }
    }

    void save_layer(unsigned lyr, const std::string& path) {
        unsigned i = lyr;
        assert(i < m_layers_rst.size());

        char lyrnum[6];
        std::sprintf(lyrnum, "%.5d", lyr);
        std::string loc = path + "layer" + lyrnum + ".png";

        std::fstream out(loc, std::fstream::out | std::fstream::binary);
        if(out.good()) {
            m_layers_rst[i].first.save(out, Raster::Compression::PNG);
        } else {
            BOOST_LOG_TRIVIAL(error) << "Can't create file for layer";
        }

        out.close();
        m_layers_rst[i].first.reset();
    }
};

// Let's shadow this eigen interface
inline coord_t  px(const Point& p) { return p(0); }
inline coord_t  py(const Point& p) { return p(1); }
inline coordf_t px(const Vec2d& p) { return p(0); }
inline coordf_t py(const Vec2d& p) { return p(1); }

template<FilePrinterFormat format, class LayerFormat, class...Args>
void print_to(Print& print,
              std::string dirpath,
              double width_mm,
              double height_mm,
              Args&&...args)
{

    std::string& dir = dirpath;

    // This map will hold the layers sorted by z coordinate. Layers on the
    // same height (from different objects) will be mapped to the same key and
    // rasterized to the same image.
    std::map<long long, LayerPtrs> layers;

    auto& objects = print.objects();

    // Merge the sliced layers with the support layers
    std::for_each(objects.cbegin(), objects.cend(),
                  [&layers](const PrintObject *o)
    {
        for(const auto l : o->layers()) {
            auto& lyrs = layers[static_cast<long long>(scale_(l->print_z))];
            lyrs.push_back(l);
        }

        for(const auto l : o->support_layers()) {
            auto& lyrs = layers[static_cast<long long>(scale_(l->print_z))];
            lyrs.push_back(l);
        }
    });

    auto print_bb = print.bounding_box();
    Vec2d punsc = unscale(print_bb.size());

    // If the print does not fit into the print area we should cry about it.
    if(px(punsc) > width_mm || py(punsc) > height_mm) {
        BOOST_LOG_TRIVIAL(warning) << "Warning: Print will not fit!" << "\n"
            << "Width needed: " << px(punsc) << "\n"
            << "Height needed: " << py(punsc) << "\n";
    }

    // Offset for centering the print onto the print area
    auto cx = scale_(width_mm)/2 - (px(print_bb.center()) - px(print_bb.min));
    auto cy = scale_(height_mm)/2 - (py(print_bb.center()) - py(print_bb.min));

    // Create the actual printer, forward any additional arguments to it.
    FilePrinter<format, LayerFormat> printer(width_mm, height_mm,
                                             std::forward<Args>(args)...);

    printer.print_config(print);

    printer.layers(layers.size());  // Allocate space for all the layers

    int st_prev = 0;
    const std::string jobdesc = "Rasterizing and compressing sliced layers";
    tbb::spin_mutex m;

    std::vector<long long> keys;
    keys.reserve(layers.size());
    for(auto& e : layers) keys.push_back(e.first);

    print.set_status(0, jobdesc);

    // Method that prints one layer
    auto process_layer = [&layers, &keys, &printer, &st_prev, &m,
            &jobdesc, print_bb, dir, cx, cy, &print]
            (unsigned layer_id)
    {
        LayerPtrs lrange = layers[keys[layer_id]];

        printer.begin_layer(layer_id);   // Switch to the appropriate layer

        for(Layer *lp : lrange) {
            Layer& l = *lp;

            ExPolygonCollection slices = l.slices;  // Copy the layer slices

            // Sort the polygons in the layer
            std::stable_sort(slices.expolygons.begin(), slices.expolygons.end(),
                             [](const ExPolygon& a, const ExPolygon& b) {
                return a.contour.contains(b.contour.first_point()) ? false :
                                                                     true;
            });

            // Draw all the polygons in the slice to the actual layer.
            for (const Point &d : l.object()->copies())
                for (ExPolygon slice : slices.expolygons) {
                    slice.translate(px(d), py(d));
                    slice.translate(-px(print_bb.min) + cx,
                                    -py(print_bb.min) + cy);

                    printer.draw_polygon(slice, layer_id);
                }

            /*if(print.has_support_material() && layer_id > 0) {
                BOOST_LOG_TRIVIAL(warning) << "support material for layer "
                                           << layer_id
                                           << " defined but export is "
                                              "not yet implemented.";

            }*/

        }

        printer.finish_layer(layer_id);  // Finish the layer for later saving it.

        auto st = static_cast<int>(layer_id*80.0/layers.size());
        m.lock();
        if( st - st_prev > 10) {
            print.set_status(st, jobdesc);
            st_prev = st;
        }
        m.unlock();

        // printer.saveLayer(layer_id, dir); We could save the layer immediately
    };

    // Print all the layers in parallel
    tbb::parallel_for<size_t, decltype(process_layer)>(0,
                                                       layers.size(),
                                                       process_layer);

    // Sequential version (for testing)
    // for(unsigned l = 0; l < layers.size(); ++l) process_layer(l);

//    print.set_status(100, jobdesc);

    // Save the print into the file system.
    print.set_status(90, "Writing layers to disk");
    printer.save(dir);
    print.set_status(100, "Writing layers completed");
}

}

#endif // PRINTEXPORT_HPP
