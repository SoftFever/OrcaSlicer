#ifndef FIRSTFIT_HPP
#define FIRSTFIT_HPP

#include "../libnest2d.hpp"
#include "selection_boilerplate.hpp"

namespace libnest2d { namespace strategies {

template<class RawShape>
class _FirstFitSelection: public SelectionBoilerplate<RawShape> {
    using Base = SelectionBoilerplate<RawShape>;
public:
    using typename Base::Item;
    using Config = int; //dummy

private:
    using Base::packed_bins_;
    using typename Base::ItemGroup;
    using Container = ItemGroup;//typename std::vector<_Item<RawShape>>;

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

        using Placer = PlacementStrategyLike<TPlacer>;

        store_.clear();
        store_.reserve(last-first);
        packed_bins_.clear();

        std::vector<Placer> placers;

        std::copy(first, last, std::back_inserter(store_));

        auto sortfunc = [](Item& i1, Item& i2) {
            return i1.area() > i2.area();
        };

        std::sort(store_.begin(), store_.end(), sortfunc);

        for(auto& item : store_ ) {
            bool was_packed = false;
            while(!was_packed) {

                for(size_t j = 0; j < placers.size() && !was_packed; j++)
                    was_packed = placers[j].pack(item);

                if(!was_packed) {
                    placers.emplace_back(bin);
                    placers.back().configure(pconfig);
                }
            }
        }

        std::for_each(placers.begin(), placers.end(),
                      [this](Placer& placer){
            packed_bins_.push_back(placer.getItems());
        });
    }

};

}
}

#endif // FIRSTFIT_HPP
