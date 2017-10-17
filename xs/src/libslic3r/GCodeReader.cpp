#include "GCodeReader.hpp"
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>
#include <fstream>
#include <iostream>

namespace Slic3r {

void GCodeReader::apply_config(const GCodeConfig &config)
{
    m_config = config;
    m_extrusion_axis = m_config.get_extrusion_axis()[0];
}

void GCodeReader::apply_config(const DynamicPrintConfig &config)
{
    m_config.apply(config, true);
    m_extrusion_axis = m_config.get_extrusion_axis()[0];
}

void GCodeReader::parse(const std::string &gcode, callback_t callback)
{
    std::istringstream ss(gcode);
    std::string line;
    while (std::getline(ss, line))
        this->parse_line(line, callback);
}

void GCodeReader::parse_line(std::string line, callback_t callback)
{
    GCodeLine gline(this);
    gline.raw = line;
    if (this->verbose)
        std::cout << line << std::endl;
    
    // strip comment
    {
        size_t pos = line.find(';');
        if (pos != std::string::npos) {
            gline.comment = line.substr(pos+1);
            line.erase(pos);
        }
    }
    
    // command and args
    {
        std::vector<std::string> args;
        boost::split(args, line, boost::is_any_of(" "));
        
        // first one is cmd
        gline.cmd = args.front();
        args.erase(args.begin());
        
        for (std::string &arg : args) {
            if (arg.size() < 2) continue;
            gline.args.insert(std::make_pair(arg[0], arg.substr(1)));
        }
    }
    
    // convert extrusion axis
    if (m_extrusion_axis != 'E') {
        const auto it = gline.args.find(m_extrusion_axis);
        if (it != gline.args.end()) {
            std::swap(gline.args['E'], it->second);
            gline.args.erase(it);
        }
    }
    
    if (gline.has('E') && m_config.use_relative_e_distances)
        this->E = 0;
    
    if (callback) callback(*this, gline);
    
    // update coordinates
    if (gline.cmd == "G0" || gline.cmd == "G1" || gline.cmd == "G92") {
        this->X = gline.new_X();
        this->Y = gline.new_Y();
        this->Z = gline.new_Z();
        this->E = gline.new_E();
        this->F = gline.new_F();
    }
}

void GCodeReader::parse_file(const std::string &file, callback_t callback)
{
    std::ifstream f(file);
    std::string line;
    while (std::getline(f, line))
        this->parse_line(line, callback);
}

void GCodeReader::GCodeLine::set(char arg, std::string value)
{
    const std::string space(" ");
    if (this->has(arg)) {
        size_t pos = this->raw.find(space + arg)+2;
        size_t end = this->raw.find(' ', pos+1);
        this->raw = this->raw.replace(pos, end-pos, value);
    } else {
        size_t pos = this->raw.find(' ');
        if (pos == std::string::npos) {
            this->raw += space + arg + value;
        } else {
            this->raw = this->raw.replace(pos, 0, space + arg + value);
        }
    }
    this->args[arg] = value;
}

}
