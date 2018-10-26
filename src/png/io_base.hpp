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
#ifndef PNGPP_IO_BASE_HPP_INCLUDED
#define PNGPP_IO_BASE_HPP_INCLUDED

#include <cassert>
#include <cstdio>
#include <cstdarg>
#include "error.hpp"
#include "info.hpp"
#include "end_info.hpp"

static void
trace_io_transform(char const* fmt, ...)
{
#ifdef DEBUG_IO_TRANSFORM
    va_list va;
    va_start(va, fmt);
    fprintf(stderr, "TRANSFORM_IO: ");
    vfprintf(stderr, fmt, va);
    va_end(va);
#endif
}
#define TRACE_IO_TRANSFORM trace_io_transform

namespace png
{

    /**
     * \brief Base class for PNG reader/writer classes.
     *
     * \see  reader, writer
     */
    class io_base
    {
        io_base(io_base const&);
        io_base& operator=(io_base const&);

    public:
        explicit io_base(png_struct* png)
            : m_png(png),
              m_info(*this, m_png),
              m_end_info(*this, m_png)
        {
        }

        ~io_base()
        {
            assert(! m_png);
            assert(! m_info.get_png_info());
            assert(! m_end_info.get_png_info());
        }

        png_struct* get_png_struct() const
        {
            return m_png;
        }

        info& get_info()
        {
            return m_info;
        }

        info const& get_info() const
        {
            return m_info;
        }

        image_info const& get_image_info() const
        {
            return m_info;
        }

        void set_image_info(image_info const& info)
        {
            static_cast< image_info& >(m_info) = info; // slice it
        }

        end_info& get_end_info()
        {
            return m_end_info;
        }

        end_info const& get_end_info() const
        {
            return m_end_info;
        }

        //////////////////////////////////////////////////////////////////////
        // info accessors
        //
        uint_32 get_width() const
        {
            return m_info.get_width();
        }

        void set_width(uint_32 width)
        {
            m_info.set_width(width);
        }

        uint_32 get_height() const
        {
            return m_info.get_height();
        }

        void set_height(uint_32 height)
        {
            m_info.set_height(height);
        }

        color_type get_color_type() const
        {
            return m_info.get_color_type();
        }

        void set_color_type(color_type color_space)
        {
            m_info.set_color_type(color_space);
        }

        int get_bit_depth() const
        {
            return m_info.get_bit_depth();
        }

        void set_bit_depth(int bit_depth)
        {
            m_info.set_bit_depth(bit_depth);
        }

        interlace_type get_interlace_type() const
        {
            return m_info.get_interlace_type();
        }

        void set_interlace_type(interlace_type interlace)
        {
            m_info.set_interlace_type(interlace);
        }

        compression_type get_compression_type() const
        {
            return m_info.get_compression_type();
        }

        void set_compression_type(compression_type compression)
        {
            m_info.set_compression_type(compression);
        }

        filter_type get_filter_type() const
        {
            return m_info.get_filter_type();
        }

        void set_filter_type(filter_type filter)
        {
            m_info.set_filter_type(filter);
        }

        //////////////////////////////////////////////////////////////////////

        bool has_chunk(chunk id)
        {
            return png_get_valid(m_png,
                                 m_info.get_png_info(),
                                 uint_32(id)) == uint_32(id);
        }

#if defined(PNG_READ_EXPAND_SUPPORTED)
        void set_gray_1_2_4_to_8() const
        {
            TRACE_IO_TRANSFORM("png_set_expand_gray_1_2_4_to_8\n");
            png_set_expand_gray_1_2_4_to_8(m_png);
        }

        void set_palette_to_rgb() const
        {
            TRACE_IO_TRANSFORM("png_set_palette_to_rgb\n");
            png_set_palette_to_rgb(m_png);
        }

