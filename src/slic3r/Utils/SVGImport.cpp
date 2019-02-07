#include "SVGImport.hpp"

#include <agg/agg_basics.h>
#include <agg/agg_rendering_buffer.h>
#include <agg/agg_rasterizer_scanline_aa.h>
#include <agg/agg_scanline_p.h>
#include <agg/agg_renderer_scanline.h>
#include <agg/agg_pixfmt_rgba.h>
#include <agg_svg/agg_svg_parser.h>

#include <png/writer.hpp>

#include <boost/nowide/iostream.hpp>

namespace Slic3r { namespace Utils {

int vojtikuv_pokus()
{
	std::string fname = "D:\\temp\\svg_examples\\slicer_vnitrni-menu.svg";
	try
    {
	    agg::svg::path_renderer path;
        agg::svg::parser prsr(path);
        prsr.parse(fname.c_str());
        path.arrange_orientations();
	    double m_min_x;
	    double m_min_y;
	    double m_max_x;
	    double m_max_y;
        path.bounding_rect(&m_min_x, &m_min_y, &m_max_x, &m_max_y);
//        caption(p.title());


//        typedef agg::pixfmt_bgra32 pixfmt;
		typedef agg::pixfmt_rgba32 pixfmt;
        typedef agg::renderer_base<pixfmt> renderer_base;
        typedef agg::renderer_scanline_aa_solid<renderer_base> renderer_solid;

        unsigned int width_px = 1000;
        unsigned int height_px = 1000;
		std::vector<unsigned char> buffer(width_px * height_px * 4, 0);
		agg::rendering_buffer rbuf(reinterpret_cast<pixfmt::value_type*>(buffer.data()), width_px, height_px, int(width_px * 4));
        pixfmt pixf(rbuf);
        renderer_base rb(pixf);
        renderer_solid ren(rb);

        rb.clear(agg::rgba(1,1,1));

        agg::rasterizer_scanline_aa<> ras;
        agg::scanline_p8 sl;
        agg::trans_affine mtx;

        ras.gamma(agg::gamma_power(1.));
        mtx *= agg::trans_affine_translation(- m_min_x, - m_min_y);
		mtx *= agg::trans_affine_scaling(double(width_px) / (m_max_x - m_min_x), double(height_px) / (m_max_y - m_min_y));
        
        path.expand(1.);
        path.render(ras, sl, ren, mtx, rb.clip_box(), 1.0);

//        ren.color(agg::rgba(0,0,0));
        ren.color(agg::rgba(1,1,1));
		agg::render_scanlines(ras, sl, ren);






        try {
			boost::nowide::ofstream c;
			c.open("d:\\temp\\rasterized.png", std::ios::out | std::ios::binary | std::ios::trunc);
            png::writer<std::ostream> wr(c);
            wr.set_bit_depth(8);
            wr.set_color_type(png::color_type_rgb_alpha);
            wr.set_width(width_px);
            wr.set_height(height_px);
            wr.set_compression_type(png::compression_type_default);
            wr.write_info();
            auto ptr = reinterpret_cast<png::byte*>(buffer.data());
            unsigned stride = 4 * width_px;
            for(unsigned r = 0; r < height_px; ++ r, ptr += stride)
                wr.write_row(ptr);
            wr.write_end_info();
			c.close();
		}
		catch (std::exception &ex) {
			printf("Hu!");
		}
    }
    catch(agg::svg::exception& e)
    {
//        app.message(e.msg());
    }

	return 0;
}

} } // namespace Slic3r::Utils
