#include "3DScene.hpp"

namespace Slic3r {

void
_3DScene::_extrusionentity_to_verts_do(const Lines &lines, const std::vector<double> &widths,
        const std::vector<double> &heights, bool closed, double top_z, const Point &copy,
        Pointf3s* qverts, Pointf3s* qnorms, Pointf3s* tverts, Pointf3s* tnorms)
{
    Line prev_line;
    Pointf prev_b1, prev_b2;
    Vectorf3 prev_xy_left_normal, prev_xy_right_normal;
    
    // loop once more in case of closed loops
    bool first_done = false;
    for (int i = 0; i <= lines.size(); ++i) {
        if (i == lines.size()) i = 0;
        
        const Line &line = lines.at(i);
        if (i == 0 && first_done && !closed) break;
        
        double len = line.length();
        if (len == 0) continue;
        double unscaled_len = unscale(len);
        
        double bottom_z = top_z - heights.at(i);
        double middle_z = (top_z + bottom_z) / 2;
        double dist = widths.at(i)/2;  // scaled
        
        Vectorf v = Vectorf::new_unscale(line.vector());
        v.scale(1/unscaled_len);
        
        Pointf a = Pointf::new_unscale(line.a);
        Pointf b = Pointf::new_unscale(line.b);
        Pointf a1 = a;
        Pointf a2 = a;
        a1.translate(+dist*v.y, -dist*v.x);
        a2.translate(-dist*v.y, +dist*v.x);
        Pointf b1 = b;
        Pointf b2 = b;
        b1.translate(+dist*v.y, -dist*v.x);
        b2.translate(-dist*v.y, +dist*v.x);
        
        // calculate new XY normals
        Vector n = line.normal();
        Vectorf3 xy_right_normal = Vectorf3::new_unscale(n.x, n.y, 0);
        xy_right_normal.scale(1/unscaled_len);
        Vectorf3 xy_left_normal = xy_right_normal;
        xy_left_normal.scale(-1);
        
        if (first_done) {
            // if we're making a ccw turn, draw the triangles on the right side, otherwise draw them on the left side
            double ccw = line.b.ccw(prev_line);
            if (ccw > EPSILON) {
                // top-right vertex triangle between previous line and this one
                {
                    // use the normal going to the right calculated for the previous line
                    tnorms->push_back(prev_xy_right_normal);
                    tverts->push_back(Pointf3(prev_b1.x, prev_b1.y, middle_z));
            
                    // use the normal going to the right calculated for this line
                    tnorms->push_back(xy_right_normal);
                    tverts->push_back(Pointf3(a1.x, a1.y, middle_z));
            
                    // normal going upwards
                    tnorms->push_back(Pointf3(0,0,1));
                    tverts->push_back(Pointf3(a.x, a.y, top_z));
                }
                // bottom-right vertex triangle between previous line and this one
                {
                    // use the normal going to the right calculated for the previous line
                    tnorms->push_back(prev_xy_right_normal);
                    tverts->push_back(Pointf3(prev_b1.x, prev_b1.y, middle_z));
            
                    // normal going downwards
                    tnorms->push_back(Pointf3(0,0,-1));
                    tverts->push_back(Pointf3(a.x, a.y, bottom_z));
            
                    // use the normal going to the right calculated for this line
                    tnorms->push_back(xy_right_normal);
                    tverts->push_back(Pointf3(a1.x, a1.y, middle_z));
                }
            } else if (ccw < -EPSILON) {
                // top-left vertex triangle between previous line and this one
                {
                    // use the normal going to the left calculated for the previous line
                    tnorms->push_back(prev_xy_left_normal);
                    tverts->push_back(Pointf3(prev_b2.x, prev_b2.y, middle_z));
            
                    // normal going upwards
                    tnorms->push_back(Pointf3(0,0,1));
                    tverts->push_back(Pointf3(a.x, a.y, top_z));
            
                    // use the normal going to the right calculated for this line
                    tnorms->push_back(xy_left_normal);
                    tverts->push_back(Pointf3(a2.x, a2.y, middle_z));
                }
                // bottom-left vertex triangle between previous line and this one
                {
                    // use the normal going to the left calculated for the previous line
                    tnorms->push_back(prev_xy_left_normal);
                    tverts->push_back(Pointf3(prev_b2.x, prev_b2.y, middle_z));
            
                    // use the normal going to the right calculated for this line
                    tnorms->push_back(xy_left_normal);
                    tverts->push_back(Pointf3(a2.x, a2.y, middle_z));
            
                    // normal going downwards
                    tnorms->push_back(Pointf3(0,0,-1));
                    tverts->push_back(Pointf3(a.x, a.y, bottom_z));
                }
            }
        }
        
        // if this was the extra iteration we were only interested in the triangles
        if (first_done && i == 0) break;
        
        prev_line = line;
        prev_b1 = b1;
        prev_b2 = b2;
        prev_xy_right_normal = xy_right_normal;
        prev_xy_left_normal  = xy_left_normal;
        
        if (!closed) {
            // terminate open paths with caps
            if (i == 0) {
                // normal pointing downwards
                qnorms->push_back(Pointf3(0,0,-1));
                qverts->push_back(Pointf3(a.x, a.y, bottom_z));
            
                // normal pointing to the right
                qnorms->push_back(xy_right_normal);
                qverts->push_back(Pointf3(a1.x, a1.y, middle_z));
            
                // normal pointing upwards
                qnorms->push_back(Pointf3(0,0,1));
                qverts->push_back(Pointf3(a.x, a.y, top_z));
            
                // normal pointing to the left
                qnorms->push_back(xy_left_normal);
                qverts->push_back(Pointf3(a2.x, a2.y, middle_z));
            } else if (i == lines.size()-1) {
                // normal pointing downwards
                qnorms->push_back(Pointf3(0,0,-1));
                qverts->push_back(Pointf3(b.x, b.y, bottom_z));
            
                // normal pointing to the left
                qnorms->push_back(xy_left_normal);
                qverts->push_back(Pointf3(b2.x, b2.y, middle_z));
            
                // normal pointing upwards
                qnorms->push_back(Pointf3(0,0,1));
                qverts->push_back(Pointf3(b.x, b.y, top_z));
            
                // normal pointing to the right
                qnorms->push_back(xy_right_normal);
                qverts->push_back(Pointf3(b1.x, b1.y, middle_z));
            }
        }
        
        // bottom-right face
        {
            // normal going downwards
            qnorms->push_back(Pointf3(0,0,-1));
            qnorms->push_back(Pointf3(0,0,-1));
            qverts->push_back(Pointf3(a.x, a.y, bottom_z));
            qverts->push_back(Pointf3(b.x, b.y, bottom_z));
            
            qnorms->push_back(xy_right_normal);
            qnorms->push_back(xy_right_normal);
            qverts->push_back(Pointf3(b1.x, b1.y, middle_z));
            qverts->push_back(Pointf3(a1.x, a1.y, middle_z));
        }
        
        // top-right face
        {
            qnorms->push_back(xy_right_normal);
            qnorms->push_back(xy_right_normal);
            qverts->push_back(Pointf3(a1.x, a1.y, middle_z));
            qverts->push_back(Pointf3(b1.x, b1.y, middle_z));
            
            // normal going upwards
            qnorms->push_back(Pointf3(0,0,1));
            qnorms->push_back(Pointf3(0,0,1));
            qverts->push_back(Pointf3(b.x, b.y, top_z));
            qverts->push_back(Pointf3(a.x, a.y, top_z));
        }
         
        // top-left face
        {
            qnorms->push_back(Pointf3(0,0,1));
            qnorms->push_back(Pointf3(0,0,1));
            qverts->push_back(Pointf3(a.x, a.y, top_z));
            qverts->push_back(Pointf3(b.x, b.y, top_z));
            
            qnorms->push_back(xy_left_normal);
            qnorms->push_back(xy_left_normal);
            qverts->push_back(Pointf3(b2.x, b2.y, middle_z));
            qverts->push_back(Pointf3(a2.x, a2.y, middle_z));
        }
        
        // bottom-left face
        {
            qnorms->push_back(xy_left_normal);
            qnorms->push_back(xy_left_normal);
            qverts->push_back(Pointf3(a2.x, a2.y, middle_z));
            qverts->push_back(Pointf3(b2.x, b2.y, middle_z));
            
            // normal going downwards
            qnorms->push_back(Pointf3(0,0,-1));
            qnorms->push_back(Pointf3(0,0,-1));
            qverts->push_back(Pointf3(b.x, b.y, bottom_z));
            qverts->push_back(Pointf3(a.x, a.y, bottom_z));
        }
        
        first_done = true;
    }
}

}
