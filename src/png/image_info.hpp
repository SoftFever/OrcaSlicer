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
#ifndef PNGPP_IMAGE_INFO_HPP_INCLUDED
#define PNGPP_IMAGE_INFO_HPP_INCLUDED

#include "types.hpp"
#include "palette.hpp"
#include "tRNS.hpp"
#include "pixel_traits.hpp"

namespace png
{

    /**
     * \brief Holds information about PNG image.
     *
     * \see image, generator, consumer
     */
    class image_info
    {
    public:
        /**
         * \brief Constructs the image_info object with default values
         * for color_type, interlace_type, compression_method and
         * filter_type.
         */
        image_info()
            : m_width(0),
              m_height(0),
              m_bit_depth(0),
              m_color_type(color_type_none),
              m_interlace_type(interlace_none),
              m_compression_type(compression_type_default),
              m_filter_type(filter_type_default),
              m_gamma(0.0)
        {
        }

        uint_32 get_width() const
        {
            return m_width;
        }

        void set_width(uint_32 width)
        {
            m_width = width;
        }

        uint_32 get_height() const
        {
            return m_height;
        }

        void set_height(uint_32 height)
        {
            m_height = height;
        }

        color_type get_color_type() const
        {
            return m_color_type;
        }

        void set_color_type(color_type color_space)
        {
            m_color_type = color_space;
        }

        int get_bit_depth() const
        {
            return m_bit_depth;
        }

        void set_bit_depth(int bit_depth)
        {
            m_bit_depth = bit_depth;
        }

        interlace_type get_interlace_type() const
        {
            return m_interlace_type;
        }

        void set_interlace_type(interlace_type interlace)
        {
            m_interlace_type = interlace;
        }

        compression_type get_compression_type() const
        {
            return m_compression_type;
        }

        void set_compression_type(compression_type compression)
        {
            m_compression_type = compression;
        }

        filter_type get_filter_type() const
        {
            return m_filter_type;
        }

        void set_filter_type(filter_type filter)
        {
            m_filter_type = filter;
        }

        palette const& get_palette() const
        {
            return m_palette;
        }

        palette& get_palette()
        {
            return m_palette;
        }

        void set_palette(palette const& plte)
        {
            m_palette = plte;
        }

        /**
         * \brief Removes all entries from the palette.
         */
        void drop_palette()
        {
            m_palette.clear();
        }

        tRNS const& get_tRNS() const
        {
            return m_tRNS;
        }

        tRNS& get_tRNS()
        {
            return m_tRNS;
        }

        void set_tRNS(tRNS const& trns)
        {
            m_tRNS = trns;
        }

        double get_gamma() const
        {
            return m_gamma;
        }

        void set_gamma(double gamma)
        {
            m_gamma = gamma;
        }

    protected:
        uint_32 m_width;
        uint_32 m_height;
        int m_bit_depth;
        color_type m_color_type;
        interlace_type m_interlace_type;
        compression_type m_compression_type;
        filter_type m_filter_type;
        palette m_palette;
        tRNS m_tRNS;
        double m_gamma;
    };

    /**
     * \brief Returns an image_info object with color_type and
     * bit_depth fields setup appropriate for the \c pixel type.
     */
    template< typename pixel >
    image_info
    make_image_info()
    {
        typedef pixel_traits< pixel > traits;
        image_info info;
        info.set_color_type(traits::get_color_type());
        info.set_bit_depth(traits::get_bit_depth());
        return info;
    }

} // namespace png

#endif // PNGPP_IMAGE_INFO_HPP_INCLUDED
