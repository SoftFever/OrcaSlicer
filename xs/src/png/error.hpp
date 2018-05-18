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
#ifndef PNGPP_ERROR_HPP_INCLUDED
#define PNGPP_ERROR_HPP_INCLUDED

/* check if we have strerror_s or strerror_r, prefer the former which is C11 std */
#ifdef __STDC_LIB_EXT1__
#define __STDC_WANT_LIB_EXT1__ 1
#include <string.h>

#define HAVE_STRERROR_S 1
#else
#undef  HAVE_STRERROR_S
#endif

#include <string>
#include <stdexcept>
#include <cerrno>
#include <cstdlib>
#include <cstring>

namespace png
{

    /**
     * \brief Exception class to represent runtime errors related to
     * png++ operation.
     */
    class error
        : public std::runtime_error
    {
    public:
        /**
         * \param  message  error description
         */
        explicit error(std::string const& message)
            : std::runtime_error(message)
        {
        }
    };

    /**
     * \brief Exception class to represent standard library errors
     * (generally IO).
     *
     * \see  reader, writer
     */
    class std_error
        : public std::runtime_error
    {
    public:
        /**
         * Constructs an std_error object.  The \a message string is
         * appended with <tt>": "</tt> and the error description as
         * returned by \c strerror(\a error).
         *
         * \param  message  error description
         * \param  error    error number
         */
        explicit std_error(std::string const& message, int errnum = errno)
            : std::runtime_error((message + ": ") + thread_safe_strerror(errnum))
        {
        }

    protected:
        static std::string thread_safe_strerror(int errnum)
        {
#define ERRBUF_SIZE 512
            char buf[ERRBUF_SIZE] = { 0 };

#ifdef HAVE_STRERROR_S
            strerror_s(buf, ERRBUF_SIZE, errnum);
            return std::string(buf);
#else
#if (_POSIX_C_SOURCE >= 200112L || _XOPEN_SOURCE >= 600) && !_GNU_SOURCE
            strerror_r(errnum, buf, ERRBUF_SIZE);
            return std::string(buf);
#else
            /* GNU variant can return a pointer to static buffer instead of buf */
            return std::string(strerror_r(errnum, buf, ERRBUF_SIZE));
#endif
#endif

#undef ERRBUF_SIZE
        }
    };

} // namespace png

#endif // PNGPP_ERROR_HPP_INCLUDED
