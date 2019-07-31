//
//  Copyright (c) 2015 Artyom Beilis (Tonkikh)
//
//  Distributed under the Boost Software License, Version 1.0. (See
//  accompanying file LICENSE_1_0.txt or copy at
//  http://www.boost.org/LICENSE_1_0.txt)
//
#ifndef BOOST_NOWIDE_UTF8_CODECVT_HPP
#define BOOST_NOWIDE_UTF8_CODECVT_HPP

#include <boost/locale/utf.hpp>
#include <boost/cstdint.hpp>
#include <boost/static_assert.hpp>
#include <locale>

namespace boost {
namespace nowide {

//
// Make sure that mbstate can keep 16 bit of UTF-16 sequence
//
BOOST_STATIC_ASSERT(sizeof(std::mbstate_t)>=2);

#ifdef _MSC_VER
// MSVC do_length is non-standard it counts wide characters instead of narrow and does not change mbstate
#define BOOST_NOWIDE_DO_LENGTH_MBSTATE_CONST
#endif

template<typename CharType,int CharSize=sizeof(CharType)>
class utf8_codecvt;

template<typename CharType>
class utf8_codecvt<CharType,2> : public std::codecvt<CharType,char,std::mbstate_t>
{
public:
    utf8_codecvt(size_t refs = 0) : std::codecvt<CharType,char,std::mbstate_t>(refs)
    {
    }
protected:

    typedef CharType uchar;

    virtual std::codecvt_base::result do_unshift(std::mbstate_t &s,char *from,char * /*to*/,char *&next) const
    {
        boost::uint16_t &state = *reinterpret_cast<boost::uint16_t *>(&s);
#ifdef DEBUG_CODECVT            
        std::cout << "Entering unshift " << std::hex << state << std::dec << std::endl;
#endif            
        if(state != 0)
            return std::codecvt_base::error;
        next=from;
        return std::codecvt_base::ok;
    }
    virtual int do_encoding() const throw()
    {
        return 0;
    }
    virtual int do_max_length() const throw()
    {
        return 4;
    }
    virtual bool do_always_noconv() const throw()
    {
        return false;
    }

