//
//  Copyright (c) 2012 Artyom Beilis (Tonkikh)
//
//  Distributed under the Boost Software License, Version 1.0. (See
//  accompanying file LICENSE_1_0.txt or copy at
//  http://www.boost.org/LICENSE_1_0.txt)
//
#ifndef BOOST_NOWIDE_CONFIG_HPP_INCLUDED
#define BOOST_NOWIDE_CONFIG_HPP_INCLUDED

#include <boost/config.hpp>

#ifndef BOOST_SYMBOL_VISIBLE
# define BOOST_SYMBOL_VISIBLE
#endif

#ifdef BOOST_HAS_DECLSPEC 
#   if defined(BOOST_ALL_DYN_LINK) || defined(BOOST_NOWIDE_DYN_LINK)
#       ifdef BOOST_NOWIDE_SOURCE
#           define BOOST_NOWIDE_DECL BOOST_SYMBOL_EXPORT
#       else
#           define BOOST_NOWIDE_DECL BOOST_SYMBOL_IMPORT
#       endif  // BOOST_NOWIDE_SOURCE
#   endif  // DYN_LINK
#endif  // BOOST_HAS_DECLSPEC

#ifndef BOOST_NOWIDE_DECL
#   define BOOST_NOWIDE_DECL
#endif

//
// Automatically link to the correct build variant where possible. 
// 
#if !defined(BOOST_ALL_NO_LIB) && !defined(BOOST_NOWIDE_NO_LIB) && !defined(BOOST_NOWIDE_SOURCE)
//
// Set the name of our library, this will get undef'ed by auto_link.hpp
// once it's done with it:
//
#define BOOST_LIB_NAME boost_nowide
//
// If we're importing code from a dll, then tell auto_link.hpp about it:
//
#if defined(BOOST_ALL_DYN_LINK) || defined(BOOST_NOWIDE_DYN_LINK)
#  define BOOST_DYN_LINK
#endif
//
// And include the header that does the work:
//
#include <boost/config/auto_link.hpp>
#endif  // auto-linking disabled


#endif // boost/nowide/config.hpp
// vim: tabstop=4 expandtab shiftwidth=4 softtabstop=4