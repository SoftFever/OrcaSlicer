#include <stdio.h>
#include <random>

#include "wx/colour.h"

class ColorGenerator
{
    // Some of next code is borrowed from https://stackoverflow.com/questions/3018313/algorithm-to-convert-rgb-to-hsv-and-hsv-to-rgb-in-range-0-255-for-both
    typedef struct {
        double r;       // a fraction between 0 and 1
        double g;       // a fraction between 0 and 1
        double b;       // a fraction between 0 and 1
    } rgb;

    typedef struct {
        double h;       // angle in degrees
        double s;       // a fraction between 0 and 1
        double v;       // a fraction between 0 and 1
    } hsv;

    //static hsv   rgb2hsv(rgb in);
    //static rgb   hsv2rgb(hsv in);

    hsv rgb2hsv(rgb in)
    {
        hsv         out;
        double      min, max, delta;

        min = in.r < in.g ? in.r : in.g;
        min = min < in.b ? min : in.b;

        max = in.r > in.g ? in.r : in.g;
        max = max > in.b ? max : in.b;

        out.v = max;                                // v
        delta = max - min;
        if (delta < 0.00001)
        {
            out.s = 0;
            out.h = 0; // undefined, maybe nan?
            return out;
        }
        if (max > 0.0) { // NOTE: if Max is == 0, this divide would cause a crash
            out.s = (delta / max);                  // s
        }
        else {
            // if max is 0, then r = g = b = 0              
            // s = 0, h is undefined
            out.s = 0.0;
            out.h = NAN;                            // its now undefined
            return out;
        }
        if (in.r >= max)                           // > is bogus, just keeps compilor happy
            out.h = (in.g - in.b) / delta;        // between yellow & magenta
        else
            if (in.g >= max)
                out.h = 2.0 + (in.b - in.r) / delta;  // between cyan & yellow
            else
                out.h = 4.0 + (in.r - in.g) / delta;  // between magenta & cyan

        out.h *= 60.0;                              // degrees

        if (out.h < 0.0)
            out.h += 360.0;

        return out;
    }

    hsv rgb2hsv(const std::string& str_clr_in)
    {
        wxColour clr(str_clr_in);
        rgb in = { clr.Red() / 255.0, clr.Green() / 255.0, clr.Blue() / 255.0 };
        return rgb2hsv(in);
    }


    rgb hsv2rgb(hsv in)
    {
        double      hh, p, q, t, ff;
        long        i;
        rgb         out;

        if (in.s <= 0.0) {       // < is bogus, just shuts up warnings
            out.r = in.v;
            out.g = in.v;
            out.b = in.v;
            return out;
        }
        hh = in.h;
        if (hh >= 360.0) hh -= 360.0;//hh = 0.0;
        hh /= 60.0;
        i = (long)hh;
        ff = hh - i;
        p = in.v * (1.0 - in.s);
        q = in.v * (1.0 - (in.s * ff));
        t = in.v * (1.0 - (in.s * (1.0 - ff)));

        switch (i) {
        case 0:
            out.r = in.v;
            out.g = t;
            out.b = p;
            break;
        case 1:
            out.r = q;
            out.g = in.v;
            out.b = p;
            break;
        case 2:
            out.r = p;
            out.g = in.v;
            out.b = t;
            break;

        case 3:
            out.r = p;
            out.g = q;
            out.b = in.v;
            break;
        case 4:
            out.r = t;
            out.g = p;
            out.b = in.v;
            break;
        case 5:
        default:
            out.r = in.v;
            out.g = p;
            out.b = q;
            break;
        }
        return out;
    }

    std::random_device rd;

public:

    ColorGenerator() {}
    ~ColorGenerator() {}

    double rand_val()
    {
        std::mt19937 rand_generator(rd());

        // this value will be used for Saturation and Value
        // to avoid extremely light/dark colors, take this value from range [0.65; 1.0]
        std::uniform_real_distribution<double> distrib(0.65, 1.0);
        return distrib(rand_generator);
    }


    std::string get_opposite_color(const std::string& color)
    {
        std::string opp_color = "";

        hsv hsv_clr = rgb2hsv(color);
        hsv_clr.h += 65; // 65 instead 60 to avoid circle values
        hsv_clr.s = rand_val();
        hsv_clr.v = rand_val();

        rgb rgb_opp_color = hsv2rgb(hsv_clr);

        wxString clr_str = wxString::Format(wxT("#%02X%02X%02X"), (unsigned char)(rgb_opp_color.r * 255), (unsigned char)(rgb_opp_color.g * 255), (unsigned char)(rgb_opp_color.b * 255));
        opp_color = clr_str.ToStdString();

        return opp_color;
    }

    std::string get_opposite_color(const std::string& color_frst, const std::string& color_scnd)
    {
        std::string opp_color = "";

        hsv hsv_frst = rgb2hsv(color_frst);
        hsv hsv_scnd = rgb2hsv(color_scnd);

        double delta_h = fabs(hsv_frst.h - hsv_scnd.h);
        double start_h = delta_h > 180 ? std::min<double>(hsv_scnd.h, hsv_frst.h) : std::max<double>(hsv_scnd.h, hsv_frst.h);
        start_h += 5; // to avoid circle change of colors for 120 deg
        if (delta_h < 180)
            delta_h = 360 - delta_h;

        hsv hsv_opp = hsv{ start_h + 0.5 * delta_h, rand_val(), rand_val() };
        rgb rgb_opp_color = hsv2rgb(hsv_opp);

        wxString clr_str = wxString::Format(wxT("#%02X%02X%02X"), (unsigned char)(rgb_opp_color.r * 255), (unsigned char)(rgb_opp_color.g * 255), (unsigned char)(rgb_opp_color.b * 255));
        opp_color = clr_str.ToStdString();

        return opp_color;
    }
};