    virtual int
    do_length(  std::mbstate_t 
    #ifdef BOOST_NOWIDE_DO_LENGTH_MBSTATE_CONST
            const   
    #endif
            &std_state,
            char const *from,
            char const *from_end,
            size_t max) const
    {
        #ifndef BOOST_NOWIDE_DO_LENGTH_MBSTATE_CONST
        char const *save_from = from;
        boost::uint16_t &state = *reinterpret_cast<boost::uint16_t *>(&std_state);
        #else
        size_t save_max = max;
        boost::uint16_t state = *reinterpret_cast<boost::uint16_t const *>(&std_state);
        #endif
        while(max > 0 && from < from_end){
            char const *prev_from = from;
            boost::uint32_t ch=boost::locale::utf::utf_traits<char>::decode(from,from_end);
            if(ch==boost::locale::utf::incomplete || ch==boost::locale::utf::illegal) {
                from = prev_from;
                break;
            }
            max --;
            if(ch > 0xFFFF) {
                if(state == 0) {
                    from = prev_from;
                    state = 1;
                }
                else {
                    state = 0;
                }
            }        
        }
        #ifndef BOOST_NOWIDE_DO_LENGTH_MBSTATE_CONST
        return from - save_from;
        #else
        return int(save_max - max);
        #endif
    }

    
    virtual std::codecvt_base::result 
    do_in(  std::mbstate_t &std_state,
            char const *from,
            char const *from_end,
            char const *&from_next,
            uchar *to,
            uchar *to_end,
            uchar *&to_next) const
    {
        std::codecvt_base::result r=std::codecvt_base::ok;
        
        // mbstate_t is POD type and should be initialized to 0 (i.a. state = stateT())
        // according to standard. We use it to keep a flag 0/1 for surrogate pair writing
        //
        // if 0 no code above >0xFFFF observed, of 1 a code above 0xFFFF observerd
        // and first pair is written, but no input consumed
        boost::uint16_t &state = *reinterpret_cast<boost::uint16_t *>(&std_state);
        while(to < to_end && from < from_end)
        {
#ifdef DEBUG_CODECVT            
            std::cout << "Entering IN--------------" << std::endl;
            std::cout << "State " << std::hex << state <<std::endl;
            std::cout << "Left in " << std::dec << from_end - from << " out " << to_end -to << std::endl;
#endif           
            char const *from_saved = from;
            
            uint32_t ch=boost::locale::utf::utf_traits<char>::decode(from,from_end);
            
            if(ch==boost::locale::utf::illegal) {
                from = from_saved;
                r=std::codecvt_base::error;
                break;
            }
            if(ch==boost::locale::utf::incomplete) {
                from = from_saved;
                r=std::codecvt_base::partial;
                break;
            }
            // Normal codepoints go direcly to stream
            if(ch <= 0xFFFF) {
                *to++=ch;
            }
            else {
                // for  other codepoints we do following
                //
                // 1. We can't consume our input as we may find ourselfs
                //    in state where all input consumed but not all output written,i.e. only
                //    1st pair is written
                // 2. We only write first pair and mark this in the state, we also revert back
                //    the from pointer in order to make sure this codepoint would be read
                //    once again and then we would consume our input together with writing
                //    second surrogate pair
                ch-=0x10000;
                boost::uint16_t vh = ch >> 10;
                boost::uint16_t vl = ch & 0x3FF;
                boost::uint16_t w1 = vh + 0xD800;
                boost::uint16_t w2 = vl + 0xDC00;
                if(state == 0) {
                    from = from_saved;
                    *to++ = w1;
                    state = 1;
                }
                else {
                    *to++ = w2;
                    state = 0;
                }
            }
        }
        from_next=from;
        to_next=to;
        if(r == std::codecvt_base::ok && (from!=from_end || state!=0))
            r = std::codecvt_base::partial;
#ifdef DEBUG_CODECVT            
        std::cout << "Returning ";
        switch(r) {
        case std::codecvt_base::ok:
            std::cout << "ok" << std::endl;
            break;
        case std::codecvt_base::partial:
            std::cout << "partial" << std::endl;
            break;
        case std::codecvt_base::error:
            std::cout << "error" << std::endl;
            break;
        default:
            std::cout << "other" << std::endl;
            break;
        }
        std::cout << "State " << std::hex << state <<std::endl;
        std::cout << "Left in " << std::dec << from_end - from << " out " << to_end -to << std::endl;
#endif            
        return r;
    }
    
