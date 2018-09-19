//#ifndef NDEBUG
//#define NFP_DEBUG
//#endif

#include "libnfpglue.hpp"
#include "tools/libnfporb/libnfporb.hpp"

namespace libnest2d {

namespace  {
inline bool vsort(const libnfporb::point_t& v1, const libnfporb::point_t& v2)
{
    using Coord = libnfporb::coord_t;
    Coord x1 = v1.x_, x2 = v2.x_, y1 = v1.y_, y2 = v2.y_;
    auto diff = y1 - y2;
#ifdef LIBNFP_USE_RATIONAL
    long double diffv = diff.convert_to<long double>();
#else
    long double diffv = diff.val();
#endif
    if(std::abs(diffv) <=
            std::numeric_limits<Coord>::epsilon())
        return x1 < x2;

    return diff < 0;
}

TCoord<PointImpl> getX(const libnfporb::point_t& p) {
#ifdef LIBNFP_USE_RATIONAL
    return p.x_.convert_to<TCoord<PointImpl>>();
#else
    return static_cast<TCoord<PointImpl>>(std::round(p.x_.val()));
#endif
}

TCoord<PointImpl> getY(const libnfporb::point_t& p) {
#ifdef LIBNFP_USE_RATIONAL
    return p.y_.convert_to<TCoord<PointImpl>>();
#else
    return static_cast<TCoord<PointImpl>>(std::round(p.y_.val()));
#endif
}

libnfporb::point_t scale(const libnfporb::point_t& p, long double factor) {
#ifdef LIBNFP_USE_RATIONAL
    auto px = p.x_.convert_to<long double>();
    auto py = p.y_.convert_to<long double>();
#else
    long double px = p.x_.val();
    long double py = p.y_.val();
#endif
    return {px*factor, py*factor};
}

}

NfpR _nfp(const PolygonImpl &sh, const PolygonImpl &cother)
{
    namespace sl = shapelike;

    NfpR ret;

    try {
        libnfporb::polygon_t pstat, porb;

        boost::geometry::convert(sh, pstat);
        boost::geometry::convert(cother, porb);

        long double factor = 0.0000001;//libnfporb::NFP_EPSILON;
        long double refactor = 1.0/factor;

        for(auto& v : pstat.outer()) v = scale(v, factor);
//        std::string message;
//        boost::geometry::is_valid(pstat, message);
//        std::cout << message << std::endl;
        for(auto& h : pstat.inners()) for(auto& v : h) v = scale(v, factor);

        for(auto& v : porb.outer()) v = scale(v, factor);
//        message;
//        boost::geometry::is_valid(porb, message);
//        std::cout << message << std::endl;
        for(auto& h : porb.inners()) for(auto& v : h) v = scale(v, factor);


        // this can throw
        auto nfp = libnfporb::generateNFP(pstat, porb, true);

        auto &ct = sl::getContour(ret.first);
        ct.reserve(nfp.front().size()+1);
        for(auto v : nfp.front()) {
            v = scale(v, refactor);
            ct.emplace_back(getX(v), getY(v));
        }
        ct.push_back(ct.front());
        std::reverse(ct.begin(), ct.end());

        auto &rholes = sl::holes(ret.first);
        for(size_t hidx = 1; hidx < nfp.size(); ++hidx) {
            if(nfp[hidx].size() >= 3) {
                rholes.emplace_back();
                auto& h = rholes.back();
                h.reserve(nfp[hidx].size()+1);

                for(auto& v : nfp[hidx]) {
                    v = scale(v, refactor);
                    h.emplace_back(getX(v), getY(v));
                }
                h.push_back(h.front());
                std::reverse(h.begin(), h.end());
            }
        }

        ret.second = nfp::referenceVertex(ret.first);

    } catch(std::exception& e) {
        std::cout << "Error: " << e.what() << "\nTrying with convex hull..." << std::endl;
//        auto ch_stat = ShapeLike::convexHull(sh);
//        auto ch_orb = ShapeLike::convexHull(cother);
        ret = nfp::nfpConvexOnly(sh, cother);
    }

    return ret;
}

NfpR nfp::NfpImpl<PolygonImpl, nfp::NfpLevel::CONVEX_ONLY>::operator()(
        const PolygonImpl &sh, const ClipperLib::PolygonImpl &cother)
{
    return _nfp(sh, cother);//nfpConvexOnly(sh, cother);
}

NfpR nfp::NfpImpl<PolygonImpl, nfp::NfpLevel::ONE_CONVEX>::operator()(
        const PolygonImpl &sh, const ClipperLib::PolygonImpl &cother)
{
    return _nfp(sh, cother);
}

NfpR nfp::NfpImpl<PolygonImpl, nfp::NfpLevel::BOTH_CONCAVE>::operator()(
        const PolygonImpl &sh, const ClipperLib::PolygonImpl &cother)
{
    return _nfp(sh, cother);
}

//PolygonImpl
//Nfp::NfpImpl<PolygonImpl, NfpLevel::ONE_CONVEX_WITH_HOLES>::operator()(
//        const PolygonImpl &sh, const ClipperLib::PolygonImpl &cother)
//{
//    return _nfp(sh, cother);
//}

//PolygonImpl
//Nfp::NfpImpl<PolygonImpl, NfpLevel::BOTH_CONCAVE_WITH_HOLES>::operator()(
//        const PolygonImpl &sh, const ClipperLib::PolygonImpl &cother)
//{
//    return _nfp(sh, cother);
//}

}
