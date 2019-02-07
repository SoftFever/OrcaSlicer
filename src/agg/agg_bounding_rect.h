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

#ifndef AGG_BOUNDING_RECT_INCLUDED
#define AGG_BOUNDING_RECT_INCLUDED

#include "agg_basics.h"

namespace agg
{

    //-----------------------------------------------------------bounding_rect
    template<class VertexSource, class GetId, class CoordT>
    bool bounding_rect(VertexSource& vs, GetId& gi, 
                       unsigned start, unsigned num, 
                       CoordT* x1, CoordT* y1, CoordT* x2, CoordT* y2)
    {
        unsigned i;
        double x;
        double y;
        bool first = true;

        *x1 = CoordT(1);
        *y1 = CoordT(1);
        *x2 = CoordT(0);
        *y2 = CoordT(0);

        for(i = 0; i < num; i++)
        {
            vs.rewind(gi[start + i]);
            unsigned cmd;
            while(!is_stop(cmd = vs.vertex(&x, &y)))
            {
                if(is_vertex(cmd))
                {
                    if(first)
                    {
                        *x1 = CoordT(x);
                        *y1 = CoordT(y);
                        *x2 = CoordT(x);
                        *y2 = CoordT(y);
                        first = false;
                    }
                    else
                    {
                        if(CoordT(x) < *x1) *x1 = CoordT(x);
                        if(CoordT(y) < *y1) *y1 = CoordT(y);
                        if(CoordT(x) > *x2) *x2 = CoordT(x);
                        if(CoordT(y) > *y2) *y2 = CoordT(y);
                    }
                }
            }
        }
        return *x1 <= *x2 && *y1 <= *y2;
    }


    //-----------------------------------------------------bounding_rect_single
    template<class VertexSource, class CoordT> 
    bool bounding_rect_single(VertexSource& vs, unsigned path_id,
                              CoordT* x1, CoordT* y1, CoordT* x2, CoordT* y2)
    {
        double x;
        double y;
        bool first = true;

        *x1 = CoordT(1);
        *y1 = CoordT(1);
        *x2 = CoordT(0);
        *y2 = CoordT(0);

        vs.rewind(path_id);
        unsigned cmd;
        while(!is_stop(cmd = vs.vertex(&x, &y)))
        {
            if(is_vertex(cmd))
            {
                if(first)
                {
                    *x1 = CoordT(x);
                    *y1 = CoordT(y);
                    *x2 = CoordT(x);
                    *y2 = CoordT(y);
                    first = false;
                }
                else
                {
                    if(CoordT(x) < *x1) *x1 = CoordT(x);
                    if(CoordT(y) < *y1) *y1 = CoordT(y);
                    if(CoordT(x) > *x2) *x2 = CoordT(x);
                    if(CoordT(y) > *y2) *y2 = CoordT(y);
                }
            }
        }
        return *x1 <= *x2 && *y1 <= *y2;
    }


}

#endif
