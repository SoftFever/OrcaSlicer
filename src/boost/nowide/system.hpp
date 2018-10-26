//
//  Copyright (c) 2012 Artyom Beilis (Tonkikh)
//
//  Distributed under the Boost Software License, Version 1.0. (See
//  accompanying file LICENSE_1_0.txt or copy at
//  http://www.boost.org/LICENSE_1_0.txt)
//
#ifndef BOOST_NOWIDE_CSTDLIB_HPP
#define BOOST_NOWIDE_CSTDLIB_HPP

#include <stdlib.h>
#include <errno.h>
#include <boost/nowide/stackstring.hpp>
namespace boost {
namespace nowide {

#if !defined(BOOST_WINDOWS) && !defined(BOOST_NOWIDE_DOXYGEN)

using ::system;

#else // Windows

///
/// Same as std::system but cmd is UTF-8.
///
/// If the input is not valid UTF-8, -1 returned and errno set to EINVAL
///
inline int system(char const *cmd)
{
    if(!cmd)
        return _wsystem(0);
    wstackstring wcmd;
    if(!wcmd.convert(cmd)) {
        errno = EINVAL;
        return -1;
    }
    return _wsystem(wcmd.c_str());
}

#endif
} // nowide
} // namespace boost

#endif
///
// vim: tabstop=4 expandtab shiftwidth=4 softtabstop=4
