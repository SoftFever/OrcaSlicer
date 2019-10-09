#include <libnest2d.h>

namespace libnest2d {

template class Nester<NfpPlacer, FirstFitSelection>;
template class Nester<BottomLeftPlacer, FirstFitSelection>;

template std::size_t nest(std::vector<Item>::iterator from,
                          std::vector<Item>::iterator from to,
                          const Box & bin,
                          Coord dist,
                          const NestConfig<NfpPlacer, FirstFitSelection> &cfg,
                          NestControl ctl);

template std::size_t nest(std::vector<Item>::iterator from,
                          std::vector<Item>::iterator from to,
                          const Box & bin,
                          Coord dist,
                          const NestConfig<BottomLeftPlacer, FirstFitSelection> &cfg,
                          NestControl ctl);
}
