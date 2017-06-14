//
//  Copyright (c) 2012 Artyom Beilis (Tonkikh)
//
//  Distributed under the Boost Software License, Version 1.0. (See
//  accompanying file LICENSE_1_0.txt or copy at
//  http://www.boost.org/LICENSE_1_0.txt)
//
#ifndef BOOST_NOWIDE_DETAILS_WIDESTR_H_INCLUDED
#define BOOST_NOWIDE_DETAILS_WIDESTR_H_INCLUDED
#include <boost/nowide/convert.hpp>
#include <string.h>
#include <algorithm>

namespace boost {
namespace nowide {

///
/// \brief A class that allows to create a temporary wide or narrow UTF strings from
/// wide or narrow UTF source.
///
/// It uses on stack buffer of the string is short enough
/// and allocated a buffer on the heap if the size of the buffer is too small
///    
template<typename CharOut=wchar_t,typename CharIn = char,size_t BufferSize = 256>
class basic_stackstring {
public:
   
    static const size_t buffer_size = BufferSize; 
    typedef CharOut output_char;
    typedef CharIn input_char;

    basic_stackstring(basic_stackstring const &other) : 
    mem_buffer_(0)
    {
        clear();
        if(other.mem_buffer_) {
            size_t len = 0;
            while(other.mem_buffer_[len])
                len ++;
            mem_buffer_ = new output_char[len + 1];
            memcpy(mem_buffer_,other.mem_buffer_,sizeof(output_char) * (len+1));
        }
        else {
            memcpy(buffer_,other.buffer_,buffer_size * sizeof(output_char));
        }
    }
    
    void swap(basic_stackstring &other)
    {
        std::swap(mem_buffer_,other.mem_buffer_);
        for(size_t i=0;i<buffer_size;i++)
            std::swap(buffer_[i],other.buffer_[i]);
    }
    basic_stackstring &operator=(basic_stackstring const &other)
    {
        if(this != &other) {
            basic_stackstring tmp(other);
            swap(tmp);            
        }
        return *this;
    }

    basic_stackstring() : mem_buffer_(0)
    {
    }
    bool convert(input_char const *input)
    {
        return convert(input,details::basic_strend(input));
    }
    bool convert(input_char const *begin,input_char const *end)
    {
        clear();

        size_t space = get_space(sizeof(input_char),sizeof(output_char),end - begin) + 1;
        if(space <= buffer_size) {
            if(basic_convert(buffer_,buffer_size,begin,end))
                return true;
            clear();
            return false;
        }
        else {
            mem_buffer_ = new output_char[space];
            if(!basic_convert(mem_buffer_,space,begin,end)) {
                clear();
                return false;
            }
            return true;
        }

    }
    output_char *c_str()
    {
        if(mem_buffer_)
            return mem_buffer_;
        return buffer_;
    }
    output_char const *c_str() const
    {
        if(mem_buffer_)
            return mem_buffer_;
        return buffer_;
    }
    void clear()
    {
        if(mem_buffer_) {
            delete [] mem_buffer_;
            mem_buffer_=0;
        }
        buffer_[0] = 0;
    }
    ~basic_stackstring()
    {
        clear();
    }
private:
    static size_t get_space(size_t insize,size_t outsize,size_t in)
    {
        if(insize <= outsize)
            return in;
        else if(insize == 2 && outsize == 1) 
            return 3 * in;
        else if(insize == 4 && outsize == 1) 
            return 4 * in;
        else  // if(insize == 4 && outsize == 2) 
            return 2 * in;
    }
    output_char buffer_[buffer_size];
    output_char *mem_buffer_;
};  //basic_stackstring

///
/// Convinience typedef
///
typedef basic_stackstring<wchar_t,char,256> wstackstring;
///
/// Convinience typedef
///
typedef basic_stackstring<char,wchar_t,256> stackstring;
///
/// Convinience typedef
///
typedef basic_stackstring<wchar_t,char,16> wshort_stackstring;
///
/// Convinience typedef
///
typedef basic_stackstring<char,wchar_t,16> short_stackstring;


} // nowide
} // namespace boost

#endif
///
// vim: tabstop=4 expandtab shiftwidth=4 softtabstop=4
