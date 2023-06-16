#ifndef slic3r_FlushVolCalc_hpp_
#define slic3r_FlushVolCalc_hpp_

#include "libslic3r.h"
#include "Config.hpp"


namespace Slic3r {

extern const int g_min_flush_volume_from_support;
extern const int g_flush_volume_to_support;
extern const int g_max_flush_volume;

class FlushVolCalculator
{
public:
    FlushVolCalculator(int min, int max, float multiplier = 1.0f);
    ~FlushVolCalculator()
    {
    }

    int calc_flush_vol(unsigned char src_a, unsigned char src_r, unsigned char src_g, unsigned char src_b,
        unsigned char dst_a, unsigned char dst_r, unsigned char dst_g, unsigned char dst_b);

private:
    int m_min_flush_vol;
    int m_max_flush_vol;
    float m_multiplier;
};


}

#endif
