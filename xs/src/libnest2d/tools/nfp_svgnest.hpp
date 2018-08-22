#ifndef NFP_SVGNEST_HPP
#define NFP_SVGNEST_HPP

#include <limits>
#include <unordered_map>

#include <libnest2d/geometry_traits_nfp.hpp>

namespace libnest2d {

namespace __svgnest {

using std::sqrt;
using std::min;
using std::max;
using std::abs;
using std::isnan;

//template<class Coord> struct _Scale {
//    static const BP2D_CONSTEXPR long long Value = 1000000;
//};

template<class S> struct _alg {
    using Contour = TContour<S>;
    using Point = TPoint<S>;
    using iCoord = TCoord<Point>;
    using Coord = double;
    using Shapes = nfp::Shapes<S>;

    static const Coord TOL;

#define dNAN std::nan("")

    struct Vector {
        Coord x = 0.0, y = 0.0;
        bool marked = false;
        Vector() = default;
        Vector(Coord X, Coord Y): x(X), y(Y) {}
        Vector(const Point& p): x(Coord(getX(p))), y(Coord(getY(p))) {}
        operator Point() const { return {iCoord(x), iCoord(y)}; }
        Vector& operator=(const Point& p) {
            x = getX(p), y = getY(p); return *this;
        }
        bool operator!=(const Vector& v) const {
            return v.x != x || v.y != y;
        }
        Vector(std::initializer_list<Coord> il):
            x(*il.begin()), y(*std::next(il.begin())) {}
    };

    static inline Coord x(const Point& p) { return Coord(getX(p)); }
    static inline Coord y(const Point& p) { return Coord(getY(p)); }

    static inline Coord x(const Vector& p) { return p.x; }
    static inline Coord y(const Vector& p) { return p.y; }

    class Cntr {
        std::vector<Vector> v_;
    public:
        Cntr(const Contour& c) {
            v_.reserve(c.size());
            std::transform(c.begin(), c.end(), std::back_inserter(v_),
                           [](const Point& p) {
                return Vector(double(x(p)) / 1e6, double(y(p)) / 1e6);
            });
            std::reverse(v_.begin(), v_.end());
            v_.pop_back();
        }
        Cntr() = default;

        Coord offsetx = 0;
        Coord offsety = 0;
        size_t size() const { return v_.size(); }
        bool empty() const { return v_.empty(); }
        typename std::vector<Vector>::const_iterator cbegin() const { return v_.cbegin(); }
        typename std::vector<Vector>::const_iterator cend() const { return v_.cend(); }
        typename std::vector<Vector>::iterator begin() { return v_.begin(); }
        typename std::vector<Vector>::iterator end() { return v_.end(); }
        Vector& operator[](size_t idx) { return v_[idx]; }
        const Vector& operator[](size_t idx) const { return v_[idx]; }
        template<class...Args>
        void emplace_back(Args&&...args) {
            v_.emplace_back(std::forward<Args>(args)...);
        }
        template<class...Args>
        void push(Args&&...args) {
            v_.emplace_back(std::forward<Args>(args)...);
        }
        void clear() { v_.clear(); }

        operator Contour() const {
            Contour cnt;
            cnt.reserve(v_.size() + 1);
            std::transform(v_.begin(), v_.end(), std::back_inserter(cnt),
                           [](const Vector& vertex) {
                return Point(iCoord(vertex.x) * 1000000, iCoord(vertex.y) * 1000000);
            });
            if(!cnt.empty()) cnt.emplace_back(cnt.front());
            S sh = shapelike::create<S>(cnt);

//            std::reverse(cnt.begin(), cnt.end());
            return shapelike::getContour(sh);
        }
    };

    inline static bool _almostEqual(Coord a, Coord b,
                                    Coord tolerance = TOL)
    {
        return std::abs(a - b) < tolerance;
    }

    // returns true if p lies on the line segment defined by AB,
    // but not at any endpoints may need work!
    static bool _onSegment(const Vector& A, const Vector& B, const Vector& p) {

        // vertical line
        if(_almostEqual(A.x, B.x) && _almostEqual(p.x, A.x)) {
            if(!_almostEqual(p.y, B.y) && !_almostEqual(p.y, A.y) &&
                    p.y < max(B.y, A.y) && p.y > min(B.y, A.y)){
                return true;
            }
            else{
                return false;
            }
        }

        // horizontal line
        if(_almostEqual(A.y, B.y) && _almostEqual(p.y, A.y)){
            if(!_almostEqual(p.x, B.x) && !_almostEqual(p.x, A.x) &&
                    p.x < max(B.x, A.x) && p.x > min(B.x, A.x)){
                return true;
            }
            else{
                return false;
            }
        }

        //range check
        if((p.x < A.x && p.x < B.x) || (p.x > A.x && p.x > B.x) ||
                (p.y < A.y && p.y < B.y) || (p.y > A.y && p.y > B.y))
            return false;

        // exclude end points
        if((_almostEqual(p.x, A.x) && _almostEqual(p.y, A.y)) ||
                (_almostEqual(p.x, B.x) && _almostEqual(p.y, B.y)))
            return false;


        double cross = (p.y - A.y) * (B.x - A.x) - (p.x - A.x) * (B.y - A.y);

        if(abs(cross) > TOL) return false;

        double dot = (p.x - A.x) * (B.x - A.x) + (p.y - A.y)*(B.y - A.y);

        if(dot < 0 || _almostEqual(dot, 0)) return false;

        double len2 = (B.x - A.x)*(B.x - A.x) + (B.y - A.y)*(B.y - A.y);

        if(dot > len2 || _almostEqual(dot, len2)) return false;

        return true;
    }

    // return true if point is in the polygon, false if outside, and null if exactly on a point or edge
    static int pointInPolygon(const Vector& point, const Cntr& polygon) {
        if(polygon.size() < 3){
            return 0;
        }

        bool inside = false;
        Coord offsetx = polygon.offsetx;
        Coord offsety = polygon.offsety;

        for (size_t i = 0, j = polygon.size() - 1; i < polygon.size(); j=i++) {
            auto xi = polygon[i].x + offsetx;
            auto yi = polygon[i].y + offsety;
            auto xj = polygon[j].x + offsetx;
            auto yj = polygon[j].y + offsety;

            if(_almostEqual(xi, point.x) && _almostEqual(yi, point.y)){
                return 0; // no result
            }

            if(_onSegment({xi, yi}, {xj, yj}, point)){
                return 0; // exactly on the segment
            }

            if(_almostEqual(xi, xj) && _almostEqual(yi, yj)){ // ignore very small lines
                continue;
            }

            bool intersect = ((yi > point.y) != (yj > point.y)) &&
                    (point.x < (xj - xi) * (point.y - yi) / (yj - yi) + xi);
            if (intersect) inside = !inside;
        }

        return inside? 1 : -1;
    }

    static bool intersect(const Cntr& A, const Cntr& B){
        Contour a = A, b = B;
        return shapelike::intersects(shapelike::create<S>(a), shapelike::create<S>(b));
    }

    static Vector _normalizeVector(const Vector& v) {
        if(_almostEqual(v.x*v.x + v.y*v.y, Coord(1))){
            return Point(v); // given vector was already a unit vector
        }
        auto len = sqrt(v.x*v.x + v.y*v.y);
        auto inverse = 1/len;

        return { Coord(v.x*inverse), Coord(v.y*inverse) };
    }

    static double pointDistance( const Vector& p,
                                 const Vector& s1,
                                 const Vector& s2,
                                 Vector normal,
                                 bool infinite = false)
    {
        normal = _normalizeVector(normal);

        Vector dir = {
            normal.y,
            -normal.x
        };

        auto pdot = p.x*dir.x + p.y*dir.y;
        auto s1dot = s1.x*dir.x + s1.y*dir.y;
        auto s2dot = s2.x*dir.x + s2.y*dir.y;

        auto pdotnorm = p.x*normal.x + p.y*normal.y;
        auto s1dotnorm = s1.x*normal.x + s1.y*normal.y;
        auto s2dotnorm = s2.x*normal.x + s2.y*normal.y;

        if(!infinite){
            if (((pdot<s1dot || _almostEqual(pdot, s1dot)) &&
                 (pdot<s2dot || _almostEqual(pdot, s2dot))) ||
                    ((pdot>s1dot || _almostEqual(pdot, s1dot)) &&
                     (pdot>s2dot || _almostEqual(pdot, s2dot))))
            {
                // dot doesn't collide with segment,
                // or lies directly on the vertex
                return dNAN;
            }
            if ((_almostEqual(pdot, s1dot) && _almostEqual(pdot, s2dot)) &&
                    (pdotnorm>s1dotnorm && pdotnorm>s2dotnorm))
            {
                return min(pdotnorm - s1dotnorm, pdotnorm - s2dotnorm);
            }
            if ((_almostEqual(pdot, s1dot) && _almostEqual(pdot, s2dot)) &&
                    (pdotnorm<s1dotnorm && pdotnorm<s2dotnorm)){
                return -min(s1dotnorm-pdotnorm, s2dotnorm-pdotnorm);
            }
        }

        return -(pdotnorm - s1dotnorm + (s1dotnorm - s2dotnorm)*(s1dot - pdot)
                 / double(s1dot - s2dot));
    }

    static double segmentDistance( const Vector& A,
                                   const Vector& B,
                                   const Vector& E,
                                   const Vector& F,
                                   Vector direction)
    {
        Vector normal = {
            direction.y,
            -direction.x
        };

        Vector reverse = {
            -direction.x,
            -direction.y
        };

        auto dotA = A.x*normal.x + A.y*normal.y;
        auto dotB = B.x*normal.x + B.y*normal.y;
        auto dotE = E.x*normal.x + E.y*normal.y;
        auto dotF = F.x*normal.x + F.y*normal.y;

        auto crossA = A.x*direction.x + A.y*direction.y;
        auto crossB = B.x*direction.x + B.y*direction.y;
        auto crossE = E.x*direction.x + E.y*direction.y;
        auto crossF = F.x*direction.x + F.y*direction.y;

//        auto crossABmin = min(crossA, crossB);
//        auto crossABmax = max(crossA, crossB);

//        auto crossEFmax = max(crossE, crossF);
//        auto crossEFmin = min(crossE, crossF);

        auto ABmin = min(dotA, dotB);
        auto ABmax = max(dotA, dotB);

        auto EFmax = max(dotE, dotF);
        auto EFmin = min(dotE, dotF);

        // segments that will merely touch at one point
        if(_almostEqual(ABmax, EFmin, TOL) || _almostEqual(ABmin, EFmax,TOL)) {
            return dNAN;
        }
        // segments miss eachother completely
        if(ABmax < EFmin || ABmin > EFmax){
            return dNAN;
        }

        double overlap = 0;

        if((ABmax > EFmax && ABmin < EFmin) || (EFmax > ABmax && EFmin < ABmin))
        {
            overlap = 1;
        }
        else{
            auto minMax = min(ABmax, EFmax);
            auto maxMin = max(ABmin, EFmin);

            auto maxMax = max(ABmax, EFmax);
            auto minMin = min(ABmin, EFmin);

            overlap = (minMax-maxMin)/(maxMax-minMin);
        }

        auto crossABE = (E.y - A.y) * (B.x - A.x) - (E.x - A.x) * (B.y - A.y);
        auto crossABF = (F.y - A.y) * (B.x - A.x) - (F.x - A.x) * (B.y - A.y);

        // lines are colinear
        if(_almostEqual(crossABE,0) && _almostEqual(crossABF,0)){

            Vector ABnorm = {B.y-A.y, A.x-B.x};
            Vector EFnorm = {F.y-E.y, E.x-F.x};

            auto ABnormlength = sqrt(ABnorm.x*ABnorm.x + ABnorm.y*ABnorm.y);
            ABnorm.x /= ABnormlength;
            ABnorm.y /= ABnormlength;

            auto EFnormlength = sqrt(EFnorm.x*EFnorm.x + EFnorm.y*EFnorm.y);
            EFnorm.x /= EFnormlength;
            EFnorm.y /= EFnormlength;

            // segment normals must point in opposite directions
            if(abs(ABnorm.y * EFnorm.x - ABnorm.x * EFnorm.y) < TOL &&
                    ABnorm.y * EFnorm.y + ABnorm.x * EFnorm.x < 0){
                // normal of AB segment must point in same direction as
                // given direction vector
                auto normdot = ABnorm.y * direction.y + ABnorm.x * direction.x;
                // the segments merely slide along eachother
                if(_almostEqual(normdot,0, TOL)){
                    return dNAN;
                }
                if(normdot < 0){
                    return 0.0;
                }
            }
            return dNAN;
        }

        std::vector<double> distances; distances.reserve(10);

        // coincident points
        if(_almostEqual(dotA, dotE)){
            distances.emplace_back(crossA-crossE);
        }
        else if(_almostEqual(dotA, dotF)){
            distances.emplace_back(crossA-crossF);
        }
        else if(dotA > EFmin && dotA < EFmax){
            auto d = pointDistance(A,E,F,reverse);
            if(!isnan(d) && _almostEqual(d, 0))
            { //  A currently touches EF, but AB is moving away from EF
                auto dB = pointDistance(B,E,F,reverse,true);
                if(dB < 0 || _almostEqual(dB*overlap,0)){
                    d = dNAN;
                }
            }
            if(!isnan(d)){
                distances.emplace_back(d);
            }
        }

        if(_almostEqual(dotB, dotE)){
            distances.emplace_back(crossB-crossE);
        }
        else if(_almostEqual(dotB, dotF)){
            distances.emplace_back(crossB-crossF);
        }
        else if(dotB > EFmin && dotB < EFmax){
            auto d = pointDistance(B,E,F,reverse);

            if(!isnan(d) && _almostEqual(d, 0))
            { // crossA>crossB A currently touches EF, but AB is moving away from EF
                double dA = pointDistance(A,E,F,reverse,true);
                if(dA < 0 || _almostEqual(dA*overlap,0)){
                    d = dNAN;
                }
            }
            if(!isnan(d)){
                distances.emplace_back(d);
            }
        }

        if(dotE > ABmin && dotE < ABmax){
            auto d = pointDistance(E,A,B,direction);
            if(!isnan(d) && _almostEqual(d, 0))
            { // crossF<crossE A currently touches EF, but AB is moving away from EF
                double dF = pointDistance(F,A,B,direction, true);
                if(dF < 0 || _almostEqual(dF*overlap,0)){
                    d = dNAN;
                }
            }
            if(!isnan(d)){
                distances.emplace_back(d);
            }
        }

        if(dotF > ABmin && dotF < ABmax){
            auto d = pointDistance(F,A,B,direction);
            if(!isnan(d) && _almostEqual(d, 0))
            { // && crossE<crossF A currently touches EF,
              // but AB is moving away from EF
                double dE = pointDistance(E,A,B,direction, true);
                if(dE < 0 || _almostEqual(dE*overlap,0)){
                    d = dNAN;
                }
            }
            if(!isnan(d)){
                distances.emplace_back(d);
            }
        }

        if(distances.empty()){
            return dNAN;
        }

        return *std::min_element(distances.begin(), distances.end());
    }

    static double polygonSlideDistance( const Cntr& AA,
                                        const Cntr& BB,
                                        Vector direction,
                                        bool ignoreNegative)
    {
//        Vector A1, A2, B1, B2;
        Cntr A = AA;
        Cntr B = BB;

        Coord Aoffsetx = A.offsetx;
        Coord Boffsetx = B.offsetx;
        Coord Aoffsety = A.offsety;
        Coord Boffsety = B.offsety;

        // close the loop for polygons
        if(A[0] != A[A.size()-1]){
            A.emplace_back(AA[0]);
        }

        if(B[0] != B[B.size()-1]){
            B.emplace_back(BB[0]);
        }

        auto& edgeA = A;
        auto& edgeB = B;

        double distance = dNAN, d = dNAN;

        Vector dir = _normalizeVector(direction);

//        Vector normal = {
//            dir.y,
//            -dir.x
//        };

//        Vector reverse = {
//            -dir.x,
//            -dir.y,
//        };

        for(size_t i = 0; i < edgeB.size() - 1; i++){
            for(size_t j = 0; j < edgeA.size() - 1; j++){
                Vector A1 = {x(edgeA[j]) + Aoffsetx, y(edgeA[j]) + Aoffsety };
                Vector A2 = {x(edgeA[j+1]) + Aoffsetx, y(edgeA[j+1]) + Aoffsety};
                Vector B1 = {x(edgeB[i]) + Boffsetx,  y(edgeB[i]) + Boffsety };
                Vector B2 = {x(edgeB[i+1]) + Boffsetx, y(edgeB[i+1]) + Boffsety};

                if((_almostEqual(A1.x, A2.x) && _almostEqual(A1.y, A2.y)) ||
                   (_almostEqual(B1.x, B2.x) && _almostEqual(B1.y, B2.y))){
                    continue; // ignore extremely small lines
                }

                d = segmentDistance(A1, A2, B1, B2, dir);

                if(!isnan(d) && (isnan(distance) || d < distance)){
                    if(!ignoreNegative || d > 0 || _almostEqual(d, 0)){
                        distance = d;
                    }
                }
            }
        }
        return distance;
    }

    static double polygonProjectionDistance(const Cntr& AA,
                                            const Cntr& BB,
                                            Vector direction)
    {
        Cntr A = AA;
        Cntr B = BB;

        auto Boffsetx = B.offsetx;
        auto Boffsety = B.offsety;
        auto Aoffsetx = A.offsetx;
        auto Aoffsety = A.offsety;

        // close the loop for polygons
        if(A[0] != A[A.size()-1]){
            A.push(A[0]);
        }

        if(B[0] != B[B.size()-1]){
            B.push(B[0]);
        }

        auto& edgeA = A;
        auto& edgeB = B;

        double distance = dNAN, d;
//        Vector p, s1, s2;

        for(size_t i = 0; i < edgeB.size(); i++) {
            // the shortest/most negative projection of B onto A
            double minprojection = dNAN;
            Vector minp;
            for(size_t j = 0; j < edgeA.size() - 1; j++){
                Vector p =  {x(edgeB[i]) + Boffsetx, y(edgeB[i]) + Boffsety };
                Vector s1 = {x(edgeA[j]) + Aoffsetx, y(edgeA[j]) + Aoffsety };
                Vector s2 = {x(edgeA[j+1]) + Aoffsetx, y(edgeA[j+1]) + Aoffsety };

                if(abs((s2.y-s1.y) * direction.x -
                       (s2.x-s1.x) * direction.y) < TOL) continue;

                // project point, ignore edge boundaries
                d = pointDistance(p, s1, s2, direction);

                if(!isnan(d) && (isnan(minprojection) || d < minprojection)) {
                    minprojection = d;
                    minp = p;
                }
            }

            if(!isnan(minprojection) && (isnan(distance) ||
                                         minprojection > distance)){
                distance = minprojection;
            }
        }

        return distance;
    }

    static std::pair<bool, Vector> searchStartPoint(
            const Cntr& AA, const Cntr& BB, bool inside, const std::vector<Cntr>& NFP = {})
    {
        // clone arrays
        auto A = AA;
        auto B = BB;

//        // close the loop for polygons
//        if(A[0] != A[A.size()-1]){
//            A.push(A[0]);
//        }

//        if(B[0] != B[B.size()-1]){
//            B.push(B[0]);
//        }

        // returns true if point already exists in the given nfp
        auto inNfp = [](const Vector& p, const std::vector<Cntr>& nfp){
            if(nfp.empty()){
                return false;
            }

            for(size_t i=0; i < nfp.size(); i++){
                for(size_t j = 0; j< nfp[i].size(); j++){
                    if(_almostEqual(p.x, nfp[i][j].x) &&
                       _almostEqual(p.y, nfp[i][j].y)){
                        return true;
                    }
                }
            }

            return false;
        };

        for(size_t i = 0; i < A.size() - 1; i++){
            if(!A[i].marked) {
                A[i].marked = true;
                for(size_t j = 0; j < B.size(); j++){
                    B.offsetx = A[i].x - B[j].x;
                    B.offsety = A[i].y - B[j].y;

                    int Binside = 0;
                    for(size_t k = 0; k < B.size(); k++){
                        int inpoly = pointInPolygon({B[k].x + B.offsetx, B[k].y + B.offsety}, A);
                        if(inpoly != 0){
                            Binside = inpoly;
                            break;
                        }
                    }

                    if(Binside == 0){ // A and B are the same
                        return {false, {}};
                    }

                    auto startPoint = std::make_pair(true, Vector(B.offsetx, B.offsety));
                    if(((Binside && inside) || (!Binside && !inside)) &&
                         !intersect(A,B) && !inNfp(startPoint.second, NFP)){
                        return startPoint;
                    }

                    // slide B along vector
                    auto vx = A[i+1].x - A[i].x;
                    auto vy = A[i+1].y - A[i].y;

                    double d1 = polygonProjectionDistance(A,B,{vx, vy});
                    double d2 = polygonProjectionDistance(B,A,{-vx, -vy});

                    double d = dNAN;

                    // todo: clean this up
                    if(isnan(d1) && isnan(d2)){
                        // nothin
                    }
                    else if(isnan(d1)){
                        d = d2;
                    }
                    else if(isnan(d2)){
                        d = d1;
                    }
                    else{
                        d = min(d1,d2);
                    }

                    // only slide until no longer negative
                    // todo: clean this up
                    if(!isnan(d) && !_almostEqual(d,0) && d > 0){

                    }
                    else{
                        continue;
                    }

                    auto vd2 = vx*vx + vy*vy;

                    if(d*d < vd2 && !_almostEqual(d*d, vd2)){
                        auto vd = sqrt(vx*vx + vy*vy);
                        vx *= d/vd;
                        vy *= d/vd;
                    }

                    B.offsetx += vx;
                    B.offsety += vy;

                    for(size_t k = 0; k < B.size(); k++){
                        int inpoly = pointInPolygon({B[k].x + B.offsetx, B[k].y + B.offsety}, A);
                        if(inpoly != 0){
                            Binside = inpoly;
                            break;
                        }
                    }
                    startPoint = std::make_pair(true, Vector{B.offsetx, B.offsety});
                    if(((Binside && inside) || (!Binside && !inside)) &&
                            !intersect(A,B) && !inNfp(startPoint.second, NFP)){
                        return startPoint;
                    }
                }
            }
        }

        return {false, Vector(0, 0)};
    }

    static std::vector<Cntr> noFitPolygon(Cntr A,
                                          Cntr B,
                                          bool inside,
                                          bool searchEdges)
    {
        if(A.size() < 3 || B.size() < 3) {
            throw GeometryException(GeomErr::NFP);
            return {};
        }

        A.offsetx = 0;
        A.offsety = 0;

        long i = 0, j = 0;

        auto minA = y(A[0]);
        long minAindex = 0;

        auto maxB = y(B[0]);
        long maxBindex = 0;

        for(i = 1; i < A.size(); i++){
            A[i].marked = false;
            if(y(A[i]) < minA){
                minA = y(A[i]);
                minAindex = i;
            }
        }

        for(i = 1; i < B.size(); i++){
            B[i].marked = false;
            if(y(B[i]) > maxB){
                maxB = y(B[i]);
                maxBindex = i;
            }
        }

        std::pair<bool, Vector> startpoint;

        if(!inside){
            // shift B such that the bottom-most point of B is at the top-most
            // point of A. This guarantees an initial placement with no
            // intersections
            startpoint = { true,
                { x(A[minAindex]) - x(B[maxBindex]),
                  y(A[minAindex]) - y(B[maxBindex]) }
            };
        }
        else {
            // no reliable heuristic for inside
            startpoint = searchStartPoint(A, B, true);
        }

        std::vector<Cntr> NFPlist;

        struct Touch {
            int type;
            long A;
            long B;
            Touch(int t, long a, long b): type(t), A(a), B(b) {}
        };

        while(startpoint.first) {

            B.offsetx = startpoint.second.x;
            B.offsety = startpoint.second.y;

            // maintain a list of touching points/edges
            std::vector<Touch> touching;

            struct V {
                Coord x, y;
                Vector *start, *end;
                operator bool() {
                    return start != nullptr && end != nullptr;
                }
                operator Vector() const { return {x, y}; }
            } prevvector = {0, 0, nullptr, nullptr};

            Cntr NFP;
            NFP.emplace_back(x(B[0]) + B.offsetx, y(B[0]) + B.offsety);

            auto referencex = x(B[0]) + B.offsetx;
            auto referencey = y(B[0]) + B.offsety;
            auto startx = referencex;
            auto starty = referencey;
            unsigned counter = 0;

            // sanity check, prevent infinite loop
            while(counter < 10*(A.size() + B.size())){
                touching.clear();

                // find touching vertices/edges
                for(i = 0; i < A.size(); i++){
                    long nexti = (i == A.size() - 1) ? 0 : i + 1;
                    for(j = 0; j < B.size(); j++){

                        long nextj = (j == B.size() - 1) ? 0 : j + 1;

                        if( _almostEqual(A[i].x, B[j].x+B.offsetx) &&
                            _almostEqual(A[i].y, B[j].y+B.offsety))
                        {
                            touching.emplace_back(0, i, j);
                        }
                        else if( _onSegment(
                                    A[i], A[nexti],
                                    { B[j].x+B.offsetx, B[j].y + B.offsety}) )
                        {
                            touching.emplace_back(1, nexti, j);
                        }
                        else if( _onSegment(
                                {B[j].x+B.offsetx, B[j].y + B.offsety},
                                {B[nextj].x+B.offsetx, B[nextj].y + B.offsety},
                                A[i]) )
                        {
                            touching.emplace_back(2, i, nextj);
                        }
                    }
                }

                // generate translation vectors from touching vertices/edges
                std::vector<V> vectors;
                for(i=0; i < touching.size(); i++){
                    auto& vertexA = A[touching[i].A];
                    vertexA.marked = true;

                    // adjacent A vertices
                    auto prevAindex = touching[i].A - 1;
                    auto nextAindex = touching[i].A + 1;

                    prevAindex = (prevAindex < 0) ? A.size() - 1 : prevAindex; // loop
                    nextAindex = (nextAindex >= A.size()) ? 0 : nextAindex; // loop

                    auto& prevA = A[prevAindex];
                    auto& nextA = A[nextAindex];

                    // adjacent B vertices
                    auto& vertexB = B[touching[i].B];

                    auto prevBindex = touching[i].B-1;
                    auto nextBindex = touching[i].B+1;

                    prevBindex = (prevBindex < 0) ? B.size() - 1 : prevBindex; // loop
                    nextBindex = (nextBindex >= B.size()) ? 0 : nextBindex; // loop

                    auto& prevB = B[prevBindex];
                    auto& nextB = B[nextBindex];

                    if(touching[i].type == 0){

                        V vA1 = {
                            prevA.x - vertexA.x,
                            prevA.y - vertexA.y,
                            &vertexA,
                            &prevA
                        };

                        V vA2 = {
                            nextA.x - vertexA.x,
                            nextA.y - vertexA.y,
                            &vertexA,
                            &nextA
                        };

                        // B vectors need to be inverted
                        V vB1 = {
                            vertexB.x - prevB.x,
                            vertexB.y - prevB.y,
                            &prevB,
                            &vertexB
                        };

                        V vB2 = {
                            vertexB.x - nextB.x,
                            vertexB.y - nextB.y,
                            &nextB,
                            &vertexB
                        };

                        vectors.emplace_back(vA1);
                        vectors.emplace_back(vA2);
                        vectors.emplace_back(vB1);
                        vectors.emplace_back(vB2);
                    }
                    else if(touching[i].type == 1){
                        vectors.emplace_back(V{
                            vertexA.x-(vertexB.x+B.offsetx),
                            vertexA.y-(vertexB.y+B.offsety),
                            &prevA,
                            &vertexA
                        });

                        vectors.emplace_back(V{
                            prevA.x-(vertexB.x+B.offsetx),
                            prevA.y-(vertexB.y+B.offsety),
                            &vertexA,
                            &prevA
                        });
                    }
                    else if(touching[i].type == 2){
                        vectors.emplace_back(V{
                            vertexA.x-(vertexB.x+B.offsetx),
                            vertexA.y-(vertexB.y+B.offsety),
                            &prevB,
                            &vertexB
                        });

                        vectors.emplace_back(V{
                            vertexA.x-(prevB.x+B.offsetx),
                            vertexA.y-(prevB.y+B.offsety),
                            &vertexB,
                            &prevB
                        });
                    }
                }

                // TODO: there should be a faster way to reject vectors that
                // will cause immediate intersection. For now just check them all

                V translate = {0, 0, nullptr, nullptr};
                double maxd = 0;

                for(i = 0; i < vectors.size(); i++) {
                    if(vectors[i].x == 0 && vectors[i].y == 0){
                        continue;
                    }

                    // if this vector points us back to where we came from, ignore it.
                    // ie cross product = 0, dot product < 0
                    if(prevvector && vectors[i].y * prevvector.y + vectors[i].x * prevvector.x < 0){

                        // compare magnitude with unit vectors
                        double vectorlength = sqrt(vectors[i].x*vectors[i].x+vectors[i].y*vectors[i].y);
                        Vector unitv = {Coord(vectors[i].x/vectorlength),
                                        Coord(vectors[i].y/vectorlength)};

                        double prevlength = sqrt(prevvector.x*prevvector.x+prevvector.y*prevvector.y);
                        Vector prevunit = { prevvector.x/prevlength, prevvector.y/prevlength};

                        // we need to scale down to unit vectors to normalize vector length. Could also just do a tan here
                        if(abs(unitv.y * prevunit.x - unitv.x * prevunit.y) < 0.0001){
                            continue;
                        }
                    }

                    V vi = vectors[i];
                    double d = polygonSlideDistance(A, B, vi, true);
                    double vecd2 = vectors[i].x*vectors[i].x + vectors[i].y*vectors[i].y;

                    if(isnan(d) || d*d > vecd2){
                        double vecd = sqrt(vectors[i].x*vectors[i].x + vectors[i].y*vectors[i].y);
                        d = vecd;
                    }

                    if(!isnan(d) && d > maxd){
                        maxd = d;
                        translate = vectors[i];
                    }
                }

                if(!translate || _almostEqual(maxd, 0))
                {
                    // didn't close the loop, something went wrong here
                    NFP.clear();
                    break;
                }

                translate.start->marked = true;
                translate.end->marked = true;

                prevvector = translate;

                // trim
                double vlength2 = translate.x*translate.x + translate.y*translate.y;
                if(maxd*maxd < vlength2 && !_almostEqual(maxd*maxd, vlength2)){
                    double scale = sqrt((maxd*maxd)/vlength2);
                    translate.x *= scale;
                    translate.y *= scale;
                }

                referencex += translate.x;
                referencey += translate.y;

                if(_almostEqual(referencex, startx) &&
                   _almostEqual(referencey, starty)) {
                    // we've made a full loop
                    break;
                }

                // if A and B start on a touching horizontal line,
                // the end point may not be the start point
                bool looped = false;
                if(NFP.size() > 0) {
                    for(i = 0; i < NFP.size() - 1; i++) {
                        if(_almostEqual(referencex, NFP[i].x) &&
                           _almostEqual(referencey, NFP[i].y)){
                            looped = true;
                        }
                    }
                }

                if(looped){
                    // we've made a full loop
                    break;
                }

                NFP.emplace_back(referencex, referencey);

                B.offsetx += translate.x;
                B.offsety += translate.y;

                counter++;
            }

            if(NFP.size() > 0){
                NFPlist.emplace_back(NFP);
            }

            if(!searchEdges){
                // only get outer NFP or first inner NFP
                break;
            }

            startpoint =
                    searchStartPoint(A, B, inside, NFPlist);

        }

        return NFPlist;
    }
};

template<class S> const double _alg<S>::TOL = std::pow(10, -9);

}
}

#endif // NFP_SVGNEST_HPP
