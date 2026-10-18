#include "OrcaMobile.hpp"

#include "libslic3r/Utils.hpp"
#include "libslic3r_version.h"

namespace Slic3r { namespace Mobile {

std::string core_version()
{
    return SoftFever_VERSION;
}

void set_directories(const std::string& resources_dir, const std::string& data_dir)
{
    set_resources_dir(resources_dir);
    set_var_dir(resources_dir + "/images");
    set_local_dir(resources_dir + "/i18n");
    set_sys_shapes_dir(resources_dir + "/shapes");
    set_custom_gcodes_dir(data_dir + "/custom_gcodes");
    set_data_dir(data_dir);
}

}} // namespace Slic3r::Mobile
