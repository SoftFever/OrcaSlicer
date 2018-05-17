#ifndef PLACER_BOILERPLATE_HPP
#define PLACER_BOILERPLATE_HPP

#include "../libnest2d.hpp"

namespace libnest2d { namespace strategies {

struct EmptyConfig {};

template<class Subclass, class RawShape, class TBin,
         class Cfg = EmptyConfig,
         class Store = std::vector<std::reference_wrapper<_Item<RawShape>>>
         >
class PlacerBoilerplate {
public:
    using Item = _Item<RawShape>;
    using Vertex = TPoint<RawShape>;
    using Segment = _Segment<Vertex>;
    using BinType = TBin;
    using Coord = TCoord<Vertex>;
    using Unit = Coord;
    using Config = Cfg;
    using Container = Store;

    class PackResult {
        Item *item_ptr_;
        Vertex move_;
        Radians rot_;
        friend class PlacerBoilerplate;
        friend Subclass;
        PackResult(Item& item):
            item_ptr_(&item),
            move_(item.translation()),
            rot_(item.rotation()) {}
        PackResult(): item_ptr_(nullptr) {}
    public:
        operator bool() { return item_ptr_ != nullptr; }
    };

    using ItemGroup = const Container&;

    inline PlacerBoilerplate(const BinType& bin): bin_(bin) {}

    inline const BinType& bin() const BP2D_NOEXCEPT { return bin_; }

    template<class TB> inline void bin(TB&& b) {
        bin_ = std::forward<BinType>(b);
    }

    inline void configure(const Config& config) BP2D_NOEXCEPT {
        config_ = config;
    }

    bool pack(Item& item) {
        auto&& r = static_cast<Subclass*>(this)->trypack(item);
        if(r) items_.push_back(*(r.item_ptr_));
        return r;
    }

    void accept(PackResult& r) {
        if(r) {
            r.item_ptr_->translation(r.move_);
            r.item_ptr_->rotation(r.rot_);
            items_.push_back(*(r.item_ptr_));
        }
    }

    void unpackLast() { items_.pop_back(); }

    inline ItemGroup getItems() { return items_; }

    inline void clearItems() { items_.clear(); }

protected:

    BinType bin_;
    Container items_;
    Cfg config_;

};


#define DECLARE_PLACER(Base) \
using Base::bin_;                 \
using Base::items_;               \
using Base::config_;              \
public:                           \
using typename Base::Item;        \
using typename Base::BinType;     \
using typename Base::Config;      \
using typename Base::Vertex;      \
using typename Base::Segment;     \
using typename Base::PackResult;  \
using typename Base::Coord;       \
using typename Base::Unit;        \
using typename Base::Container;   \
private:

}
}

#endif // PLACER_BOILERPLATE_HPP
