#include "json_diff.hpp"
#include <string>
#include <atomic>
#include <vector>
#include <fstream>
#include <iostream>
#include <thread>
#include <functional>

#include <stdio.h>
#include "nlohmann/json.hpp"
using namespace std;
using json = nlohmann::json;

int json_diff::diff_objects(json in, json &out, json &base)
{
    for (auto& el: in.items()) {
        if (el.value().empty()) {
            //BBL_LOG_INFO("json_c diff empty key: " << el.key());
            continue;
        }

        if (!base.contains(el.key()) ) {
            out[el.key()] = el.value();
            BOOST_LOG_TRIVIAL(trace) << "json_c diff new key: " << el.key()
                        << " type: "  << el.value().type_name()
                        << " value: " << el.value();

            continue;
        }

        if (el.value().type() != base[el.key()].type()) {
            out[el.key()] = el.value();
            BOOST_LOG_TRIVIAL(trace) << "json_c diff type changed"
                 << " key: " << el.key() << " value: " << el.value().dump()
                 << " last value: " << base[el.key()].dump();
            continue;
        }


        if (el.value().is_object()) {
            json recur_out;
            int recur_ret = diff_objects(
                              el.value(), recur_out, base[el.key()]);
            if (recur_ret == 0) {
                out[el.key()] = recur_out;
            }
            continue;
        }

        if (el.value() != base[el.key()]) {
            out[el.key()] = el.value();
                BOOST_LOG_TRIVIAL(trace) << "json_c diff value changed"
                 << " key: " << el.key() << " value: " << el.value().dump()
                 << " last value: " << base[el.key()].dump();
            continue;
        }
    }

    if (out.empty())
        return 1;

    return 0;
}

int json_diff::all2diff_base_reset(json &base)
{
    BOOST_LOG_TRIVIAL(trace) << "all2diff_base_reset";
    all2diff_base = base;
    return 0;
}

int json_diff::all2diff(json& in, json& out)
{
    int ret = 0;
    if (all2diff_base.empty()) {
        all2diff_base = in;
        out = in;
        BOOST_LOG_TRIVIAL(trace) << "json_c diff base reinit";
        return 0;
    }

    ret = diff_objects(in, out, all2diff_base);
    if (ret != 0) {
        BOOST_LOG_TRIVIAL(trace) << "json_c diff no new info";
    }
    all2diff_base = in;
    return 0;
}

int json_diff::restore_objects(json in, json &out, json &base)
{
    json jout;

    for (auto& el: base.items()) {
        /*element not in input json,
          use base element to restore*/
        if (!in.contains(el.key()) ) {
            out[el.key()] = el.value();
            /*
            BBL_LOG_INFO("json_c restore compressed key " << el.key()
                        << " type: "  << el.value().type_name()
                        << " value: " << el.value());
            */
            continue;
        }

        /*element in both base and input, but json type changed
           use input to restore*/
        if (el.value().type() != in[el.key()].type() ){
            out[el.key()] = in[el.key()];
            BOOST_LOG_TRIVIAL(trace) << "json_c restore type changed"
                  << " key: " << el.key() << " value: "
                  << in[el.key()].dump()
                  << " last value: " << el.value().dump();
            continue;
        }

        /*element in both base and input, it is a object
          recursive until basic type*/
        if (el.value().is_object()) {
            json recur_out;
            int recur_ret = restore_objects(
                              in[el.key()], recur_out, el.value());
            if (recur_ret == 0) {
                out[el.key()] = recur_out;
            }
            continue;
        }

        /*element in both base and input, but value changed
          use input to restore*/
        if (el.value() != in[el.key()]) {
            out[el.key()] = in[el.key()];
            continue;
        }
        /*element in both base and input,value is same
          use base to restore*/
        out[el.key()] = el.value();
    }

    if (out.empty())
        return  -1;

    return 0;
}

int json_diff::restore_append_objects(json in, json &out)
{
    /*a new element comming, but be recoreded in base
      need be added to output*/
    for (auto& el: in.items()) {

        if (!out.contains(el.key()) ) {
            BOOST_LOG_TRIVIAL(trace) << "json_c append new " << el.key()
                        << " type: "  << el.value().type_name()
                        << " value: " << el.value();
            out[el.key()] = el.value();
            continue;
        }

        if (el.value().is_object()) {
            int recur_ret =
                     restore_append_objects(el.value(), out[el.key()]);
            if (recur_ret != 0) {
                BOOST_LOG_TRIVIAL(trace) << "json_c append obj failed"
                                 << " key: " << el.key()
                                 << " value: " << el.value();
                return recur_ret;
            }
        }
    }
    return 0;
}

int json_diff::diff2all(json &in, json &out)
{
    if (!diff2all_base.empty()) {
        int ret = restore_objects(in, out, diff2all_base);
        if (ret < 0) {
            BOOST_LOG_TRIVIAL(trace) << "json_c restore failed";
            decode_error_count++;
            return ret;
        }
    } else {
        BOOST_LOG_TRIVIAL(trace) << "json_c restore base empty";
        decode_error_count++;
        return -1;
    }
    restore_append_objects(in, out);
    diff2all_base = out;
    decode_error_count = 0;
    return 0;
}

void json_diff::compare_print(json &a, json &b)
{
    for (auto& e: a.items()) {
        if (!b.contains(e.key()) ) { BOOST_LOG_TRIVIAL(trace) << "json_c compare loss " << e.key()
                        << " type: "  << e.value().type_name();
        }
        if (e.value() != b[e.key()]) {
            BOOST_LOG_TRIVIAL(trace) << "json_c compare not equal: key: " << e.key()
                                 << " value: " << e.value();
            BOOST_LOG_TRIVIAL(trace) << "json_c compare vs value "
                                 << " vs value: " << b[e.key()];

        }
    }
    return;
}

bool json_diff::is_need_request()
{
    if (decode_error_count > 5) {
        return true;
    }
    return false;
}

int json_diff::diff2all_base_reset(json &base){
    BOOST_LOG_TRIVIAL(trace) << "diff2all_base_reset";
    diff2all_base = base;
    return 0;
}
