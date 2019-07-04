#ifndef LIBNEST2D_HPP
#define LIBNEST2D_HPP

#include <memory>
#include <vector>
#include <map>
#include <array>
#include <algorithm>
#include <functional>

#include <libnest2d/geometry_traits.hpp>

namespace libnest2d {

/**
 * \brief An item to be placed on a bin.
 *
 * It holds a copy of the original shape object but supports move construction
 * from the shape objects if its an rvalue reference. This way we can construct
 * the items without the cost of copying a potentially large amount of input.
 *
 * The results of some calculations are cached for maintaining fast run times.
 * For this reason, memory demands are much higher but this should pay off.
 */
template<class RawShape>
class _Item {
    using Coord = TCoord<TPoint<RawShape>>;
    using Vertex = TPoint<RawShape>;
    using Box = _Box<Vertex>;

    using VertexConstIterator = typename TContour<RawShape>::const_iterator;

    // The original shape that gets encapsulated.
    RawShape sh_;

    // Transformation data
    Vertex translation_;
    Radians rotation_;
    Coord inflation_;

    // Info about whether the transformations will have to take place
    // This is needed because if floating point is used, it is hard to say
    // that a zero angle is not a rotation because of testing for equality.
    bool has_rotation_ = false, has_translation_ = false, has_inflation_ = false;

    // For caching the calculations as they can get pretty expensive.
    mutable RawShape tr_cache_;
    mutable bool tr_cache_valid_ = false;
    mutable double area_cache_ = 0;
    mutable bool area_cache_valid_ = false;
    mutable RawShape inflate_cache_;
    mutable bool inflate_cache_valid_ = false;

    enum class Convexity: char {
        UNCHECKED,
        C_TRUE,
        C_FALSE
    };

    mutable Convexity convexity_ = Convexity::UNCHECKED;
    mutable VertexConstIterator rmt_;    // rightmost top vertex
    mutable VertexConstIterator lmb_;    // leftmost bottom vertex
    mutable bool rmt_valid_ = false, lmb_valid_ = false;
    mutable struct BBCache {
        Box bb; bool valid;
        BBCache(): valid(false) {}
    } bb_cache_;
    
    static const size_t ID_UNSET = size_t(-1);
    
    size_t id_{ID_UNSET};
    bool fixed_{false};

public:

    /// The type of the shape which was handed over as the template argument.
    using ShapeType = RawShape;

    /**
     * \brief Iterator type for the outer vertices.
     *
     * Only const iterators can be used. The _Item type is not intended to
     * modify the carried shapes from the outside. The main purpose of this type
     * is to cache the calculation results from the various operators it
     * supports. Giving out a non const iterator would make it impossible to
     * perform correct cache invalidation.
     */
    using Iterator = VertexConstIterator;

    /**
     * @brief Get the orientation of the polygon.
     *
     * The orientation have to be specified as a specialization of the
     * OrientationType struct which has a Value constant.
     *
     * @return The orientation type identifier for the _Item type.
     */
    static BP2D_CONSTEXPR Orientation orientation() {
        return OrientationType<RawShape>::Value;
    }

    /**
     * @brief Constructing an _Item form an existing raw shape. The shape will
     * be copied into the _Item object.
     * @param sh The original shape object.
     */
    explicit inline _Item(const RawShape& sh): sh_(sh) {}

    /**
     * @brief Construction of an item by moving the content of the raw shape,
     * assuming that it supports move semantics.
     * @param sh The original shape object.
     */
    explicit inline _Item(RawShape&& sh): sh_(std::move(sh)) {}

    /**
     * @brief Create an item from an initializer list.
     * @param il The initializer list of vertices.
     */
    inline _Item(const std::initializer_list< Vertex >& il):
        sh_(sl::create<RawShape>(il)) {}

    inline _Item(const TContour<RawShape>& contour,
                 const THolesContainer<RawShape>& holes = {}):
        sh_(sl::create<RawShape>(contour, holes)) {}

    inline _Item(TContour<RawShape>&& contour,
                 THolesContainer<RawShape>&& holes):
        sh_(sl::create<RawShape>(std::move(contour), std::move(holes))) {}

//    template<class... Args>
//    _Item(std::function<void(const _Item&, unsigned)> applyfn, Args &&... args):
//        _Item(std::forward<Args>(args)...)
//    {
//        applyfn_ = std::move(applyfn);
//    }

