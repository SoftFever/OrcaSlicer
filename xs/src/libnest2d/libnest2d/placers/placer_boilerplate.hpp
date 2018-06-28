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
    mutable bool farea_valid_ = false;
    mutable double farea_ = 0.0;
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

    inline PlacerBoilerplate(const BinType& bin, unsigned cap = 50): bin_(bin)
    {
        items_.reserve(cap);
    }

    inline const BinType& bin() const BP2D_NOEXCEPT { return bin_; }

    template<class TB> inline void bin(TB&& b) {
        bin_ = std::forward<BinType>(b);
    }

    inline void configure(const Config& config) BP2D_NOEXCEPT {
        config_ = config;
    }

    bool pack(Item& item) {
        auto&& r = static_cast<Subclass*>(this)->trypack(item);
        if(r) {
            items_.push_back(*(r.item_ptr_));
            farea_valid_ = false;
        }
        return r;
    }

    void accept(PackResult& r) {
        if(r) {
            r.item_ptr_->translation(r.move_);
            r.item_ptr_->rotation(r.rot_);
            items_.push_back(*(r.item_ptr_));
            farea_valid_ = false;
        }
    }

    void unpackLast() {
        items_.pop_back();
        farea_valid_ = false;
    }

    inline ItemGroup getItems() const { return items_; }

    inline void clearItems() {
        items_.clear();
        farea_valid_ = false;
#ifndef NDEBUG
        debug_items_.clear();
#endif
    }

    inline double filledArea() const {
        if(farea_valid_) return farea_;
        else {
            farea_ = .0;
            std::for_each(items_.begin(), items_.end(),
                          [this] (Item& item) {
                farea_ += item.area();
            });
            farea_valid_ = true;
        }

        return farea_;
    }

#ifndef NDEBUG
    std::vector<Item> debug_items_;
#endif

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
