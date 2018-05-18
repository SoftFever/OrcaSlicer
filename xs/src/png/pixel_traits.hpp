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
#ifndef PNGPP_PIXEL_TRAITS_HPP_INCLUDED
#define PNGPP_PIXEL_TRAITS_HPP_INCLUDED

#include <limits>
#include "types.hpp"

namespace png
{

    /**
     * \brief Pixel traits class template.
     *
     * Provides information about pixel color type and components bit depth.
     * Not implemented--see specializations.
     *
     * \see  pixel_traits<rgb_pixel>, pixel_traits<rgba_pixel>
     */
    template< typename pixel > struct pixel_traits;

    /**
     * \brief Basic pixel traits class template.
     *
     * Provides common implementation for various pixel_traits<>
     * specializations.
     */
    template< typename pixel,
              typename component,
              color_type pixel_color_type,
              int channels_value = sizeof(pixel) / sizeof(component),
              int bit_depth_value = std::numeric_limits< component >::digits >
    struct basic_pixel_traits
    {
        typedef pixel pixel_type;
        typedef component component_type;

        static color_type get_color_type()
        {
            return pixel_color_type;
        }
        
        static const int channels = channels_value;
        static int get_channels()
        {
            return channels;
        }
        
        static const int bit_depth = bit_depth_value;
        static int get_bit_depth()
        {
            return bit_depth;
        }
    };

    /**
     * \brief Basic pixel traits class template for pixels with alpha
     * channel.
     */
    template< typename component >
    struct basic_alpha_pixel_traits
    {
        /**
         * \brief Returns the default alpha channel filler for full
         * opacity.
         */
        static component get_alpha_filler()
        {
            return std::numeric_limits< component >::max();
        }
    };

} // namespace png

#endif // PNGPP_PIXEL_TRAITS_HPP_INCLUDED