    // Call the apply callback set in constructor. Within the callback, the
    // original caller can apply the stored transformation to the original
    // objects inteded for nesting. It might not be the shape handed over
    // to _Item (e.g. arranging 3D shapes based on 2D silhouette or the
    // client uses a simplified or processed polygon for nesting)
    // This callback, if present, will be called for each item after the nesting
    // is finished.
//    inline void callApplyFunction(unsigned binidx) const
//    {
//        if (applyfn_) applyfn_(*this, binidx);
//    }
    
    inline bool isFixed() const noexcept { return fixed_; }
    inline void markAsFixed(bool fixed = true) { fixed_ = fixed; }
    inline void id(size_t idx) { id_ = idx; }
    inline long id() const noexcept { return id_; }

    /**
     * @brief Convert the polygon to string representation. The format depends
     * on the implementation of the polygon.
     * @return
     */
    inline std::string toString() const
    {
        return sl::toString(sh_);
    }

    /// Iterator tho the first contour vertex in the polygon.
    inline Iterator begin() const
    {
        return sl::cbegin(sh_);
    }

    /// Alias to begin()
    inline Iterator cbegin() const
    {
        return sl::cbegin(sh_);
    }

    /// Iterator to the last contour vertex.
    inline Iterator end() const
    {
        return sl::cend(sh_);
    }

    /// Alias to end()
    inline Iterator cend() const
    {
        return sl::cend(sh_);
    }

    /**
     * @brief Get a copy of an outer vertex within the carried shape.
     *
     * Note that the vertex considered here is taken from the original shape
     * that this item is constructed from. This means that no transformation is
     * applied to the shape in this call.
     *
     * @param idx The index of the requested vertex.
     * @return A copy of the requested vertex.
     */
    inline Vertex vertex(unsigned long idx) const
    {
        return sl::vertex(sh_, idx);
    }

    /**
     * @brief Modify a vertex.
     *
     * Note that this method will invalidate every cached calculation result
     * including polygon offset and transformations.
     *
     * @param idx The index of the requested vertex.
     * @param v The new vertex data.
     */
    inline void setVertex(unsigned long idx, const Vertex& v )
    {
        invalidateCache();
        sl::vertex(sh_, idx) = v;
    }

    /**
     * @brief Calculate the shape area.
     *
     * The method returns absolute value and does not reflect polygon
     * orientation. The result is cached, subsequent calls will have very little
     * cost.
     * @return The shape area in floating point double precision.
     */
    inline double area() const {
        double ret ;
        if(area_cache_valid_) ret = area_cache_;
        else {
            ret = sl::area(infaltedShape());
            area_cache_ = ret;
            area_cache_valid_ = true;
        }
        return ret;
    }

    inline bool isContourConvex() const {
        bool ret = false;

        switch(convexity_) {
        case Convexity::UNCHECKED:
            ret = sl::isConvex(sl::contour(transformedShape()));
            convexity_ = ret? Convexity::C_TRUE : Convexity::C_FALSE;
            break;
        case Convexity::C_TRUE: ret = true; break;
        case Convexity::C_FALSE:;
        }

        return ret;
    }

    inline bool isHoleConvex(unsigned /*holeidx*/) const {
        return false;
    }

    inline bool areHolesConvex() const {
        return false;
    }

    /// The number of the outer ring vertices.
    inline size_t vertexCount() const {
        return sl::contourVertexCount(sh_);
    }

    inline size_t holeCount() const {
        return sl::holeCount(sh_);
    }

    /**
     * @brief isPointInside
     * @param p
     * @return
     */
    inline bool isInside(const Vertex& p) const
    {
        return sl::isInside(p, transformedShape());
    }

    inline bool isInside(const _Item& sh) const
    {
        return sl::isInside(transformedShape(), sh.transformedShape());
    }

    inline bool isInside(const RawShape& sh) const
    {
        return sl::isInside(transformedShape(), sh);
    }

    inline bool isInside(const _Box<TPoint<RawShape>>& box) const;
    inline bool isInside(const _Circle<TPoint<RawShape>>& box) const;

