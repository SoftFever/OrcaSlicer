//
//  Copyright (c) 2012 Artyom Beilis (Tonkikh)
//
//  Distributed under the Boost Software License, Version 1.0. (See
//  accompanying file LICENSE_1_0.txt or copy at
//  http://www.boost.org/LICENSE_1_0.txt)
//
#ifndef BOOST_NOWIDE_CSTDIO_H_INCLUDED
#define BOOST_NOWIDE_CSTDIO_H_INCLUDED

#include <cstdio>
#include <stdio.h>
#include <boost/config.hpp>
#include <boost/nowide/convert.hpp>
#include <boost/nowide/stackstring.hpp>
#include <errno.h>

#ifdef BOOST_MSVC
#  pragma warning(push)
#  pragma warning(disable : 4996)
#endif


namespace boost {
namespace nowide {
#if !defined(BOOST_WINDOWS) && !defined(BOOST_NOWIDE_DOXYGEN)
    using std::fopen;
    using std::freopen;
    using std::remove;
    using std::rename;
#else

///
/// \brief Same as freopen but file_name and mode are UTF-8 strings
///
/// If invalid UTF-8 given, NULL is returned and errno is set to EINVAL
///
inline FILE *freopen(char const *file_name,char const *mode,FILE *stream)
{
    wstackstring wname;
    wshort_stackstring wmode;
    if(!wname.convert(file_name) || !wmode.convert(mode)) {
        errno = EINVAL;
        return 0;
    }
    return _wfreopen(wname.c_str(),wmode.c_str(),stream);
}
///
/// \brief Same as fopen but file_name and mode are UTF-8 strings
///
/// If invalid UTF-8 given, NULL is returned and errno is set to EINVAL
///
inline FILE *fopen(char const *file_name,char const *mode)
{
    wstackstring wname;
    wshort_stackstring wmode;
    if(!wname.convert(file_name) || !wmode.convert(mode)) {
        errno = EINVAL;
        return 0;
    }
    return _wfopen(wname.c_str(),wmode.c_str());
}
///
/// \brief Same as rename but old_name and new_name are UTF-8 strings
///
/// If invalid UTF-8 given, -1 is returned and errno is set to EINVAL
///
inline int rename(char const *old_name,char const *new_name)
{
    wstackstring wold,wnew;
    if(!wold.convert(old_name) || !wnew.convert(new_name)) {
        errno = EINVAL;
        return -1;
    }
    return _wrename(wold.c_str(),wnew.c_str());
}
///
/// \brief Same as rename but name is UTF-8 string
///
/// If invalid UTF-8 given, -1 is returned and errno is set to EINVAL
///
inline int remove(char const *name)
{
    wstackstring wname;
    if(!wname.convert(name)) {
        errno = EINVAL;
        return -1;
    }
    return _wremove(wname.c_str());
}
#endif
} // nowide
} // namespace boost

#ifdef BOOST_MSVC
#pragma warning(pop)
#endif

#endif
///
// vim: tabstop=4 expandtab shiftwidth=4 softtabstop=4
