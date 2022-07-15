#ifndef __JSON_DIFF_HPP
#define __JSON_DIFF_HPP

#include <string>
#include <atomic>
#include <vector>

#include "nlohmann/json.hpp"


using json = nlohmann::json;
using namespace std;

class json_diff
{
private:
    json diff2all_base;
    json all2diff_base;
    int  decode_error_count = 0;

    int diff_objects(json in, json &out, json &base);
    int restore_objects(json in, json &out, json &base);
    int restore_append_objects(json in, json &out);

public:
    int all2diff(json &in, json &out);
    int diff2all(json &in, json &out);
    int all2diff_base_reset(json &base);
    int diff2all_base_reset(json &base);
    void compare_print(json &a, json &b);

    bool is_need_request();
};
#endif // __JSON_DIFF_HPP