    inline void translate(const Vertex& d) BP2D_NOEXCEPT
    {
        translation(translation() + d);
    }

    inline void rotate(const Radians& rads) BP2D_NOEXCEPT
    {
        rotation(rotation() + rads);
    }
    
    inline void inflation(Coord distance) BP2D_NOEXCEPT
    {
        inflation_ = distance;
        has_inflation_ = true;
        invalidateCache();
    }
    
    inline Coord inflation() const BP2D_NOEXCEPT {
        return inflation_;
    }
    
    inline void inflate(Coord distance) BP2D_NOEXCEPT
    {
        inflation(inflation() + distance);
    }

    inline Radians rotation() const BP2D_NOEXCEPT
    {
        return rotation_;
    }

    inline TPoint<RawShape> translation() const BP2D_NOEXCEPT
    {
        return translation_;
    }

    inline void rotation(Radians rot) BP2D_NOEXCEPT
    {
        if(rotation_ != rot) {
            rotation_ = rot; has_rotation_ = true; tr_cache_valid_ = false;
            rmt_valid_ = false; lmb_valid_ = false;
            bb_cache_.valid = false;
        }
    }

    inline void translation(const TPoint<RawShape>& tr) BP2D_NOEXCEPT
    {
        if(translation_ != tr) {
            translation_ = tr; has_translation_ = true; tr_cache_valid_ = false;
            //bb_cache_.valid = false;
        }
    }

    inline const RawShape& transformedShape() const
    {
        if(tr_cache_valid_) return tr_cache_;

        RawShape cpy = infaltedShape();
        if(has_rotation_) sl::rotate(cpy, rotation_);
        if(has_translation_) sl::translate(cpy, translation_);
        tr_cache_ = cpy; tr_cache_valid_ = true;
        rmt_valid_ = false; lmb_valid_ = false;

        return tr_cache_;
    }

    inline operator RawShape() const
    {
        return transformedShape();
    }

    inline const RawShape& rawShape() const BP2D_NOEXCEPT
    {
        return sh_;
    }

    inline void resetTransformation() BP2D_NOEXCEPT
    {
        has_translation_ = false; has_rotation_ = false; has_inflation_ = false;
        invalidateCache();
    }

    inline Box boundingBox() const {
        if(!bb_cache_.valid) {
            if(!has_rotation_)
                bb_cache_.bb = sl::boundingBox(infaltedShape());
            else {
                // TODO make sure this works
                auto rotsh = infaltedShape();
                sl::rotate(rotsh, rotation_);
                bb_cache_.bb = sl::boundingBox(rotsh);
            }
            bb_cache_.valid = true;
        }

        auto &bb = bb_cache_.bb; auto &tr = translation_;
        return {bb.minCorner() + tr, bb.maxCorner() + tr };
    }

    inline Vertex referenceVertex() const {
        return rightmostTopVertex();
    }

    inline Vertex rightmostTopVertex() const {
        if(!rmt_valid_ || !tr_cache_valid_) {  // find max x and max y vertex
            auto& tsh = transformedShape();
            rmt_ = std::max_element(sl::cbegin(tsh), sl::cend(tsh), vsort);
            rmt_valid_ = true;
        }
        return *rmt_;
    }

    inline Vertex leftmostBottomVertex() const {
        if(!lmb_valid_ || !tr_cache_valid_) {  // find min x and min y vertex
            auto& tsh = transformedShape();
            lmb_ = std::min_element(sl::cbegin(tsh), sl::cend(tsh), vsort);
            lmb_valid_ = true;
        }
        return *lmb_;
    }

    //Static methods:

    inline static bool intersects(const _Item& sh1, const _Item& sh2)
    {
        return sl::intersects(sh1.transformedShape(),
                                     sh2.transformedShape());
    }

    inline static bool touches(const _Item& sh1, const _Item& sh2)
    {
        return sl::touches(sh1.transformedShape(),
                                  sh2.transformedShape());
    }

private:

    inline const RawShape& infaltedShape() const {
        if(has_inflation_ ) {
            if(inflate_cache_valid_) return inflate_cache_;

            inflate_cache_ = sh_;
            sl::offset(inflate_cache_, inflation_);
            inflate_cache_valid_ = true;
            return inflate_cache_;
        }
        return sh_;
    }

