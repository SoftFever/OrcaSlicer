//
//  Copyright (c) 2012 Artyom Beilis (Tonkikh)
//
//  Distributed under the Boost Software License, Version 1.0. (See
//  accompanying file LICENSE_1_0.txt or copy at
//  http://www.boost.org/LICENSE_1_0.txt)
//
#ifndef BOOST_NOWIDE_FSTREAM_INCLUDED_HPP
#define BOOST_NOWIDE_FSTREAM_INCLUDED_HPP

//#include <iosfwd>
#include <boost/config.hpp>
#include <boost/nowide/convert.hpp>
#include <boost/scoped_ptr.hpp>
#include <fstream>
#include <memory>
#include <boost/nowide/filebuf.hpp>

namespace boost {
///
/// \brief This namespace includes implementation of the standard library functios
/// such that they accept UTF-8 strings on Windows. On other platforms it is just an alias
/// of std namespace (i.e. not on Windows)
///
namespace nowide {
#if !defined(BOOST_WINDOWS)  && !defined(BOOST_NOWIDE_FSTREAM_TESTS) && !defined(BOOST_NOWIDE_DOXYGEN)

    using std::basic_ifstream;
    using std::basic_ofstream;
    using std::basic_fstream;
    using std::ifstream;
    using std::ofstream;
    using std::fstream;

#else
    ///
    /// \brief Same as std::basic_ifstream<char> but accepts UTF-8 strings under Windows
    ///
    template<typename CharType,typename Traits = std::char_traits<CharType> >
    class basic_ifstream : public std::basic_istream<CharType,Traits>
    {
    public:
        typedef basic_filebuf<CharType,Traits> internal_buffer_type;
        typedef std::basic_istream<CharType,Traits> internal_stream_type;

        basic_ifstream() : 
            internal_stream_type(0)
        {
            buf_.reset(new internal_buffer_type());
            std::ios::rdbuf(buf_.get());
        }
        
        explicit basic_ifstream(char const *file_name,std::ios_base::openmode mode = std::ios_base::in) : 
            internal_stream_type(0) 
        {
            buf_.reset(new internal_buffer_type());
            std::ios::rdbuf(buf_.get());
            open(file_name,mode);
        }

        explicit basic_ifstream(std::string const &file_name,std::ios_base::openmode mode = std::ios_base::in) : 
            internal_stream_type(0) 
        {
            buf_.reset(new internal_buffer_type());
            std::ios::rdbuf(buf_.get());
            open(file_name,mode);
        }

        
        void open(std::string const &file_name,std::ios_base::openmode mode = std::ios_base::in)
        {
            open(file_name.c_str(),mode);
        }
        void open(char const *file_name,std::ios_base::openmode mode = std::ios_base::in)
        {
            if(!buf_->open(file_name,mode | std::ios_base::in)) {
                this->setstate(std::ios_base::failbit);
            }
            else {
                this->clear();
            }
        }
        bool is_open()
        {
            return buf_->is_open();
        }
        bool is_open() const
        {
            return buf_->is_open();
        }
        void close()
        {
            if(!buf_->close())
                this->setstate(std::ios_base::failbit);
            else
                this->clear();
        }

        internal_buffer_type *rdbuf() const
        {
            return buf_.get();
        }
        ~basic_ifstream()
        {
            buf_->close();
        }
            
    private:
        boost::scoped_ptr<internal_buffer_type> buf_;
    };

    ///
    /// \brief Same as std::basic_ofstream<char> but accepts UTF-8 strings under Windows
    ///

    template<typename CharType,typename Traits = std::char_traits<CharType> >
    class basic_ofstream : public std::basic_ostream<CharType,Traits>
    {
    public:
        typedef basic_filebuf<CharType,Traits> internal_buffer_type;
        typedef std::basic_ostream<CharType,Traits> internal_stream_type;

