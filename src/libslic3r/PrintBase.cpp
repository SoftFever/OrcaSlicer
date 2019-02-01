#include "PrintBase.hpp"

#include <boost/filesystem.hpp>
#include <boost/lexical_cast.hpp>

#include "I18N.hpp"

//! macro used to mark string used at localization, 
//! return same string
#define L(s) Slic3r::I18N::translate(s)

namespace Slic3r
{

size_t PrintStateBase::g_last_timestamp = 0;

// Update "scale", "input_filename", "input_filename_base" placeholders from the current m_objects.
void PrintBase::update_object_placeholders()
{
    // get the first input file name
    std::string input_file;
    std::vector<std::string> v_scale;
	for (const ModelObject *model_object : m_model.objects) {
		ModelInstance *printable = nullptr;
		for (ModelInstance *model_instance : model_object->instances)
			if (model_instance->is_printable()) {
				printable = model_instance;
				break;
			}
		if (printable) {
	        // CHECK_ME -> Is the following correct ?
			v_scale.push_back("x:" + boost::lexical_cast<std::string>(printable->get_scaling_factor(X) * 100) +
				"% y:" + boost::lexical_cast<std::string>(printable->get_scaling_factor(Y) * 100) +
				"% z:" + boost::lexical_cast<std::string>(printable->get_scaling_factor(Z) * 100) + "%");
	        if (input_file.empty())
	            input_file = model_object->input_file;
	    }
    }
    
    PlaceholderParser &pp = m_placeholder_parser;
    pp.set("scale", v_scale);
    if (! input_file.empty()) {
        // get basename with and without suffix
        const std::string input_basename = boost::filesystem::path(input_file).filename().string();
        pp.set("input_filename", input_basename);
        const std::string input_basename_base = input_basename.substr(0, input_basename.find_last_of("."));
        pp.set("input_filename_base", input_basename_base);
    }
}

std::string PrintBase::output_filename(const std::string &format, const std::string &default_ext, const DynamicConfig *config_override) const
{
    DynamicConfig cfg;
    if (config_override != nullptr)
    	cfg = *config_override;
    PlaceholderParser::update_timestamp(cfg);
    try {
        boost::filesystem::path filename = this->placeholder_parser().process(format, 0, &cfg);
        if (filename.extension().empty())
        	filename = boost::filesystem::change_extension(filename, default_ext);
        return filename.string();
    } catch (std::runtime_error &err) {
        throw std::runtime_error(L("Failed processing of the output_filename_format template.") + "\n" + err.what());
    }
}

std::string PrintBase::output_filepath(const std::string &path) const
{
    // if we were supplied no path, generate an automatic one based on our first object's input file
    if (path.empty())
        // get the first input file name
        return (boost::filesystem::path(m_model.propose_export_file_name()).parent_path() / this->output_filename()).make_preferred().string();
    
    // if we were supplied a directory, use it and append our automatically generated filename
    boost::filesystem::path p(path);
    if (boost::filesystem::is_directory(p))
        return (p / this->output_filename()).make_preferred().string();
    
    // if we were supplied a file which is not a directory, use it
    return path;
}

tbb::mutex& PrintObjectBase::state_mutex(PrintBase *print)
{ 
	return print->state_mutex();
}

std::function<void()> PrintObjectBase::cancel_callback(PrintBase *print)
{ 
	return print->cancel_callback();
}

} // namespace Slic3r