        void set_tRNS_to_alpha() const
        {
            TRACE_IO_TRANSFORM("png_set_tRNS_to_alpha\n");
            png_set_tRNS_to_alpha(m_png);
        }
#endif // defined(PNG_READ_EXPAND_SUPPORTED)

#if defined(PNG_READ_BGR_SUPPORTED) || defined(PNG_WRITE_BGR_SUPPORTED)
        void set_bgr() const
        {
            TRACE_IO_TRANSFORM("png_set_bgr\n");
            png_set_bgr(m_png);
        }
#endif

#if defined(PNG_READ_GRAY_TO_RGB_SUPPORTED)
        void set_gray_to_rgb() const
        {
            TRACE_IO_TRANSFORM("png_set_gray_to_rgb\n");
            png_set_gray_to_rgb(m_png);
        }
#endif

#ifdef PNG_FLOATING_POINT_SUPPORTED
        void set_rgb_to_gray(rgb_to_gray_error_action error_action
                             = rgb_to_gray_silent,
                             double red_weight   = -1.0,
                             double green_weight = -1.0) const
        {
            TRACE_IO_TRANSFORM("png_set_rgb_to_gray: error_action=%d,"
                               " red_weight=%lf, green_weight=%lf\n",
                               error_action, red_weight, green_weight);

            png_set_rgb_to_gray(m_png, error_action, red_weight, green_weight);
        }
#else
        void set_rgb_to_gray(rgb_to_gray_error_action error_action
                             = rgb_to_gray_silent,
                             fixed_point red_weight   = -1,
                             fixed_point green_weight = -1) const
        {
            TRACE_IO_TRANSFORM("png_set_rgb_to_gray_fixed: error_action=%d,"
                               " red_weight=%d, green_weight=%d\n",
                               error_action, red_weight, green_weight);

            png_set_rgb_to_gray_fixed(m_png, error_action,
                                      red_weight, green_weight);
        }
#endif // PNG_FLOATING_POINT_SUPPORTED

        //////////////////////////////////////////////////////////////////////
        // alpha channel transformations
        //
#if defined(PNG_READ_STRIP_ALPHA_SUPPORTED)
        void set_strip_alpha() const
        {
            TRACE_IO_TRANSFORM("png_set_strip_alpha\n");
            png_set_strip_alpha(m_png);
        }
#endif

#if defined(PNG_READ_SWAP_ALPHA_SUPPORTED) \
    || defined(PNG_WRITE_SWAP_ALPHA_SUPPORTED)
        void set_swap_alpha() const
        {
            TRACE_IO_TRANSFORM("png_set_swap_alpha\n");
            png_set_swap_alpha(m_png);
        }
#endif

#if defined(PNG_READ_INVERT_ALPHA_SUPPORTED) \
    || defined(PNG_WRITE_INVERT_ALPHA_SUPPORTED)
        void set_invert_alpha() const
        {
            TRACE_IO_TRANSFORM("png_set_invert_alpha\n");
            png_set_invert_alpha(m_png);
        }
#endif

#if defined(PNG_READ_FILLER_SUPPORTED) || defined(PNG_WRITE_FILLER_SUPPORTED)
        void set_filler(uint_32 filler, filler_type type) const
        {
            TRACE_IO_TRANSFORM("png_set_filler: filler=%08x, type=%d\n",
                               filler, type);

            png_set_filler(m_png, filler, type);
        }

#if !defined(PNG_1_0_X)
        void set_add_alpha(uint_32 filler, filler_type type) const
        {
            TRACE_IO_TRANSFORM("png_set_add_alpha: filler=%08x, type=%d\n",
                               filler, type);

            png_set_add_alpha(m_png, filler, type);
        }
#endif
#endif // PNG_READ_FILLER_SUPPORTED || PNG_WRITE_FILLER_SUPPORTED

#if defined(PNG_READ_SWAP_SUPPORTED) || defined(PNG_WRITE_SWAP_SUPPORTED)
        void set_swap() const
        {
            TRACE_IO_TRANSFORM("png_set_swap\n");
            png_set_swap(m_png);
        }
#endif

#if defined(PNG_READ_PACK_SUPPORTED) || defined(PNG_WRITE_PACK_SUPPORTED)
        void set_packing() const
        {
            TRACE_IO_TRANSFORM("png_set_packing\n");
            png_set_packing(m_png);
        }
#endif

#if defined(PNG_READ_PACKSWAP_SUPPORTED) \
    || defined(PNG_WRITE_PACKSWAP_SUPPORTED)
        void set_packswap() const
        {
            TRACE_IO_TRANSFORM("png_set_packswap\n");
            png_set_packswap(m_png);
        }
#endif

#if defined(PNG_READ_SHIFT_SUPPORTED) || defined(PNG_WRITE_SHIFT_SUPPORTED)
        void set_shift(byte red_bits, byte green_bits, byte blue_bits,
                       byte alpha_bits = 0) const
        {
            TRACE_IO_TRANSFORM("png_set_shift: red_bits=%d, green_bits=%d,"
                               " blue_bits=%d, alpha_bits=%d\n",
                               red_bits, green_bits, blue_bits, alpha_bits);

            if (get_color_type() != color_type_rgb
                || get_color_type() != color_type_rgb_alpha)
            {
                throw error("set_shift: expected RGB or RGBA color type");
            }
            color_info bits;
            bits.red = red_bits;
            bits.green = green_bits;
            bits.blue = blue_bits;
            bits.alpha = alpha_bits;
            png_set_shift(m_png, & bits);
        }