        basic_ofstream() : 
            internal_stream_type(0)
        {
            buf_.reset(new internal_buffer_type());
            std::ios::rdbuf(buf_.get());
        }
        explicit basic_ofstream(char const *file_name,std::ios_base::openmode mode = std::ios_base::out) :
            internal_stream_type(0)
        {
            buf_.reset(new internal_buffer_type());
            std::ios::rdbuf(buf_.get());
            open(file_name,mode);
        }
        explicit basic_ofstream(std::string const &file_name,std::ios_base::openmode mode = std::ios_base::out) :
            internal_stream_type(0)
        {
            buf_.reset(new internal_buffer_type());
            std::ios::rdbuf(buf_.get());
            open(file_name,mode);
        }
        void open(std::string const &file_name,std::ios_base::openmode mode = std::ios_base::out)
        {
            open(file_name.c_str(),mode);
        }
        void open(char const *file_name,std::ios_base::openmode mode = std::ios_base::out)
        {
            if(!buf_->open(file_name,mode | std::ios_base::out)) {
                this->setstate(std::ios_base::failbit);
            }
            else {
                this->clear();
            }
        }
        bool is_open()
        {
            return buf_->is_open();
        }
        bool is_open() const
        {
            return buf_->is_open();
        }
        void close()
        {
            if(!buf_->close())
                this->setstate(std::ios_base::failbit);
            else
                this->clear();
        }

        internal_buffer_type *rdbuf() const
        {
            return buf_.get();
        }
        ~basic_ofstream()
        {
            buf_->close();
        }
            
    private:
        boost::scoped_ptr<internal_buffer_type> buf_;
    };

    ///
    /// \brief Same as std::basic_fstream<char> but accepts UTF-8 strings under Windows
    ///

    template<typename CharType,typename Traits = std::char_traits<CharType> >
    class basic_fstream : public std::basic_iostream<CharType,Traits>
    {
    public:
        typedef basic_filebuf<CharType,Traits> internal_buffer_type;
        typedef std::basic_iostream<CharType,Traits> internal_stream_type;

        basic_fstream() : 
            internal_stream_type(0)
        {
            buf_.reset(new internal_buffer_type());
            std::ios::rdbuf(buf_.get());
        }
        explicit basic_fstream(char const *file_name,std::ios_base::openmode mode = std::ios_base::out | std::ios_base::in) :
            internal_stream_type(0)
        {
            buf_.reset(new internal_buffer_type());
            std::ios::rdbuf(buf_.get());
            open(file_name,mode);
        }
        explicit basic_fstream(std::string const &file_name,std::ios_base::openmode mode = std::ios_base::out | std::ios_base::in) :
            internal_stream_type(0)
        {
            buf_.reset(new internal_buffer_type());
            std::ios::rdbuf(buf_.get());
            open(file_name,mode);
        }
        void open(std::string const &file_name,std::ios_base::openmode mode = std::ios_base::out | std::ios_base::out)
        {
            open(file_name.c_str(),mode);
        }
        void open(char const *file_name,std::ios_base::openmode mode = std::ios_base::out | std::ios_base::out)
        {
            if(!buf_->open(file_name,mode)) {
                this->setstate(std::ios_base::failbit);
            }
            else {
                this->clear();
            }
        }
        bool is_open()
        {
            return buf_->is_open();
        }
        bool is_open() const
        {
            return buf_->is_open();
        }
        void close()
        {
            if(!buf_->close())
                this->setstate(std::ios_base::failbit);
            else
                this->clear();
        }

        internal_buffer_type *rdbuf() const
        {
            return buf_.get();
        }
        ~basic_fstream()
        {
            buf_->close();
        }
            
    private:
        boost::scoped_ptr<internal_buffer_type> buf_;
    };


    ///
    /// \brief Same as std::filebuf but accepts UTF-8 strings under Windows
    ///
    typedef basic_filebuf<char> filebuf;
    ///
    /// Same as std::ifstream but accepts UTF-8 strings under Windows
    ///
    typedef basic_ifstream<char> ifstream;
    ///
    /// Same as std::ofstream but accepts UTF-8 strings under Windows
    ///
    typedef basic_ofstream<char> ofstream;
    ///
    /// Same as std::fstream but accepts UTF-8 strings under Windows
    ///
    typedef basic_fstream<char> fstream;

#endif
} // nowide
} // namespace boost



#endif
// vim: tabstop=4 expandtab shiftwidth=4 softtabstop=4