    inline void invalidateCache() const BP2D_NOEXCEPT
    {
        tr_cache_valid_ = false;
        lmb_valid_ = false; rmt_valid_ = false;
        area_cache_valid_ = false;
        inflate_cache_valid_ = false;
        bb_cache_.valid = false;
        convexity_ = Convexity::UNCHECKED;
    }

    static inline bool vsort(const Vertex& v1, const Vertex& v2)
    {
        TCompute<Vertex> x1 = getX(v1), x2 = getX(v2);
        TCompute<Vertex> y1 = getY(v1), y2 = getY(v2);
        return y1 == y2 ? x1 < x2 : y1 < y2;
    }
};

/**
 * \brief Subclass of _Item for regular rectangle items.
 */
template<class RawShape>
class _Rectangle: public _Item<RawShape> {
    using _Item<RawShape>::vertex;
    using TO = Orientation;
public:

    using Unit = TCoord<TPoint<RawShape>>;

    template<TO o = OrientationType<RawShape>::Value>
    inline _Rectangle(Unit width, Unit height,
                      // disable this ctor if o != CLOCKWISE
                      enable_if_t< o == TO::CLOCKWISE, int> = 0 ):
        _Item<RawShape>( sl::create<RawShape>( {
                                                        {0, 0},
                                                        {0, height},
                                                        {width, height},
                                                        {width, 0},
                                                        {0, 0}
                                                      } ))
    {
    }

    template<TO o = OrientationType<RawShape>::Value>
    inline _Rectangle(Unit width, Unit height,
                      // disable this ctor if o != COUNTER_CLOCKWISE
                      enable_if_t< o == TO::COUNTER_CLOCKWISE, int> = 0 ):
        _Item<RawShape>( sl::create<RawShape>( {
                                                        {0, 0},
                                                        {width, 0},
                                                        {width, height},
                                                        {0, height},
                                                        {0, 0}
                                                      } ))
    {
    }

    inline Unit width() const BP2D_NOEXCEPT {
        return getX(vertex(2));
    }

    inline Unit height() const BP2D_NOEXCEPT {
        return getY(vertex(2));
    }
};

template<class RawShape>
inline bool _Item<RawShape>::isInside(const _Box<TPoint<RawShape>>& box) const {
    return sl::isInside(boundingBox(), box);
}

template<class RawShape> inline bool
_Item<RawShape>::isInside(const _Circle<TPoint<RawShape>>& circ) const {
    return sl::isInside(transformedShape(), circ);
}

template<class RawShape> using _ItemRef = std::reference_wrapper<_Item<RawShape>>;
template<class RawShape> using _ItemGroup = std::vector<_ItemRef<RawShape>>;

/**
 * \brief A list of packed item vectors. Each vector represents a bin.
 */
template<class RawShape>
using _PackGroup = std::vector<std::vector<_ItemRef<RawShape>>>;

template<class Iterator>
struct ConstItemRange {
    Iterator from;
    Iterator to;
    bool valid = false;

    ConstItemRange() = default;
    ConstItemRange(Iterator f, Iterator t): from(f), to(t), valid(true) {}
};

template<class Container>
inline ConstItemRange<typename Container::const_iterator>
rem(typename Container::const_iterator it, const Container& cont) {
    return {std::next(it), cont.end()};
}

/**
 * \brief A wrapper interface (trait) class for any placement strategy provider.
 *
 * If a client wants to use its own placement algorithm, all it has to do is to
 * specialize this class template and define all the ten methods it has. It can
 * use the strategies::PlacerBoilerplace class for creating a new placement
 * strategy where only the constructor and the trypack method has to be provided
 * and it will work out of the box.
 */
template<class PlacementStrategy>
class PlacementStrategyLike {
    PlacementStrategy impl_;
public:

    using RawShape = typename PlacementStrategy::ShapeType;

    /// The item type that the placer works with.
    using Item = _Item<RawShape>;

    /// The placer's config type. Should be a simple struct but can be anything.
    using Config = typename PlacementStrategy::Config;

    /**
     * \brief The type of the bin that the placer works with.
     *
     * Can be a box or an arbitrary shape or just a width or height without a
     * second dimension if an infinite bin is considered.
     */
    using BinType = typename PlacementStrategy::BinType;