    virtual std::codecvt_base::result 
    do_out( std::mbstate_t &std_state,
            uchar const *from,
            uchar const *from_end,
            uchar const *&from_next,
            char *to,
            char *to_end,
            char *&to_next) const
    {
        std::codecvt_base::result r=std::codecvt_base::ok;
        // mbstate_t is POD type and should be initialized to 0 (i.a. state = stateT())
        // according to standard. We assume that sizeof(mbstate_t) >=2 in order
        // to be able to store first observerd surrogate pair
        //
        // State: state!=0 - a first surrogate pair was observerd (state = first pair),
        // we expect the second one to come and then zero the state
        ///
        boost::uint16_t &state = *reinterpret_cast<boost::uint16_t *>(&std_state);
        while(to < to_end && from < from_end)
        {
#ifdef DEBUG_CODECVT            
        std::cout << "Entering OUT --------------" << std::endl;
        std::cout << "State " << std::hex << state <<std::endl;
        std::cout << "Left in " << std::dec << from_end - from << " out " << to_end -to << std::endl;
#endif            
            boost::uint32_t ch=0;
            if(state != 0) {
                // if the state idecates that 1st surrogate pair was written
                // we should make sure that the second one that comes is actually
                // second surrogate
                boost::uint16_t w1 = state;
                boost::uint16_t w2 = *from; 
                // we don't forward from as writing may fail to incomplete or
                // partial conversion
                if(0xDC00 <= w2 && w2<=0xDFFF) {
                    boost::uint16_t vh = w1 - 0xD800;
                    boost::uint16_t vl = w2 - 0xDC00;
                    ch=((uint32_t(vh) << 10)  | vl) + 0x10000;
                }
                else {
                    // Invalid surrogate
                    r=std::codecvt_base::error;
                    break;
                }
            }
            else {
                ch = *from;
                if(0xD800 <= ch && ch<=0xDBFF) {
                    // if this is a first surrogate pair we put
                    // it into the state and consume it, note we don't
                    // go forward as it should be illegal so we increase
                    // the from pointer manually
                    state = ch;
                    from++;
                    continue;
                }
                else if(0xDC00 <= ch && ch<=0xDFFF) {
                    // if we observe second surrogate pair and 
                    // first only may be expected we should break from the loop with error
                    // as it is illegal input
                    r=std::codecvt_base::error;
                    break;
                }
            }
            if(!boost::locale::utf::is_valid_codepoint(ch)) {
                r=std::codecvt_base::error;
                break;
            }
            int len = boost::locale::utf::utf_traits<char>::width(ch);
            if(to_end - to < len) {
                r=std::codecvt_base::partial;
                break;
            }
            to = boost::locale::utf::utf_traits<char>::encode(ch,to);
            state = 0;
            from++;
        }
        from_next=from;
        to_next=to;
        if(r==std::codecvt_base::ok && from!=from_end)
            r = std::codecvt_base::partial;
#ifdef DEBUG_CODECVT            
        std::cout << "Returning ";
        switch(r) {
        case std::codecvt_base::ok:
            std::cout << "ok" << std::endl;
            break;
        case std::codecvt_base::partial:
            std::cout << "partial" << std::endl;
            break;
        case std::codecvt_base::error:
            std::cout << "error" << std::endl;
            break;
        default:
            std::cout << "other" << std::endl;
            break;
        }
        std::cout << "State " << std::hex << state <<std::endl;
        std::cout << "Left in " << std::dec << from_end - from << " out " << to_end -to << std::endl;
#endif            
        return r;
    }
    
};

template<typename CharType>
class utf8_codecvt<CharType,4> : public std::codecvt<CharType,char,std::mbstate_t>
{
public:
    utf8_codecvt(size_t refs = 0) : std::codecvt<CharType,char,std::mbstate_t>(refs)
    {
    }
protected:

    typedef CharType uchar;

    virtual std::codecvt_base::result do_unshift(std::mbstate_t &/*s*/,char *from,char * /*to*/,char *&next) const
    {
        next=from;
        return std::codecvt_base::ok;
    }
    virtual int do_encoding() const throw()
    {
        return 0;
    }
    virtual int do_max_length() const throw()
    {
        return 4;
    }
    virtual bool do_always_noconv() const throw()
    {
        return false;
    }

