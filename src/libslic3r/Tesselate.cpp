#include "Tesselate.hpp"

#include "ExPolygon.hpp"

#include <glu-libtess.h>

namespace Slic3r {

class GluTessWrapper {
public:
    Pointf3s tesselate(const ExPolygon &expoly, double z_, bool flipped_)
    {
        z = z_;
        flipped = flipped_;
        triangles.clear();
        intersection_points.clear();
        std::vector<GLdouble> coords;
        {
            size_t num_coords = expoly.contour.points.size();
            for (const Polygon &poly : expoly.holes)
                num_coords += poly.points.size();
            coords.reserve(num_coords * 3);
        }
        GLUtesselator *tess = gluNewTess(); // create a tessellator
        // register callback functions
#ifndef _GLUfuncptr
    #ifdef _MSC_VER
        typedef void (__stdcall *_GLUfuncptr)(void);
    #else /* _MSC_VER */
        #ifdef GLAPIENTRYP
            typedef void (GLAPIENTRYP _GLUfuncptr)(void);
        #else /* GLAPIENTRYP */
            typedef void (*_GLUfuncptr)(void);
        #endif
    #endif /* _MSC_VER */
#endif /* _GLUfuncptr */
        gluTessCallback(tess, GLU_TESS_BEGIN_DATA,   (_GLUfuncptr)tessBeginCB);
        gluTessCallback(tess, GLU_TESS_END_DATA,     (_GLUfuncptr)tessEndCB);
        gluTessCallback(tess, GLU_TESS_ERROR_DATA,   (_GLUfuncptr)tessErrorCB);
        gluTessCallback(tess, GLU_TESS_VERTEX_DATA,  (_GLUfuncptr)tessVertexCB);
        gluTessCallback(tess, GLU_TESS_COMBINE_DATA, (_GLUfuncptr)tessCombineCB);
        gluTessBeginPolygon(tess, (void*)this);
        gluTessBeginContour(tess);
        for (const Point &pt : expoly.contour.points) {
            coords.emplace_back(unscale<double>(pt[0]));
            coords.emplace_back(unscale<double>(pt[1]));
            coords.emplace_back(0.);
            gluTessVertex(tess, &coords[coords.size() - 3], &coords[coords.size() - 3]);
        }
        gluTessEndContour(tess);
        for (const Polygon &poly : expoly.holes) {
            gluTessBeginContour(tess);
            for (const Point &pt : poly.points) {
                coords.emplace_back(unscale<double>(pt[0]));
                coords.emplace_back(unscale<double>(pt[1]));
                coords.emplace_back(0.);
                gluTessVertex(tess, &coords[coords.size() - 3], &coords[coords.size() - 3]);
            }
            gluTessEndContour(tess);
        }
        gluTessEndPolygon(tess);
        gluDeleteTess(tess);
        return std::move(triangles);
    }

    Pointf3s tesselate(const ExPolygons &expolygons, double z_, bool flipped_)
    {
        z = z_;
        flipped = flipped_;
        triangles.clear();
        intersection_points.clear();
        std::vector<GLdouble> coords;
        {
            size_t num_coords = 0;
            for (const ExPolygon &expoly : expolygons) {
                size_t num_coords_this = expoly.contour.points.size();
                for (const Polygon &poly : expoly.holes)
                    num_coords_this += poly.points.size();
                num_coords = std::max(num_coords, num_coords_this);
            }
            coords.assign(num_coords * 3, 0);
        }
        GLUtesselator *tess = gluNewTess(); // create a tessellator
        // register callback functions
#ifndef _GLUfuncptr
    #ifdef _MSC_VER
        typedef void (__stdcall *_GLUfuncptr)(void);
    #else /* _MSC_VER */
        #ifdef GLAPIENTRYP
            typedef void (GLAPIENTRYP _GLUfuncptr)(void);
        #else /* GLAPIENTRYP */
            typedef void (*_GLUfuncptr)(void);
        #endif
    #endif /* _MSC_VER */
#endif /* _GLUfuncptr */
        gluTessCallback(tess, GLU_TESS_BEGIN_DATA,   (_GLUfuncptr)tessBeginCB);
        gluTessCallback(tess, GLU_TESS_END_DATA,     (_GLUfuncptr)tessEndCB);
        gluTessCallback(tess, GLU_TESS_ERROR_DATA,   (_GLUfuncptr)tessErrorCB);
        gluTessCallback(tess, GLU_TESS_VERTEX_DATA,  (_GLUfuncptr)tessVertexCB);
        gluTessCallback(tess, GLU_TESS_COMBINE_DATA, (_GLUfuncptr)tessCombineCB);
        for (const ExPolygon &expoly : expolygons) {
            gluTessBeginPolygon(tess, (void*)this);
            gluTessBeginContour(tess);
            size_t idx = 0;
            for (const Point &pt : expoly.contour.points) {
                coords[idx ++] = unscale<double>(pt[0]);
                coords[idx ++] = unscale<double>(pt[1]);
                coords[idx ++] = 0.;
                gluTessVertex(tess, &coords[idx - 3], &coords[idx - 3]);
            }
            gluTessEndContour(tess);
            for (const Polygon &poly : expoly.holes) {
                gluTessBeginContour(tess);
                for (const Point &pt : poly.points) {
                    coords[idx ++] = unscale<double>(pt[0]);
                    coords[idx ++] = unscale<double>(pt[1]);
                    coords[idx ++] = 0.;
                    gluTessVertex(tess, &coords[idx - 3], &coords[idx - 3]);
                }
                gluTessEndContour(tess);
            }
            gluTessEndPolygon(tess);
        }
        gluDeleteTess(tess);
        return std::move(triangles);
    }

private:
    static void tessBeginCB(GLenum which, void *polygonData)        { reinterpret_cast<GluTessWrapper*>(polygonData)->tessBegin(which); }
    static void tessEndCB(void *polygonData)                        { reinterpret_cast<GluTessWrapper*>(polygonData)->tessEnd(); }
    static void tessVertexCB(const GLvoid *data, void *polygonData) { reinterpret_cast<GluTessWrapper*>(polygonData)->tessVertex(data); }
    static void tessCombineCB(const GLdouble newVertex[3], const GLdouble *neighborVertex[4], const GLfloat neighborWeight[4], GLdouble **outData, void *polygonData)
                                                                    { reinterpret_cast<GluTessWrapper*>(polygonData)->tessCombine(newVertex, neighborVertex, neighborWeight, outData); }
    static void tessErrorCB(GLenum errorCode, void *polygonData)    { reinterpret_cast<GluTessWrapper*>(polygonData)->tessError(errorCode); }