    /**
     * \brief Pack result that can be used to accept or discard it. See trypack
     * method.
     */
    using PackResult = typename PlacementStrategy::PackResult;

    using ItemGroup = _ItemGroup<RawShape>;
    using DefaultIterator = typename ItemGroup::const_iterator;

    /**
     * @brief Constructor taking the bin and an optional configuration.
     * @param bin The bin object whose type is defined by the placement strategy.
     * @param config The configuration for the particular placer.
     */
    explicit PlacementStrategyLike(const BinType& bin,
                                   const Config& config = Config()):
        impl_(bin)
    {
        configure(config);
    }

    /**
     * @brief Provide a different configuration for the placer.
     *
     * Note that it depends on the particular placer implementation how it
     * reacts to config changes in the middle of a calculation.
     *
     * @param config The configuration object defined by the placement strategy.
     */
    inline void configure(const Config& config) { impl_.configure(config); }

    /**
     * Try to pack an item with a result object that contains the packing
     * information for later accepting it.
     *
     * \param item_store A container of items that are intended to be packed
     * later. Can be used by the placer to switch tactics. When it's knows that
     * many items will come a greedy strategy may not be the best.
     * \param from The iterator to the item from which the packing should start,
     * including the pointed item
     * \param count How many items should be packed. If the value is 1, than
     * just the item pointed to by "from" argument should be packed.
     */
    template<class Iter = DefaultIterator>
    inline PackResult trypack(
            Item& item,
            const ConstItemRange<Iter>& remaining = ConstItemRange<Iter>())
    {
        return impl_.trypack(item, remaining);
    }

    /**
     * @brief A method to accept a previously tried item (or items).
     *
     * If the pack result is a failure the method should ignore it.
     * @param r The result of a previous trypack call.
     */
    inline void accept(PackResult& r) { impl_.accept(r); }

    /**
     * @brief pack Try to pack and immediately accept it on success.
     *
     * A default implementation would be to call
     * { auto&& r = trypack(...); accept(r); return r; } but we should let the
     * implementor of the placement strategy to harvest any optimizations from
     * the absence of an intermediate step. The above version can still be used
     * in the implementation.
     *
     * @param item The item to pack.
     * @return Returns true if the item was packed or false if it could not be
     * packed.
     */
    template<class Range = ConstItemRange<DefaultIterator>>
    inline bool pack(
            Item& item,
            const Range& remaining = Range())
    {
        return impl_.pack(item, remaining);
    }

    /**
     * This method makes possible to "preload" some items into the placer. It
     * will not move these items but will consider them as already packed.
     */
    inline void preload(const ItemGroup& packeditems)
    {
        impl_.preload(packeditems);
    }

    /// Unpack the last element (remove it from the list of packed items).
    inline void unpackLast() { impl_.unpackLast(); }

    /// Get the bin object.
    inline const BinType& bin() const { return impl_.bin(); }

    /// Set a new bin object.
    inline void bin(const BinType& bin) { impl_.bin(bin); }

    /// Get the packed items.
    inline ItemGroup getItems() { return impl_.getItems(); }

    /// Clear the packed items so a new session can be started.
    inline void clearItems() { impl_.clearItems(); }

    inline double filledArea() const { return impl_.filledArea(); }

};

// The progress function will be called with the number of placed items
using ProgressFunction = std::function<void(unsigned)>;
using StopCondition = std::function<bool(void)>;

/**
 * A wrapper interface (trait) class for any selections strategy provider.
 */
template<class SelectionStrategy>
class SelectionStrategyLike {
    SelectionStrategy impl_;
public:
    using RawShape = typename SelectionStrategy::ShapeType;
    using Item = _Item<RawShape>;
    using PackGroup = _PackGroup<RawShape>;
    using Config = typename SelectionStrategy::Config;


    /**
     * @brief Provide a different configuration for the selection strategy.
     *
     * Note that it depends on the particular placer implementation how it
     * reacts to config changes in the middle of a calculation.
     *
     * @param config The configuration object defined by the selection strategy.
     */
    inline void configure(const Config& config) {
        impl_.configure(config);
    }

