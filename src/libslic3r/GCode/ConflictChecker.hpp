#ifndef slic3r_ConflictChecker_hpp_
#define slic3r_ConflictChecker_hpp_

#include "../Utils.hpp"
#include "../Model.hpp"
#include "../Print.hpp"
#include "../Layer.hpp"

#include <queue>
#include <vector>
#include <optional>

namespace Slic3r {

struct LineWithID
{
    Line         _line;
    PrintObject *_objPtr;
    int          _role;

    LineWithID(const Line &line, PrintObject *objPtr, int role) : _line(line), _objPtr(objPtr), _role(role) {}
};

using LineWithIDs = std::vector<LineWithID>;

class LinesBucket
{
private:
    double   _curHeight  = 0.0;
    unsigned _curPileIdx = 0;

    std::vector<ExtrusionPaths> _piles;
    PrintObject *               _objPtr;
    Point                       _offset;

public:
    LinesBucket(std::vector<ExtrusionPaths> &&paths, PrintObject *objPtr, Point offset) : _piles(paths), _objPtr(objPtr), _offset(offset) {}
    LinesBucket(LinesBucket &&) = default;

    bool valid() const { return _curPileIdx < _piles.size(); }
    void raise()
    {
        if (valid()) {
            if (_piles[_curPileIdx].empty() == false) { _curHeight += _piles[_curPileIdx].front().height; }
            _curPileIdx++;
        }
    }
    double      curHeight() const { return _curHeight; }
    LineWithIDs curLines() const
    {
        LineWithIDs lines;
        for (const ExtrusionPath &path : _piles[_curPileIdx]) {
            if (path.is_force_no_extrusion() == false) {
                Polyline check_polyline = path.polyline;
                if (path.role() != ExtrusionRole::erBrim) { check_polyline.translate(_offset); }
                Lines tmpLines = check_polyline.lines();
                for (const Line &line : tmpLines) { lines.emplace_back(line, _objPtr, path.role()); }
            }
        }
        return lines;
    }

    friend bool operator>(const LinesBucket &left, const LinesBucket &right) { return left._curHeight > right._curHeight; }
    friend bool operator<(const LinesBucket &left, const LinesBucket &right) { return left._curHeight < right._curHeight; }
    friend bool operator==(const LinesBucket &left, const LinesBucket &right) { return left._curHeight == right._curHeight; }
};

struct LinesBucketPtrComp
{
    bool operator()(const LinesBucket *left, const LinesBucket *right) { return *left > *right; }
};

class LinesBucketQueue
{
private:
    std::vector<LinesBucket>                                                           _buckets;
    std::priority_queue<LinesBucket *, std::vector<LinesBucket *>, LinesBucketPtrComp> _pq;

public:
    void emplace_back_bucket(std::vector<ExtrusionPaths> &&paths, PrintObject *objPtr, Point offset);
    bool valid() const { return _pq.empty() == false; }

    void        removeLowests();
    LineWithIDs getCurLines() const;
};

void getExtrusionPathsFromEntity(const ExtrusionEntityCollection *entity, ExtrusionPaths &paths);

ExtrusionPaths getExtrusionPathsFromLayer(LayerRegionPtrs layerRegionPtrs);

ExtrusionPaths getExtrusionPathsFromSupportLayer(SupportLayer *supportLayer);

std::pair<std::vector<ExtrusionPaths>, std::vector<ExtrusionPaths>> getAllLayersExtrusionPathsFromObject(PrintObject *obj);

struct ConflictResult
{
    PrintObject *_obj1;
    PrintObject *_obj2;
    ConflictResult(PrintObject *obj1, PrintObject *obj2) : _obj1(obj1), _obj2(obj2) {}
    ConflictResult() = default;
};

static_assert(std::is_trivial<ConflictResult>::value, "atomic value requires to be trival.");

using ConflictRet = std::optional<ConflictResult>;

struct ConflictChecker
{
    static ConflictRet find_inter_of_lines_in_diff_objs(PrintObjectPtrs objs);
    static ConflictRet find_inter_of_lines(const LineWithIDs &lines);
    static ConflictRet line_intersect(const LineWithID &l1, const LineWithID &l2);
};

} // namespace Slic3r

#endif
