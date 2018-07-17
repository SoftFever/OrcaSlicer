#ifndef FILLER_HPP
#define FILLER_HPP

#include "selection_boilerplate.hpp"

namespace libnest2d { namespace strategies {

template<class RawShape>
class _FillerSelection: public SelectionBoilerplate<RawShape> {
    using Base = SelectionBoilerplate<RawShape>;
public:
    using typename Base::Item;
    using Config = int; //dummy

private:
    using Base::packed_bins_;
    using typename Base::ItemGroup;
    using Container = ItemGroup;
    Container store_;

public:

    void configure(const Config& /*config*/) { }

    template<class TPlacer, class TIterator,
             class TBin = typename PlacementStrategyLike<TPlacer>::BinType,
             class PConfig = typename PlacementStrategyLike<TPlacer>::Config>
    void packItems(TIterator first,
                   TIterator last,
                   TBin&& bin,
                   PConfig&& pconfig = PConfig())
    {

        store_.clear();
        auto total = last-first;
        store_.reserve(total);
        packed_bins_.emplace_back();

        auto makeProgress = [this, &total](
                PlacementStrategyLike<TPlacer>& placer)
        {
            packed_bins_.back() = placer.getItems();
#ifndef NDEBUG
            packed_bins_.back().insert(packed_bins_.back().end(),
                                       placer.getDebugItems().begin(),
                                       placer.getDebugItems().end());
#endif
            this->progress_(--total);
        };

        std::copy(first, last, std::back_inserter(store_));

        auto sortfunc = [](Item& i1, Item& i2) {
            return i1.area() > i2.area();
        };

        std::sort(store_.begin(), store_.end(), sortfunc);

//        Container a = {store_[0], store_[1], store_[4], store_[5] };
////        a.insert(a.end(), store_.end()-10, store_.end());
//        store_ = a;

        PlacementStrategyLike<TPlacer> placer(bin);
        placer.configure(pconfig);

        auto it = store_.begin();
        while(it != store_.end()) {
            if(!placer.pack(*it))  {
                if(packed_bins_.back().empty()) ++it;
//                makeProgress(placer);
                placer.clearItems();
                packed_bins_.emplace_back();
            } else {
                makeProgress(placer);
                ++it;
            }
        }

//        if(was_packed) {
//            packed_bins_.push_back(placer.getItems());
//        }
    }
};

}
}

#endif //BOTTOMLEFT_HPP