    /**
     * @brief A function callback which should be called whenever an item or
     * a group of items where successfully packed.
     * @param fn A function callback object taking one unsigned integer as the
     * number of the remaining items to pack.
     */
    void progressIndicator(ProgressFunction fn) { impl_.progressIndicator(fn); }

    void stopCondition(StopCondition cond) { impl_.stopCondition(cond); }

    /**
     * \brief A method to start the calculation on the input sequence.
     *
     * \tparam TPlacer The only mandatory template parameter is the type of
     * placer compatible with the PlacementStrategyLike interface.
     *
     * \param first, last The first and last iterator if the input sequence. It
     * can be only an iterator of a type convertible to Item.
     * \param bin. The shape of the bin. It has to be supported by the placement
     * strategy.
     * \param An optional config object for the placer.
     */
    template<class TPlacer, class TIterator,
             class TBin = typename PlacementStrategyLike<TPlacer>::BinType,
             class PConfig = typename PlacementStrategyLike<TPlacer>::Config>
    inline void packItems(
            TIterator first,
            TIterator last,
            TBin&& bin,
            PConfig&& config = PConfig() )
    {
        impl_.template packItems<TPlacer>(first, last,
                                 std::forward<TBin>(bin),
                                 std::forward<PConfig>(config));
    }

    /**
     * @brief Get the items for a particular bin.
     * @param binIndex The index of the requested bin.
     * @return Returns a list of all items packed into the requested bin.
     */
    inline const PackGroup& getResult() const {
        return impl_.getResult();
    }

    /**
     * @brief Loading a group of already packed bins. It is best to use a result
     * from a previous packing. The algorithm will consider this input as if the
     * objects are already packed and not move them. If any of these items are
     * outside the bin, it is up to the placer algorithm what will happen.
     * Packing additional items can fail for the bottom-left and nfp placers.
     * @param pckgrp A packgroup which is a vector of item vectors. Each item
     * vector corresponds to a packed bin.
     */
    inline void preload(const PackGroup& pckgrp) { impl_.preload(pckgrp); }

    void clear() { impl_.clear(); }
};

using BinIdx = unsigned;
template<class S, class Key = size_t> using _NestResult =
    std::vector<
        std::tuple<Key,          // Identifier of the original shape
                   TPoint<S>,    // Translation calculated by nesting
                   Radians,      // Rotation calculated by nesting
                   BinIdx>       // Logical bin index, first is zero
        >;

template<class T> struct Indexed {
    using ShapeType = T;
    static T& get(T& obj) { return obj; }
};

template<class K, class S> struct Indexed<std::pair<K, S>> {
    using ShapeType = S;
    static S& get(std::pair<K, S>& obj) { return obj.second; }
};

/**
 * The Arranger is the front-end class for the libnest2d library. It takes the
 * input items and outputs the items with the proper transformations to be
 * inside the provided bin.
 */
template<class PlacementStrategy, class SelectionStrategy >
class Nester {
    using TSel = SelectionStrategyLike<SelectionStrategy>;
    TSel selector_;
public:
    using Item = typename PlacementStrategy::Item;
    using ShapeType = typename Item::ShapeType;
    using ItemRef = std::reference_wrapper<Item>;
    using TPlacer = PlacementStrategyLike<PlacementStrategy>;
    using BinType = typename TPlacer::BinType;
    using PlacementConfig = typename TPlacer::Config;
    using SelectionConfig = typename TSel::Config;
    using Coord = TCoord<TPoint<typename Item::ShapeType>>;
    using PackGroup = _PackGroup<typename Item::ShapeType>;
    using ResultType = PackGroup;
    template<class K> using NestResult = _NestResult<ShapeType, K>;

private:
    BinType bin_;
    PlacementConfig pconfig_;
    Coord min_obj_distance_;

    using SItem =  typename SelectionStrategy::Item;
    using TPItem = remove_cvref_t<Item>;
    using TSItem = remove_cvref_t<SItem>;

    std::vector<TPItem> item_cache_;
    StopCondition stopfn_;

public:

