#ifndef LIBNFPGLUE_HPP
#define LIBNFPGLUE_HPP

#include <libnest2d/clipper_backend/clipper_backend.hpp>

namespace libnest2d {

using NfpR = nfp::NfpResult<PolygonImpl>;

NfpR _nfp(const PolygonImpl& sh, const PolygonImpl& cother);

template<>
struct nfp::NfpImpl<PolygonImpl, nfp::NfpLevel::CONVEX_ONLY> {
    NfpR operator()(const PolygonImpl& sh, const PolygonImpl& cother);
};

template<>
struct nfp::NfpImpl<PolygonImpl, nfp::NfpLevel::ONE_CONVEX> {
    NfpR operator()(const PolygonImpl& sh, const PolygonImpl& cother);
};

template<>
struct nfp::NfpImpl<PolygonImpl, nfp::NfpLevel::BOTH_CONCAVE> {
    NfpR operator()(const PolygonImpl& sh, const PolygonImpl& cother);
};

//template<>
//struct Nfp::NfpImpl<PolygonImpl, NfpLevel::ONE_CONVEX_WITH_HOLES> {
//    NfpResult operator()(const PolygonImpl& sh, const PolygonImpl& cother);
//};

//template<>
//struct Nfp::NfpImpl<PolygonImpl, NfpLevel::BOTH_CONCAVE_WITH_HOLES> {
//    NfpResult operator()(const PolygonImpl& sh, const PolygonImpl& cother);
//};

template<> struct nfp::MaxNfpLevel<PolygonImpl> {
    static const BP2D_CONSTEXPR NfpLevel value =
//            NfpLevel::CONVEX_ONLY;
            NfpLevel::BOTH_CONCAVE;
};

}


#endif // LIBNFPGLUE_HPP
