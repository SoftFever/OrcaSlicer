#ifndef slic3r_ZTable_hpp_
#define slic3r_ZTable_hpp_

#include <vector>

namespace Slic3r {

class ZTable
{
    public:
    ZTable(std::vector<unsigned int>* z_array);
    std::vector<unsigned int> get_range(unsigned int min_z, unsigned int max_z);
    std::vector<unsigned int> z;
};

ZTable::ZTable(std::vector<unsigned int>* ztable) :
    z(*ztable)
{
}

}

#endif
