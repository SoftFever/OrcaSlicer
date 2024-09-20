#include "FlushVolPredictor.hpp"
#include "Utils.hpp"
#include <fstream>
#include <sstream>


static bool rgb_hex_to_dec(const std::string& hexstr, unsigned char& r, unsigned char& g, unsigned char& b)
{
    if (hexstr.empty() || hexstr.length() != 7 || hexstr[0] != '#')
    {
        assert(false);
        r = 0, g = 0, b = 0;
        return false;
    }

    auto hexToByte = [](const std::string& hex)->int
        {
            unsigned int byte;
            std::istringstream(hex) >> std::hex >> byte;
            return byte;
        };
    r = hexToByte(hexstr.substr(1, 2));
    g = hexToByte(hexstr.substr(3, 2));
    b = hexToByte(hexstr.substr(5, 2));
    return true;
}


FlushVolPredictor::FlushVolPredictor(const std::string& data_file)
{
    std::ifstream in(data_file);
    if (!in.is_open()) {
        m_valid = false;
        return;
    }
    std::string line;
    getline(in, line); // skip the first line
    while (std::getline(in, line)) {
        std::istringstream iss(line);
        std::string rgb_src, rgb_dst;
        float value;
        if (iss >> rgb_src >> rgb_dst >> value) {
            unsigned char r_src = 0, g_src = 0, b_src = 0;
            unsigned char r_dst = 0, g_dst = 0, b_dst = 0;
            if (!rgb_hex_to_dec(rgb_src, r_src, g_src, b_src)) {
                m_valid = false;
                return;
            }
            if (!rgb_hex_to_dec(rgb_dst, r_dst, g_dst, b_dst)) {
                m_valid = false;
                return;
            }
            uint64_t key = 0;
            key |= (static_cast<uint64_t>(r_src) << 40);
            key |= (static_cast<uint64_t>(g_src) << 32);
            key |= (static_cast<uint64_t>(b_src) << 24);
            key |= (static_cast<uint64_t>(r_dst) << 16);
            key |= (static_cast<uint64_t>(g_dst) << 8);
            key |= static_cast<uint64_t>(b_dst);
            m_flush_map.emplace(key, value);
        }
        else {
            m_valid = false;
            return;
        }
    }
    m_valid = true;
}

bool FlushVolPredictor::predict(const unsigned char src_r, const unsigned char src_g, const unsigned char src_b, const unsigned char dst_r, const unsigned char dst_g, const unsigned char dst_b, float& flush)
{
    if (!m_valid)
        return false;

    uint64_t key = 0;
    key |= (static_cast<uint64_t>(src_r) << 40);
    key |= (static_cast<uint64_t>(src_g) << 32);
    key |= (static_cast<uint64_t>(src_b) << 24);
    key |= (static_cast<uint64_t>(dst_r) << 16);
    key |= (static_cast<uint64_t>(dst_g) << 8);
    key |= static_cast<uint64_t>(dst_b);

    auto iter = m_flush_map.find(key);
    if (iter == m_flush_map.end())
        return false;

    flush = iter->second;
    return true;
}

FlushVolPredictor& FlushVolPredictor::get_instance()
{
    static std::string prefix = Slic3r::resources_dir();
    static FlushVolPredictor instance(prefix + "/flush/flush_data.txt");
    return instance;
}
