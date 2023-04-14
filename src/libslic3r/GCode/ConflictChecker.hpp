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
    Line _line;
    int  _id;
    int  _role;

    LineWithID(const Line &line, int id, int role) : _line(line), _id(id), _role(role) {}
};

using LineWithIDs = std::vector<LineWithID>;

class LinesBucket
{
private:
    double   _curHeight  = 0.0;
    unsigned _curPileIdx = 0;

    std::vector<ExtrusionPaths> _piles;
    int                         _id;
    Point                       _offset;

public:
    LinesBucket(std::vector<ExtrusionPaths> &&paths, int id, Point offset) : _piles(paths), _id(id), _offset(offset) {}
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
                check_polyline.translate(_offset);
                Lines tmpLines = check_polyline.lines();
                for (const Line &line : tmpLines) { lines.emplace_back(line, _id, path.role()); }
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
    std::map<int, const void *>                                                        _idToObjsPtr;
    std::map<const void *, int>                                                        _objsPtrToId;

public:
    void        emplace_back_bucket(std::vector<ExtrusionPaths> &&paths, const void *objPtr, Point offset);
    bool        valid() const { return _pq.empty() == false; }
    const void *idToObjsPtr(int id)
    {
        if (_idToObjsPtr.find(id) != _idToObjsPtr.end())
            return _idToObjsPtr[id];
        else
            return nullptr;
    }
    double      removeLowests();
    LineWithIDs getCurLines() const;
};

void getExtrusionPathsFromEntity(const ExtrusionEntityCollection *entity, ExtrusionPaths &paths);

ExtrusionPaths getExtrusionPathsFromLayer(LayerRegionPtrs layerRegionPtrs);

ExtrusionPaths getExtrusionPathsFromSupportLayer(SupportLayer *supportLayer);

std::pair<std::vector<ExtrusionPaths>, std::vector<ExtrusionPaths>> getAllLayersExtrusionPathsFromObject(PrintObject *obj);

struct ConflictComputeResult
{
    int _obj1;
    int _obj2;

    ConflictComputeResult(int o1, int o2) : _obj1(o1), _obj2(o2) {}
    ConflictComputeResult() = default;
};

using ConflictComputeOpt = std::optional<ConflictComputeResult>;

using ConflictObjName = std::optional<std::pair<std::string, std::string>>;

struct ConflictChecker
{
    static ConflictResultOpt  find_inter_of_lines_in_diff_objs(PrintObjectPtrs objs, std::optional<const FakeWipeTower *> wtdptr);
    static ConflictComputeOpt find_inter_of_lines(const LineWithIDs &lines);
    static ConflictComputeOpt line_intersect(const LineWithID &l1, const LineWithID &l2);
};

} // namespace Slic3r

#endif
