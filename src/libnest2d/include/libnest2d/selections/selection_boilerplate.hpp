#ifndef SELECTION_BOILERPLATE_HPP
#define SELECTION_BOILERPLATE_HPP

#include <atomic>
#include <libnest2d/libnest2d.hpp>

namespace libnest2d { namespace selections {

template<class RawShape>
class SelectionBoilerplate {
public:
    using ShapeType = RawShape;
    using Item = _Item<RawShape>;
    using ItemGroup = _ItemGroup<RawShape>;
    using PackGroup = _PackGroup<RawShape>;

    inline const PackGroup& getResult() const {
        return packed_bins_;
    }

    inline void progressIndicator(ProgressFunction fn) { progress_ = fn; }

    inline void stopCondition(StopCondition cond) { stopcond_ = cond; }

    inline void clear() { packed_bins_.clear(); }

protected:

    PackGroup packed_bins_;
    ProgressFunction progress_ = [](unsigned){};
    StopCondition stopcond_ = [](){ return false; };
};

}
}

#endif // SELECTION_BOILERPLATE_HPP