    virtual int
    do_length(  std::mbstate_t 
    #ifdef BOOST_NOWIDE_DO_LENGTH_MBSTATE_CONST
            const   
    #endif    
            &/*state*/,
            char const *from,
            char const *from_end,
            size_t max) const
    {
        #ifndef BOOST_NOWIDE_DO_LENGTH_MBSTATE_CONST 
        char const *start_from = from;
        #else
        size_t save_max = max;
        #endif
        
        while(max > 0 && from < from_end){
            char const *save_from = from;
            boost::uint32_t ch=boost::locale::utf::utf_traits<char>::decode(from,from_end);
            if(ch==boost::locale::utf::incomplete || ch==boost::locale::utf::illegal) {
                from = save_from;
                break;
            }
            max--;
        }
        #ifndef BOOST_NOWIDE_DO_LENGTH_MBSTATE_CONST 
        return from - start_from;
        #else
        return save_max - max;
        #endif
    }

    
    virtual std::codecvt_base::result 
    do_in(  std::mbstate_t &/*state*/,
            char const *from,
            char const *from_end,
            char const *&from_next,
            uchar *to,
            uchar *to_end,
            uchar *&to_next) const
    {
        std::codecvt_base::result r=std::codecvt_base::ok;
        
        // mbstate_t is POD type and should be initialized to 0 (i.a. state = stateT())
        // according to standard. We use it to keep a flag 0/1 for surrogate pair writing
        //
        // if 0 no code above >0xFFFF observed, of 1 a code above 0xFFFF observerd
        // and first pair is written, but no input consumed
        while(to < to_end && from < from_end)
        {
#ifdef DEBUG_CODECVT            
            std::cout << "Entering IN--------------" << std::endl;
            std::cout << "State " << std::hex << state <<std::endl;
            std::cout << "Left in " << std::dec << from_end - from << " out " << to_end -to << std::endl;
#endif           
            char const *from_saved = from;
            
            uint32_t ch=boost::locale::utf::utf_traits<char>::decode(from,from_end);
            
            if(ch==boost::locale::utf::illegal) {
                r=std::codecvt_base::error;
                from = from_saved;
                break;
            }
            if(ch==boost::locale::utf::incomplete) {
                r=std::codecvt_base::partial;
                from=from_saved;
                break;
            }
            *to++=ch;
        }
        from_next=from;
        to_next=to;
        if(r == std::codecvt_base::ok && from!=from_end)
            r = std::codecvt_base::partial;
#ifdef DEBUG_CODECVT            
        std::cout << "Returning ";
        switch(r) {
        case std::codecvt_base::ok:
            std::cout << "ok" << std::endl;
            break;
        case std::codecvt_base::partial:
            std::cout << "partial" << std::endl;
            break;
        case std::codecvt_base::error:
            std::cout << "error" << std::endl;
            break;
        default:
            std::cout << "other" << std::endl;
            break;
        }
        std::cout << "State " << std::hex << state <<std::endl;
        std::cout << "Left in " << std::dec << from_end - from << " out " << to_end -to << std::endl;
#endif            
        return r;
    }
    
    virtual std::codecvt_base::result 
    do_out( std::mbstate_t &std_state,
            uchar const *from,
            uchar const *from_end,
            uchar const *&from_next,
            char *to,
            char *to_end,
            char *&to_next) const
    {
        std::codecvt_base::result r=std::codecvt_base::ok;
        while(to < to_end && from < from_end)
        {
#ifdef DEBUG_CODECVT            
        std::cout << "Entering OUT --------------" << std::endl;
        std::cout << "State " << std::hex << state <<std::endl;
        std::cout << "Left in " << std::dec << from_end - from << " out " << to_end -to << std::endl;
#endif            
            boost::uint32_t ch=0;
            ch = *from;
            if(!boost::locale::utf::is_valid_codepoint(ch)) {
                r=std::codecvt_base::error;
                break;
            }
            int len = boost::locale::utf::utf_traits<char>::width(ch);
            if(to_end - to < len) {
                r=std::codecvt_base::partial;
                break;
            }
            to = boost::locale::utf::utf_traits<char>::encode(ch,to);
            from++;
        }
        from_next=from;
        to_next=to;
        if(r==std::codecvt_base::ok && from!=from_end)
            r = std::codecvt_base::partial;
#ifdef DEBUG_CODECVT            
        std::cout << "Returning ";
        switch(r) {
        case std::codecvt_base::ok:
            std::cout << "ok" << std::endl;
            break;
        case std::codecvt_base::partial:
            std::cout << "partial" << std::endl;
            break;
        case std::codecvt_base::error:
            std::cout << "error" << std::endl;
            break;
        default:
            std::cout << "other" << std::endl;
            break;
        }
        std::cout << "State " << std::hex << state <<std::endl;
        std::cout << "Left in " << std::dec << from_end - from << " out " << to_end -to << std::endl;
#endif            
        return r;
    }
};

} // nowide
} // namespace boost

#endif
///
// vim: tabstop=4 expandtab shiftwidth=4 softtabstop=4
