//----------------------------------------------------------------------------
// Anti-Grain Geometry (AGG) - Version 2.5
// A high quality rendering engine for C++
// Copyright (C) 2002-2006 Maxim Shemanarev
// Contact: mcseem@antigrain.com
//          mcseemagg@yahoo.com
//          http://antigrain.com
// 
// AGG is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
// 
// AGG is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
// 
// You should have received a copy of the GNU General Public License
// along with AGG; if not, write to the Free Software
// Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, 
// MA 02110-1301, USA.
//----------------------------------------------------------------------------

#ifndef AGG_CONV_STROKE_INCLUDED
#define AGG_CONV_STROKE_INCLUDED

#include "agg_basics.h"
#include "agg_vcgen_stroke.h"
#include "agg_conv_adaptor_vcgen.h"

namespace agg
{

    //-------------------------------------------------------------conv_stroke
    template<class VertexSource, class Markers=null_markers> 
    struct conv_stroke : 
    public conv_adaptor_vcgen<VertexSource, vcgen_stroke, Markers>
    {
        typedef Markers marker_type;
        typedef conv_adaptor_vcgen<VertexSource, vcgen_stroke, Markers> base_type;

        conv_stroke(VertexSource& vs) : 
            conv_adaptor_vcgen<VertexSource, vcgen_stroke, Markers>(vs)
        {
        }

        void line_cap(line_cap_e lc)     { base_type::generator().line_cap(lc);  }
        void line_join(line_join_e lj)   { base_type::generator().line_join(lj); }
        void inner_join(inner_join_e ij) { base_type::generator().inner_join(ij); }

        line_cap_e   line_cap()   const { return base_type::generator().line_cap();  }
        line_join_e  line_join()  const { return base_type::generator().line_join(); }
        inner_join_e inner_join() const { return base_type::generator().inner_join(); }

        void width(double w) { base_type::generator().width(w); }
        void miter_limit(double ml) { base_type::generator().miter_limit(ml); }
        void miter_limit_theta(double t) { base_type::generator().miter_limit_theta(t); }
        void inner_miter_limit(double ml) { base_type::generator().inner_miter_limit(ml); }
        void approximation_scale(double as) { base_type::generator().approximation_scale(as); }

        double width() const { return base_type::generator().width(); }
        double miter_limit() const { return base_type::generator().miter_limit(); }
        double inner_miter_limit() const { return base_type::generator().inner_miter_limit(); }
        double approximation_scale() const { return base_type::generator().approximation_scale(); }

        void shorten(double s) { base_type::generator().shorten(s); }
        double shorten() const { return base_type::generator().shorten(); }

    private:
       conv_stroke(const conv_stroke<VertexSource, Markers>&);
       const conv_stroke<VertexSource, Markers>& 
           operator = (const conv_stroke<VertexSource, Markers>&);

    };

}

#endif
