//
//  Copyright (c) 2012 Artyom Beilis (Tonkikh)
//
//  Distributed under the Boost Software License, Version 1.0. (See
//  accompanying file LICENSE_1_0.txt or copy at
//  http://www.boost.org/LICENSE_1_0.txt)
//
#ifndef BOOST_NOWIDE_FILEBUF_HPP
#define BOOST_NOWIDE_FILEBUF_HPP

#include <iosfwd>
#include <boost/config.hpp>
#include <boost/nowide/stackstring.hpp>
#include <fstream>
#include <streambuf>
#include <stdio.h>

#ifdef BOOST_MSVC
#  pragma warning(push)
#  pragma warning(disable : 4996 4244 4800)
#endif


namespace boost {
namespace nowide {
#if !defined(BOOST_WINDOWS) && !defined(BOOST_NOWIDE_FSTREAM_TESTS) && !defined(BOOST_NOWIDE_DOXYGEN)
    using std::basic_filebuf;
    using std::filebuf;
#else // Windows
    
    ///
    /// \brief This forward declaration defined the basic_filebuf type.
    ///
    /// it is implemented and specialized for CharType = char, it behaves
    /// implements std::filebuf over standard C I/O
    ///
    template<typename CharType,typename Traits = std::char_traits<CharType> >
    class basic_filebuf;
    
    ///
    /// \brief This is implementation of std::filebuf
    ///
    /// it is implemented and specialized for CharType = char, it behaves
    /// implements std::filebuf over standard C I/O
    ///
    template<>
    class basic_filebuf<char> : public std::basic_streambuf<char> {
    public:
        ///
        /// Creates new filebuf
        ///
        basic_filebuf() : 
            buffer_size_(4),
            buffer_(0),
            file_(0),
            own_(true),
            mode_(std::ios::in | std::ios::out)
        {
            setg(0,0,0);
            setp(0,0);
        }
        
        virtual ~basic_filebuf()
        {
            if(file_) {
                ::fclose(file_);
                file_ = 0;
            }
            if(own_ && buffer_)
                delete [] buffer_;
        }
        
        ///
        /// Same as std::filebuf::open but s is UTF-8 string
        ///
        basic_filebuf *open(std::string const &s,std::ios_base::openmode mode)
        {
            return open(s.c_str(),mode);
        }
        ///
        /// Same as std::filebuf::open but s is UTF-8 string
        ///
        basic_filebuf *open(char const *s,std::ios_base::openmode mode)
        {
            if(file_) {
                sync();
                ::fclose(file_);
                file_ = 0;
            }
            bool ate = bool(mode & std::ios_base::ate);
            if(ate)
                mode = mode ^ std::ios_base::ate;
            wchar_t const *smode = get_mode(mode);
            if(!smode)
                return 0;
            wstackstring name;
            if(!name.convert(s)) 
                return 0;
            #ifdef BOOST_NOWIDE_FSTREAM_TESTS
            FILE *f = ::fopen(s,boost::nowide::convert(smode).c_str());
            #else
            FILE *f = ::_wfopen(name.c_str(),smode);
            #endif
            if(!f)
                return 0;
            if(ate && fseek(f,0,SEEK_END)!=0) {
                fclose(f);
                return 0;
            }
            file_ = f;
            return this;
        }
        ///
        /// Same as std::filebuf::close()
        ///
        basic_filebuf *close()
        {
            bool res = sync() == 0;
            if(file_) {
                if(::fclose(file_)!=0)
                    res = false;
                file_ = 0;
            }
            return res ? this : 0;
        }
        ///
        /// Same as std::filebuf::is_open()
        ///
        bool is_open() const
        {
            return file_ != 0;
        }

    private:
        void make_buffer()
        {
            if(buffer_)
                return;
            if(buffer_size_ > 0) {
                buffer_ = new char [buffer_size_];
                own_ = true;
            }
        }
    protected:
        
        virtual std::streambuf *setbuf(char *s,std::streamsize n)
        {
            if(!buffer_ && n>=0) {
                buffer_ = s;
                buffer_size_ = n;
                own_ = false;
            }
            return this;
        }
        
#ifdef BOOST_NOWIDE_DEBUG_FILEBUF

        void print_buf(char *b,char *p,char *e)
        {
            std::cerr << "-- Is Null: " << (b==0) << std::endl;; 
            if(b==0)
                return;
            if(e != 0)
                std::cerr << "-- Total: " << e - b <<" offset from start " << p - b << std::endl;
            else
                std::cerr << "-- Total: " << p - b << std::endl;
                
            std::cerr << "-- [";
            for(char *ptr = b;ptr<p;ptr++)
                std::cerr << *ptr;
            if(e!=0) {
                std::cerr << "|";
                for(char *ptr = p;ptr<e;ptr++)
                    std::cerr << *ptr;
            }
            std::cerr << "]" << std::endl;
           
        }
        
        void print_state()
        {
            std::cerr << "- Output:" << std::endl;
            print_buf(pbase(),pptr(),0);
            std::cerr << "- Input:" << std::endl;
            print_buf(eback(),gptr(),egptr());
            std::cerr << "- fpos: " << (file_ ? ftell(file_) : -1L) << std::endl;
        }
        
        struct print_guard
        {
            print_guard(basic_filebuf *p,char const *func)
            {
                self = p;
                f=func;
                std::cerr << "In: " << f << std::endl;
                self->print_state();
            }
            ~print_guard()
            {
                std::cerr << "Out: " << f << std::endl;
                self->print_state();
            }
            basic_filebuf *self;
            char const *f;
        };
#else
#endif        
        
