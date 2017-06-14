//
//  Copyright (c) 2012 Artyom Beilis (Tonkikh)
//
//  Distributed under the Boost Software License, Version 1.0. (See
//  accompanying file LICENSE_1_0.txt or copy at
//  http://www.boost.org/LICENSE_1_0.txt)
//
#ifndef BOOST_NOWIDE_INTEGRATION_FILESYSTEM_HPP_INCLUDED
#define BOOST_NOWIDE_INTEGRATION_FILESYSTEM_HPP_INCLUDED

#include <boost/filesystem/path.hpp>
#include <boost/nowide/utf8_codecvt.hpp>
namespace boost {
    namespace nowide {
        ///
        /// Instal utf8_codecvt facet into  boost::filesystem::path such all char strings are interpreted as utf-8 strings
        ///
        inline void nowide_filesystem()
        {
            std::locale tmp = std::locale(std::locale(),new boost::nowide::utf8_codecvt<wchar_t>());
            boost::filesystem::path::imbue(tmp);
        }
    } // nowide
} // boost

#endif
///
// vim: tabstop=4 expandtab shiftwidth=4 softtabstop=4
