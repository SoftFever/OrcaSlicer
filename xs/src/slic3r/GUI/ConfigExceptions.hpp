#include <exception> 
namespace Slic3r {

class ConfigError : public std::runtime_error { 
using std::runtime_error::runtime_error;
};

namespace GUI {

class ConfigGUITypeError : public ConfigError { 
using ConfigError::ConfigError;
};
}

}