        int overflow(int c)
        {
#ifdef BOOST_NOWIDE_DEBUG_FILEBUF
            print_guard g(this,__FUNCTION__);
#endif            
            if(!file_)
                return EOF;
            
            if(fixg() < 0)
                return EOF;

            size_t n = pptr() - pbase();
            if(n > 0) {
                if(::fwrite(pbase(),1,n,file_) < n)
                    return -1;
                fflush(file_);
            }

            if(buffer_size_ > 0) {
                make_buffer();
                setp(buffer_,buffer_+buffer_size_);
                if(c!=EOF)
                    sputc(c);
            }
            else if(c!=EOF) {
                if(::fputc(c,file_)==EOF)
                    return EOF;
                fflush(file_);
            }
            return 0;
        }
        
        
        int sync()
        {
            return overflow(EOF);
        }

        int underflow()
        {
#ifdef BOOST_NOWIDE_DEBUG_FILEBUF
            print_guard g(this,__FUNCTION__);
#endif            
            if(!file_)
                return EOF;
            if(fixp() < 0)
                return EOF;
            if(buffer_size_ == 0) {
                int c = ::fgetc(file_);
                if(c==EOF) {
                    return EOF;
                }
                last_char_ = c;
                setg(&last_char_,&last_char_,&last_char_ + 1);
                return c;
            }
            make_buffer();
            size_t n = ::fread(buffer_,1,buffer_size_,file_);
            setg(buffer_,buffer_,buffer_+n);
            if(n == 0)
                return EOF;
            return std::char_traits<char>::to_int_type(*gptr());
        }

        int pbackfail(int)
        {
            return pubseekoff(-1,std::ios::cur);
        }

        std::streampos seekoff(std::streamoff off,
                            std::ios_base::seekdir seekdir,
                            std::ios_base::openmode /*m*/)
        {
#ifdef BOOST_NOWIDE_DEBUG_FILEBUF
            print_guard g(this,__FUNCTION__);
#endif            
            if(!file_)
                return EOF;
            if(fixp() < 0 || fixg() < 0)
                return EOF;
            if(seekdir == std::ios_base::cur) {
                if( ::fseek(file_,off,SEEK_CUR) < 0)
                    return EOF;
            }
            else if(seekdir == std::ios_base::beg) {
                if( ::fseek(file_,off,SEEK_SET) < 0)
                    return EOF;
            }
            else if(seekdir == std::ios_base::end) {
                if( ::fseek(file_,off,SEEK_END) < 0)
                    return EOF;
            }
            else
                return -1;
            return ftell(file_);
        }
        std::streampos seekpos(std::streampos off,std::ios_base::openmode m)
        {
            return seekoff(std::streamoff(off),std::ios_base::beg,m);
        }
    private:
        int fixg()
        {
            if(gptr()!=egptr()) {
                std::streamsize off = gptr() - egptr();
                setg(0,0,0);
                if(fseek(file_,off,SEEK_CUR) != 0)
                    return -1;
            }
            setg(0,0,0);
            return 0;
        }
        
        int fixp()
        {
            if(pptr()!=0) {
                int r = sync();
                setp(0,0);
                return r;
            }
            return 0;
        }

        void reset(FILE *f = 0)
        {
            sync();
            if(file_) {
                fclose(file_);
                file_ = 0;
            }
            file_ = f;
        }
        
        
        static wchar_t const *get_mode(std::ios_base::openmode mode)
        {
            //
            // done according to n2914 table 106 27.9.1.4
            //

            // note can't use switch case as overload operator can't be used
            // in constant expression
            if(mode == (std::ios_base::out))
                return L"w";
            if(mode == (std::ios_base::out | std::ios_base::app))
                return L"a";
            if(mode == (std::ios_base::app))
                return L"a";
            if(mode == (std::ios_base::out | std::ios_base::trunc))
                return L"w";
            if(mode == (std::ios_base::in))
                return L"r";
            if(mode == (std::ios_base::in | std::ios_base::out))
                return L"r+";
            if(mode == (std::ios_base::in | std::ios_base::out | std::ios_base::trunc))
                return L"w+";
            if(mode == (std::ios_base::in | std::ios_base::out | std::ios_base::app))
                return L"a+";
            if(mode == (std::ios_base::in | std::ios_base::app))
                return L"a+";
            if(mode == (std::ios_base::binary | std::ios_base::out))
                return L"wb";
            if(mode == (std::ios_base::binary | std::ios_base::out | std::ios_base::app))
                return L"ab";
            if(mode == (std::ios_base::binary | std::ios_base::app))
                return L"ab";
            if(mode == (std::ios_base::binary | std::ios_base::out | std::ios_base::trunc))
                return L"wb";
            if(mode == (std::ios_base::binary | std::ios_base::in))
                return L"rb";
            if(mode == (std::ios_base::binary | std::ios_base::in | std::ios_base::out))
                return L"r+b";
            if(mode == (std::ios_base::binary | std::ios_base::in | std::ios_base::out | std::ios_base::trunc))
                return L"w+b";
            if(mode == (std::ios_base::binary | std::ios_base::in | std::ios_base::out | std::ios_base::app))
                return L"a+b";
            if(mode == (std::ios_base::binary | std::ios_base::in | std::ios_base::app))
                return L"a+b";
            return 0;    
        }
        
        size_t buffer_size_;
        char *buffer_;
        FILE *file_;
        bool own_;
        char last_char_;
        std::ios::openmode mode_;
    };
    
    ///
    /// \brief Convinience typedef
    ///
    typedef basic_filebuf<char> filebuf;
    
    #endif // windows
    
} // nowide
} // namespace boost

#ifdef BOOST_MSVC
#  pragma warning(pop)
#endif


#endif

// vim: tabstop=4 expandtab shiftwidth=4 softtabstop=4