        void set_shift(byte gray_bits, byte alpha_bits = 0) const
        {
            TRACE_IO_TRANSFORM("png_set_shift: gray_bits=%d, alpha_bits=%d\n",
                               gray_bits, alpha_bits);

            if (get_color_type() != color_type_gray
                || get_color_type() != color_type_gray_alpha)
            {
                throw error("set_shift: expected Gray or Gray+Alpha color type");
            }
            color_info bits;
            bits.gray = gray_bits;
            bits.alpha = alpha_bits;
            png_set_shift(m_png, & bits);
        }
#endif // PNG_READ_SHIFT_SUPPORTED || PNG_WRITE_SHIFT_SUPPORTED

#if defined(PNG_READ_INTERLACING_SUPPORTED) \
    || defined(PNG_WRITE_INTERLACING_SUPPORTED)
        int set_interlace_handling() const
        {
            TRACE_IO_TRANSFORM("png_set_interlace_handling\n");
            return png_set_interlace_handling(m_png);
        }
#endif

#if defined(PNG_READ_INVERT_SUPPORTED) || defined(PNG_WRITE_INVERT_SUPPORTED)
        void set_invert_mono() const
        {
            TRACE_IO_TRANSFORM("png_set_invert_mono\n");
            png_set_invert_mono(m_png);
        }
#endif

#if defined(PNG_READ_16_TO_8_SUPPORTED)
        void set_strip_16() const
        {
            TRACE_IO_TRANSFORM("png_set_strip_16\n");
            png_set_strip_16(m_png);
        }
#endif

#if defined(PNG_READ_USER_TRANSFORM_SUPPORTED)
        void set_read_user_transform(png_user_transform_ptr transform_fn)
        {
            TRACE_IO_TRANSFORM("png_set_read_user_transform_fn\n");
            png_set_read_user_transform_fn(m_png, transform_fn);
        }
#endif

#if defined(PNG_READ_USER_TRANSFORM_SUPPORTED) \
    || defined(PNG_WRITE_USER_TRANSFORM_SUPPORTED)
        void set_user_transform_info(void* info, int bit_depth, int channels)
        {
            TRACE_IO_TRANSFORM("png_set_user_transform_info: bit_depth=%d,"
                               " channels=%d\n", bit_depth, channels);

            png_set_user_transform_info(m_png, info, bit_depth, channels);
        }
#endif

    protected:
        void* get_io_ptr() const
        {
            return png_get_io_ptr(m_png);
        }

        void set_error(char const* message)
        {
            assert(message);
            m_error = message;
        }

        void reset_error()
        {
            m_error.clear();
        }

/*
        std::string const& get_error() const
        {
            return m_error;
        }
*/

        bool is_error() const
        {
            return !m_error.empty();
        }

        void raise_error()
        {
            longjmp(png_jmpbuf(m_png), -1);
        }

        static void raise_error(png_struct* png, char const* message)
        {
            io_base* io = static_cast< io_base* >(png_get_error_ptr(png));
            io->set_error(message);
            io->raise_error();
        }

        png_struct* m_png;
        info m_info;
        end_info m_end_info;
        std::string m_error;
    };

} // namespace png

#endif // PNGPP_IO_BASE_HPP_INCLUDED
