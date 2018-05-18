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
#ifndef PNGPP_WRITER_HPP_INCLUDED
#define PNGPP_WRITER_HPP_INCLUDED

#include <cassert>
#include "io_base.hpp"

namespace png
{

    /**
     * \brief PNG writer class template.  This is the low-level
     * writing interface--use image class or generator class to
     * actually write images.
     *
     * The \c ostream template parameter specifies the type of output
     * stream to work with.  The \c ostream class should implement the
     * minimum of the following interface:
     *
     * \code
     * class my_ostream
     * {
     * public:
     *     void write(char const*, size_t);
     *     void flush();
     *     bool good();
     * };
     * \endcode
     *
     * With the semantics similar to the \c std::ostream.  Naturally,
     * \c std::ostream fits this requirement and can be used with the
     * writer class as is.
     *
     * \see image, reader, generator, io_base
     */
    template< class ostream >
    class writer
        : public io_base
    {
    public:
        /**
         * \brief Constructs a writer prepared to write PNG image into
         * a \a stream.
         */
        explicit writer(ostream& stream)
            : io_base(png_create_write_struct(PNG_LIBPNG_VER_STRING,
                                              static_cast< io_base* >(this),
                                              raise_error,
                                              0))
        {
            png_set_write_fn(m_png, & stream, write_data, flush_data);
        }

        ~writer()
        {
            m_end_info.destroy();
            png_destroy_write_struct(& m_png, m_info.get_png_info_ptr());
        }

        void write_png() const
        {
            if (setjmp(png_jmpbuf(m_png)))
            {
                throw error(m_error);
            }
            png_write_png(m_png,
                          m_info.get_png_info(),
                          /* transforms = */ 0,
                          /* params = */ 0);
        }

        /**
         * \brief Write info about PNG image.
         */
        void write_info() const
        {
            if (setjmp(png_jmpbuf(m_png)))
            {
                throw error(m_error);
            }
            m_info.write();
        }

        /**
         * \brief Writes a row of image data at a time.
         */
        void write_row(byte* bytes)
        {
            if (setjmp(png_jmpbuf(m_png)))
            {
                throw error(m_error);
            }
            png_write_row(m_png, bytes);
        }

        /**
         * \brief Reads ending info about PNG image.
         */
        void write_end_info() const
        {
            if (setjmp(png_jmpbuf(m_png)))
            {
                throw error(m_error);
            }
            m_end_info.write();
        }

    private:
        static void write_data(png_struct* png, byte* data, png_size_t length)
        {
            io_base* io = static_cast< io_base* >(png_get_error_ptr(png));
            writer* wr = static_cast< writer* >(io);
            wr->reset_error();
            ostream* stream = reinterpret_cast< ostream* >(png_get_io_ptr(png));
            try
            {
                stream->write(reinterpret_cast< char* >(data), length);
                if (!stream->good())
                {
                    wr->set_error("ostream::write() failed");
                }
            }
            catch (std::exception const& error)
            {
                wr->set_error(error.what());
            }
            catch (...)
            {
                assert(!"caught something wrong");
                wr->set_error("write_data: caught something wrong");
            }
            if (wr->is_error())
            {
                wr->raise_error();
            }
        }

        static void flush_data(png_struct* png)
        {
            io_base* io = static_cast< io_base* >(png_get_error_ptr(png));
            writer* wr = static_cast< writer* >(io);
            wr->reset_error();
            ostream* stream = reinterpret_cast< ostream* >(png_get_io_ptr(png));
            try
            {
                stream->flush();
                if (!stream->good())
                {
                    wr->set_error("ostream::flush() failed");
                }
            }
            catch (std::exception const& error)
            {
                wr->set_error(error.what());
            }
            catch (...)
            {
                assert(!"caught something wrong");
                wr->set_error("flush_data: caught something wrong");
            }
            if (wr->is_error())
            {
                wr->raise_error();
            }
        }
    };

} // namespace png

#endif // PNGPP_WRITER_HPP_INCLUDED
