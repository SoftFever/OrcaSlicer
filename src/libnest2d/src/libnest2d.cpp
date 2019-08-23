#include <libnest2d.h>

namespace libnest2d {

template class Nester<NfpPlacer, FirstFitSelection>;
template class Nester<BottomLeftPlacer, FirstFitSelection>;

template PackGroup nest(std::vector<Item>::iterator from, 
                        std::vector<Item>::iterator to,
                        const Box& bin,
                        Coord dist = 0,
                        const NfpPlacer::Config& pconf,
                        const FirstFitSelection::Config& sconf);

template PackGroup nest(std::vector<Item>::iterator from, 
                        std::vector<Item>::iterator to,
                        const Box& bin,
                        ProgressFunction prg,
                        StopCondition scond,
                        Coord dist = 0,
                        const NfpPlacer::Config& pconf,
                        const FirstFitSelection::Config& sconf);
}
