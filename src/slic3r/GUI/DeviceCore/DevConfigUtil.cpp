#include "DevConfigUtil.h"

#include <wx/dir.h>

using namespace nlohmann;

namespace Slic3r
{

std::string DevPrinterConfigUtil::m_resource_file_path = "";


std::map<std::string, std::string> DevPrinterConfigUtil::get_all_model_id_with_name()
{
    {
        std::map<std::string, std::string> models;

        wxDir dir(m_resource_file_path + "/printers/");
        if (!dir.IsOpened())
        {
            return models;
        }

        wxString filename;
        std::vector<wxString> m_files;
        bool hasFile = dir.GetFirst(&filename, wxEmptyString, wxDIR_FILES);
        while (hasFile)
        {
            m_files.push_back(filename);
            hasFile = dir.GetNext(&filename);
        }

        for (wxString file : m_files)
        {
            if (!file.Lower().ends_with(".json")) continue;

            std::string config_file = m_resource_file_path + "/printers/" + file.ToStdString();
            boost::nowide::ifstream json_file(config_file.c_str());

            try
            {
                json jj;
                if (json_file.is_open())
                {
                    json_file >> jj;
                    if (jj.contains("00.00.00.00"))
                    {
                        json const& printer = jj["00.00.00.00"];

                        std::string model_id;
                        std::string display_name;
                        if (printer.contains("model_id")) { model_id = printer["model_id"].get<std::string>(); }
                        if (printer.contains("display_name")) { display_name = printer["display_name"].get<std::string>(); }
                        models[display_name] = model_id;
                    }
                }
            }
            catch (...) {}
        }

        return models;
    }
}

PrinterArch DevPrinterConfigUtil::get_printer_arch(std::string type_str)
{
    const std::string& arch_str = get_value_from_config<std::string>(type_str, "printer_arch");
    if (arch_str == "i3")
    {
        return PrinterArch::ARCH_I3;
    }
    else if (arch_str == "core_xy")
    {
        return PrinterArch::ARCH_CORE_XY;
    }

    return PrinterArch::ARCH_CORE_XY;
}

std::string DevPrinterConfigUtil::get_printer_ext_img(const std::string& type_str, int pos)
{
    const auto& vec = get_value_from_config<std::vector<std::string>>(type_str, "printer_ext_image");
    return (vec.size() > pos) ? vec[pos] : std::string();
};

std::string DevPrinterConfigUtil::get_fan_text(const std::string& type_str, const std::string& key)
{
    std::vector<std::string> filaments;
    std::string              config_file = m_resource_file_path + "/printers/" + type_str + ".json";
    boost::nowide::ifstream  json_file(config_file.c_str());
    try
    {
        json jj;
        if (json_file.is_open())
        {
            json_file >> jj;
            if (jj.contains("00.00.00.00"))
            {
                json const& printer = jj["00.00.00.00"];
                if (printer.contains("fan") && printer["fan"].contains(key))
                {
                    return printer["fan"][key].get<std::string>();
                }
            }
        }
    }
    catch (...) {}
    return std::string();
}

std::map<std::string, std::vector<std::string>> DevPrinterConfigUtil::get_all_subseries(std::string type_str)
{
    std::map<std::string, std::vector<std::string>> subseries;
    std::vector<wxString> m_files;

    wxDir dir(m_resource_file_path + "/printers/");
    if (!dir.IsOpened()) { return subseries; }

    wxString filename;
    bool     hasFile = dir.GetFirst(&filename, "*.json", wxDIR_FILES);
    while (hasFile)
    {
        m_files.push_back(filename);
        hasFile = dir.GetNext(&filename);
    }

    for (wxString file : m_files)
    {
        std::string             config_file = m_resource_file_path + "/printers/" + file.ToStdString();
        boost::nowide::ifstream json_file(config_file.c_str());

        try
        {
            json jj;
            if (json_file.is_open())
            {
                json_file >> jj;
                if (jj.contains("00.00.00.00"))
                {

                    json const& printer = jj["00.00.00.00"];
                    if (printer.contains("subseries"))
                    {
                        std::vector<std::string> subs;

                        std::string model_id = printer["model_id"].get<std::string>();
                        if (model_id == type_str || type_str.empty())
                        {
                            for (auto res : printer["subseries"])
                            {
                                subs.emplace_back(res.get<std::string>());
                            }
                        }
                        subseries.insert(make_pair(model_id, subs));
                    }


                }
            }
        }
        catch (...) {}
    }

    return subseries;
}

};