    /**
     * \brief Constructor taking the bin as the only mandatory parameter.
     *
     * \param bin The bin shape that will be used by the placers. The type
     * of the bin should be one that is supported by the placer type.
     */
    template<class TBinType = BinType,
             class PConf = PlacementConfig,
             class SConf = SelectionConfig>
    Nester( TBinType&& bin,
              Coord min_obj_distance = 0,
              const PConf& pconfig = PConf(),
              const SConf& sconfig = SConf()):
        bin_(std::forward<TBinType>(bin)),
        pconfig_(pconfig),
        min_obj_distance_(min_obj_distance)
    {
        static_assert( std::is_same<TPItem, TSItem>::value,
                       "Incompatible placement and selection strategy!");

        selector_.configure(sconfig);
    }

    void configure(const PlacementConfig& pconf) { pconfig_ = pconf; }
    void configure(const SelectionConfig& sconf) { selector_.configure(sconf); }
    void configure(const PlacementConfig& pconf, const SelectionConfig& sconf)
    {
        pconfig_ = pconf;
        selector_.configure(sconf);
    }
    void configure(const SelectionConfig& sconf, const PlacementConfig& pconf)
    {
        pconfig_ = pconf;
        selector_.configure(sconf);
    }

    /**
     * \brief Arrange an input sequence and return a PackGroup object with
     * the packed groups corresponding to the bins.
     *
     * The number of groups in the pack group is the number of bins opened by
     * the selection algorithm.
     */
    template<class It, class Key = size_t>
    inline const NestResult<Key> execute(It from, It to,
                                         std::function<Key(It)> keyfn = nullptr)
    {
        if (!keyfn) keyfn = [to](It it) { return to - it; };
        return _execute(from, to, keyfn);
    }

    /// Set a progress indicator function object for the selector.
    inline Nester& progressIndicator(ProgressFunction func)
    {
        selector_.progressIndicator(func); return *this;
    }

    /// Set a predicate to tell when to abort nesting.
    inline Nester& stopCondition(StopCondition fn)
    {
        stopfn_ = fn; selector_.stopCondition(fn); return *this;
    }

    inline const PackGroup& lastResult() const
    {
        return selector_.getResult();
    }

private:
    
    template<class It> using TVal = remove_cvref_t<typename It::value_type>;
    
    template<class It, class Out>
    using ConvertibleOnly =
        enable_if_t< std::is_convertible<TVal<It>, TPItem>::value, void>;
    
    template<class It, class Out>
    using NotConvertibleOnly =
        enable_if_t< ! std::is_convertible<TVal<It>, TPItem>::value, void>;
    
    // This function will be used only if the iterators are pointing to
    // a type compatible with the libnets2d::_Item template.
    // This way we can use references to input elements as they will
    // have to exist for the lifetime of this call.
    template<class It, class Key>
    inline ConvertibleOnly<It, const NestResult<Key>> _execute(
        It from, It to, std::function<Key(It)> keyfn)
    {
        {
            auto it = from; size_t id = 0;
            while(it != to) 
                if (it->id() == Item::ID_UNSET) (it++)->id(id++);
                else { id = it->id() + 1; ++it; }
        }
        
        NestResult<Key> result(to - from);
        
        __execute(from, to, keyfn);
        
        BinIdx binidx = 0;
        for(auto &itmgrp : lastResult()) {
            for(const Item& itm : itmgrp) 
                result[itm.id()] =
                    std::make_tuple(keyfn(from + itm.id()), itm.translation(),
                                    itm.rotation(), binidx);
            
            ++binidx;
        }
        
        return result;
    }
    
    template<class It, class Key = size_t>
    inline NotConvertibleOnly<It, const NestResult<Key>> _execute(
        It from, It to, std::function<Key(It)> keyfn)
    {
        item_cache_.reserve(to - from);
        for(auto it = from; it != to; ++it)
            item_cache_.emplace_back(Indexed<typename It::value_type>::get(*it));

        return _execute(item_cache_.begin(), item_cache_.end(), keyfn);
    }

    template<class It> inline void __execute(It from, It to)
    {
        auto infl = static_cast<Coord>(std::ceil(min_obj_distance_/2.0));
        if(infl > 0) std::for_each(from, to, [this](Item& item) {
            item.inflate(infl);
        });

        selector_.template packItems<PlacementStrategy>(
                    from, to, bin_, pconfig_);
        
        if(min_obj_distance_ > 0) std::for_each(from, to, [](Item& item) {
            item.inflate(-infl);
        });
    }
};

}

#endif // LIBNEST2D_HPP