    void tessBegin(GLenum which)
    {
        assert(which == GL_TRIANGLES || which == GL_TRIANGLE_FAN || which == GL_TRIANGLE_STRIP);
        if (!(which == GL_TRIANGLES || which == GL_TRIANGLE_FAN || which == GL_TRIANGLE_STRIP))
            printf("Co je to za haluz!?\n");
        primitive_type = which;
        num_points = 0;
    }

    void tessEnd()
    {
        num_points = 0;
    }

    void tessVertex(const GLvoid *data)
    {
        if (data == nullptr)
            return;
        const GLdouble *ptr = (const GLdouble*)data;
        ++ num_points;
        if (num_points == 1) {
            memcpy(pt0, ptr, sizeof(GLdouble) * 3);
        } else if (num_points == 2) {
            memcpy(pt1, ptr, sizeof(GLdouble) * 3);
        } else {
            bool flip = flipped;
            if (primitive_type == GL_TRIANGLE_STRIP && num_points == 4) {
                flip = !flip;
                num_points = 2;
            }
            triangles.emplace_back(pt0[0], pt0[1], z);
            if (flip) {
                triangles.emplace_back(ptr[0], ptr[1], z);
                triangles.emplace_back(pt1[0], pt1[1], z);
            } else {
                triangles.emplace_back(pt1[0], pt1[1], z);
                triangles.emplace_back(ptr[0], ptr[1], z);
            }
            if (primitive_type == GL_TRIANGLE_STRIP) {
                memcpy(pt0, pt1, sizeof(GLdouble) * 3);
                memcpy(pt1, ptr, sizeof(GLdouble) * 3);
            } else if (primitive_type == GL_TRIANGLE_FAN) {
                memcpy(pt1, ptr, sizeof(GLdouble) * 3);
            } else {
                assert(primitive_type == GL_TRIANGLES);
                assert(num_points == 3);
                num_points = 0;
            }
        }
    }

    void tessCombine(const GLdouble newVertex[3], const GLdouble *neighborVertex[4], const GLfloat neighborWeight[4], GLdouble **outData)
    {
        intersection_points.emplace_back(newVertex[0], newVertex[1], newVertex[2]);
        *outData = intersection_points.back().data();
    }

    static void tessError(GLenum errorCode)
    {
//        const GLubyte *errorStr;
//        errorStr = gluErrorString(errorCode);
//        printf("Error: %s\n", (const char*)errorStr);
    }

    GLenum   primitive_type;
    GLdouble pt0[3];
    GLdouble pt1[3];
    int      num_points;
    Pointf3s triangles;
    std::deque<Vec3d> intersection_points;
    double   z;
    bool     flipped;
};

Pointf3s triangulate_expolygons_3df(const ExPolygon &poly, coordf_t z, bool flip)
{
    GluTessWrapper tess;
    return tess.tesselate(poly, z, flip);
}

Pointf3s triangulate_expolygons_3df(const ExPolygons &polys, coordf_t z, bool flip)
{
	GluTessWrapper tess;
    return tess.tesselate(polys, z, flip);
}

} // namespace Slic3r
