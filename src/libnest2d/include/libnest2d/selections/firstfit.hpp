#ifndef FIRSTFIT_HPP
#define FIRSTFIT_HPP

#include "selection_boilerplate.hpp"

namespace libnest2d { namespace selections {

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

        std::vector<Placer> placers;
        placers.reserve(last-first);
        
        std::for_each(first, last, [this](Item& itm) {
            if(itm.isFixed()) {
                if (itm.binId() < 0) itm.binId(0);
                auto binidx = size_t(itm.binId());
                
                while(packed_bins_.size() <= binidx)
                    packed_bins_.emplace_back();
                
                packed_bins_[binidx].emplace_back(itm);
            } else {
                store_.emplace_back(itm);
            }
        });

        // If the packed_items array is not empty we have to create as many
        // placers as there are elements in packed bins and preload each item
        // into the appropriate placer
        for(ItemGroup& ig : packed_bins_) {
            placers.emplace_back(bin);
            placers.back().configure(pconfig);
            placers.back().preload(ig);
        }
        
        auto sortfunc = [](Item& i1, Item& i2) {
            int p1 = i1.priority(), p2 = i2.priority();
            return p1 == p2 ? i1.area() > i2.area() : p1 > p2;
        };

        std::sort(store_.begin(), store_.end(), sortfunc);

        auto total = last-first;
        auto makeProgress = [this, &total](Placer& placer, size_t idx) {
            packed_bins_[idx] = placer.getItems();
            this->progress_(static_cast<unsigned>(--total));
        };

        auto& cancelled = this->stopcond_;
        
        this->template remove_unpackable_items<Placer>(store_, bin, pconfig);

        auto it = store_.begin();

        while(it != store_.end() && !cancelled()) {
            bool was_packed = false;
            size_t j = 0;
            while(!was_packed && !cancelled()) {
                for(; j < placers.size() && !was_packed && !cancelled(); j++) {
                    if((was_packed = placers[j].pack(*it, rem(it, store_) ))) {
                        it->get().binId(int(j));
                        makeProgress(placers[j], j);
                    }
                }

                if(!was_packed) {
                    placers.emplace_back(bin);
                    placers.back().configure(pconfig);
                    packed_bins_.emplace_back();
                    j = placers.size() - 1;
                }
            }
            ++it;
        }
    }

};

}
}

#endif // FIRSTFIT_HPP
