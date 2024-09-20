#ifndef FLUSH_VOL_PREDICTOR_HPP
#define FLUSH_VOL_PREDICTOR_HPP

#include<unordered_map>

class FlushVolPredictor
{
public:
    bool predict(const unsigned char src_r, const unsigned char src_g, const unsigned char src_b,
        const unsigned char dst_r, const unsigned char dst_g, const unsigned char dst_b, float& flush);
    static FlushVolPredictor& get_instance();
private:
    FlushVolPredictor(const std::string& data_file);
    FlushVolPredictor(const FlushVolPredictor&) = delete;
    FlushVolPredictor& operator=(const FlushVolPredictor&) = delete;
    ~FlushVolPredictor() = default;
private:
    std::unordered_map<uint64_t, float> m_flush_map;
    bool m_valid;
};

#endif