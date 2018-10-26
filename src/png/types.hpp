/*
 * Copyright (C) 2007,2008   Alex Shulgin
 *
 * This file is part of png++ the C++ wrapper for libpng.  PNG++ is free
 * software; the exact copying conditions are as follows:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 *
 * 3. The name of the author may not be used to endorse or promote products
 * derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN
 * NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#ifndef PNGPP_TYPES_HPP_INCLUDED
#define PNGPP_TYPES_HPP_INCLUDED

#include <png.h>

namespace png
{

    typedef png_byte byte;
    typedef png_uint_16 uint_16;
    typedef png_uint_32 uint_32;
    typedef png_fixed_point fixed_point;
    typedef png_color_8 color_info;
    typedef png_color_16 color_info_16;

    enum color_type
    {
        color_type_none       = -1,
        color_type_gray       = PNG_COLOR_TYPE_GRAY,
        color_type_palette    = PNG_COLOR_TYPE_PALETTE,
        color_type_rgb        = PNG_COLOR_TYPE_RGB,
        color_type_rgb_alpha  = PNG_COLOR_TYPE_RGB_ALPHA,
        color_type_gray_alpha = PNG_COLOR_TYPE_GRAY_ALPHA,
        color_type_rgba       = PNG_COLOR_TYPE_RGBA,
        color_type_ga         = PNG_COLOR_TYPE_GA
    };

    enum color_mask
    {
        color_mask_palette = PNG_COLOR_MASK_PALETTE,
        color_mask_color   = PNG_COLOR_MASK_COLOR,
        color_mask_rgb     = color_mask_color,
        color_mask_alpha   = PNG_COLOR_MASK_ALPHA
    };

    enum filler_type
    {
        filler_before = PNG_FILLER_BEFORE,
        filler_after  = PNG_FILLER_AFTER
    };

    enum rgb_to_gray_error_action
    {
        rgb_to_gray_silent  = 1,
        rgb_to_gray_warning = 2,
        rgb_to_gray_error   = 3
    };

    enum interlace_type
    {
        interlace_none  = PNG_INTERLACE_NONE,
        interlace_adam7 = PNG_INTERLACE_ADAM7
    };

    enum compression_type
    {
        compression_type_base    = PNG_COMPRESSION_TYPE_BASE,
        compression_type_default = PNG_COMPRESSION_TYPE_DEFAULT
    };

    enum filter_type
    {
        filter_type_base        = PNG_FILTER_TYPE_BASE,
        intrapixel_differencing = PNG_INTRAPIXEL_DIFFERENCING,
        filter_type_default     = PNG_FILTER_TYPE_DEFAULT
    };

    enum chunk
    {
        chunk_gAMA = PNG_INFO_gAMA,
        chunk_sBIT = PNG_INFO_sBIT,
        chunk_cHRM = PNG_INFO_cHRM,
        chunk_PLTE = PNG_INFO_PLTE,
        chunk_tRNS = PNG_INFO_tRNS,
        chunk_bKGD = PNG_INFO_bKGD,
        chunk_hIST = PNG_INFO_hIST,
        chunk_pHYs = PNG_INFO_pHYs,
        chunk_oFFs = PNG_INFO_oFFs,
        chunk_tIME = PNG_INFO_tIME,
        chunk_pCAL = PNG_INFO_pCAL,
        chunk_sRGB = PNG_INFO_sRGB,
        chunk_iCCP = PNG_INFO_iCCP,
        chunk_sPLT = PNG_INFO_sPLT,
        chunk_sCAL = PNG_INFO_sCAL,
        chunk_IDAT = PNG_INFO_IDAT
    };

} // namespace png

#endif // PNGPP_TYPES_HPP_INCLUDED
