#include "PrintConfig.hpp"
#include "CommonDefs.hpp"
#include "Point.hpp"
#include "Polygon.hpp"
#include "PrintConfigConstants.hpp"
#include "BeltTransform.hpp"
#include "ClipperUtils.hpp"
#include "Config.hpp"
#include "Geometry.hpp"
#include "FilamentMixer.hpp"
#include "MaterialType.hpp"
#include "I18N.hpp"
#include "enum_bitmask.hpp"
#include "format.hpp"

#include "GCode/Thumbnails.hpp"
#include <numeric>
#include <cstddef>
#include <algorithm>
#include <cassert>
#include <map>
#include <boost/algorithm/string/classification.hpp>
#include <iterator>
#include <cstdlib>
#include "libslic3r.h"
#include <boost/algorithm/string/predicate.hpp>
#include <cmath>
#include <boost/algorithm/string/constants.hpp>
#include <limits>
#include <memory>
#include <boost/preprocessor/cat.hpp>
#include <boost/preprocessor/seq/for_each.hpp>
#include <boost/preprocessor/tuple/to_seq.hpp>
#include <cstdint>
#include <set>
#include <boost/algorithm/string/case_conv.hpp>
#include <boost/algorithm/string/replace.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/trim.hpp>
#include <boost/format.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/log/trivial.hpp>
#include <boost/thread.hpp>
#include <float.h>
#include <string>
#include <vector>
#include <utility>
#include <sstream>
#include <unordered_map>


namespace Slic3r {

//! macro used to mark string used at localization,
//! return same string
#define L(s) (s)

static const t_config_enum_values s_keys_map_BedType = {
    { "Default Plate",      btDefault },
    { "Supertack Plate",    btSuperTack },
    { "Cool Plate",         btPC },
    { "Engineering Plate",  btEP  },
    { "High Temp Plate",    btPEI  },
    { "Textured PEI Plate", btPTE },
    { "Textured Cool Plate", btPCT }
};

void PrintConfigDef::init_fff_prepare_plate_settings()
{
    ConfigOptionDef* def;


    def = this->add("curr_bed_type", coEnum);
    def->label = L("Bed type");
    def->tooltip = L("Plate types supported by the printer");
    def->mode = comSimple;
    def->enum_keys_map = &s_keys_map_BedType;
    // Orca: make sure the order of the values is the same as the BedType enum 
    def->enum_values.emplace_back("Cool Plate");
    def->enum_values.emplace_back("Engineering Plate");
    def->enum_values.emplace_back("High Temp Plate");
    def->enum_values.emplace_back("Textured PEI Plate");
    def->enum_values.emplace_back("Textured Cool Plate");
    def->enum_values.emplace_back("Supertack Plate");
    def->enum_labels.emplace_back(L("Smooth Cool Plate"));
    def->enum_labels.emplace_back(L("Engineering Plate"));
    def->enum_labels.emplace_back(L("Smooth High Temp Plate"));
    def->enum_labels.emplace_back(L("Textured PEI Plate"));
    def->enum_labels.emplace_back(L("Textured Cool Plate"));
    def->enum_labels.emplace_back(L("Cool Plate (SuperTack)"));
    def->set_default_value(new ConfigOptionEnum<BedType>(btPC));

    def = this->add("first_layer_sequence_choice", coEnum);
    def->category = L("Quality");
    def->label = L("First layer filament sequence");
    def->enum_keys_map = &ConfigOptionEnum<LayerSeq>::get_enum_values();
    def->enum_values.push_back("Auto");
    def->enum_values.push_back("Customize");
    def->enum_labels.push_back(L("Auto"));
    def->enum_labels.push_back(L("Customize"));
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionEnum<LayerSeq>(flsAuto));

    def = this->add("other_layers_sequence_choice", coEnum);
    def->category = L("Quality");
    def->label = L("Other layers filament sequence");
    def->enum_keys_map = &ConfigOptionEnum<LayerSeq>::get_enum_values();
    def->enum_values.push_back("Auto");
    def->enum_values.push_back("Customize");
    def->enum_labels.push_back(L("Auto"));
    def->enum_labels.push_back(L("Customize"));
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionEnum<LayerSeq>(flsAuto));
}

} // namespace Slic3r
