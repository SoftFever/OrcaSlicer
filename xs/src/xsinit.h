#ifndef _xsinit_h_
#define _xsinit_h_

#ifdef _MSC_VER
// Disable some obnoxious warnings given by Visual Studio with the default warning level 4.
#pragma warning(disable: 4100 4127 4189 4244 4267 4700 4702 4800)
#endif

// undef some macros set by Perl which cause compilation errors on Win32
#undef read
#undef seekdir
#undef bind
#undef send
#undef connect
#undef wait
#undef accept
#undef close
#undef open
#undef write
#undef socket
#undef listen
#undef shutdown
#undef ioctl
#undef getpeername
#undef rect
#undef setsockopt
#undef getsockopt
#undef getsockname
#undef gethostname
#undef select
#undef socketpair
#undef recvfrom
#undef sendto
#undef pause

// these need to be included early for Win32 (listing it in Build.PL is not enough)
#include <ostream>
#include <iostream>
#include <sstream>
#include <libslic3r.h>

#ifdef SLIC3RXS
extern "C" {
#include "EXTERN.h"
#include "perl.h"
#include "XSUB.h"
#include "ppport.h"
#undef do_open
#undef do_close
#undef bind
#undef seed
#undef push
#undef pop
#ifdef _MSC_VER
    // Undef some of the macros set by Perl <xsinit.h>, which cause compilation errors on Win32
    #undef connect
    #undef link
    #undef unlink
    #undef seek
    #undef send
    #undef write
    #undef open
    #undef close
    #undef seekdir
    #undef setbuf
    #undef fread
    #undef fseek
    #undef fputc
    #undef fwrite
    #undef fclose
#endif /* _MSC_VER */
}
#endif

#include <ClipperUtils.hpp>
#include <Config.hpp>
#include <ExPolygon.hpp>
#include <MultiPoint.hpp>
#include <Point.hpp>
#include <Polygon.hpp>
#include <Polyline.hpp>
#include <TriangleMesh.hpp>

namespace Slic3r {
    
template<class T>
struct ClassTraits { 
    static const char* name;
    static const char* name_ref; 
};

// use this for typedefs for which the forward prototype
// in REGISTER_CLASS won't work
#define __REGISTER_CLASS(cname, perlname)                                            \
    template <>const char* ClassTraits<cname>::name = "Slic3r::" perlname;           \
    template <>const char* ClassTraits<cname>::name_ref = "Slic3r::" perlname "::Ref"; 

#define REGISTER_CLASS(cname,perlname)                                               \
    class cname;                                                                     \
    __REGISTER_CLASS(cname, perlname);

template<class T>
const char* perl_class_name(const T*) { return ClassTraits<T>::name; }
template<class T>
const char* perl_class_name_ref(const T*) { return ClassTraits<T>::name_ref; }

template<class T>
SV* perl_to_SV_ref(T &t) {
    SV* sv = newSV(0);
    sv_setref_pv( sv, perl_class_name_ref(&t), &t );
    return sv;
}

template<class T>
SV* perl_to_SV_clone_ref(const T &t) {
    SV* sv = newSV(0);
    sv_setref_pv( sv, perl_class_name(&t), new T(t) );
    return sv;
}

template <class T> 
class Ref {
    T* val;
public:
    Ref() : val(NULL) {}
    Ref(T* t) : val(t) {}
    Ref(const T* t) : val(const_cast<T*>(t)) {}
    operator T*() const { return val; }
    static const char* CLASS() { return ClassTraits<T>::name_ref; }
};
  
template <class T>
class Clone {
    T* val;
public:
    Clone() : val(NULL) {}
    Clone(T* t) : val(new T(*t)) {}
    Clone(const T& t) : val(new T(t)) {}
    operator T*() const { return val; }
    static const char* CLASS() { return ClassTraits<T>::name; }
};

SV* ConfigBase__as_hash(ConfigBase* THIS);
SV* ConfigOption_to_SV(const ConfigOption &opt, const ConfigOptionDef &def);
SV* ConfigBase__get(ConfigBase* THIS, const t_config_option_key &opt_key);
SV* ConfigBase__get_at(ConfigBase* THIS, const t_config_option_key &opt_key, size_t i);
bool ConfigBase__set(ConfigBase* THIS, const t_config_option_key &opt_key, SV* value);
bool ConfigBase__set_deserialize(ConfigBase* THIS, const t_config_option_key &opt_key, SV* str);
void ConfigBase__set_ifndef(ConfigBase* THIS, const t_config_option_key &opt_key, SV* value, bool deserialize = false);
bool StaticConfig__set(StaticConfig* THIS, const t_config_option_key &opt_key, SV* value);
SV* to_AV(ExPolygon* expolygon);
SV* to_SV_pureperl(const ExPolygon* expolygon);
void from_SV(SV* expoly_sv, ExPolygon* expolygon);
void from_SV_check(SV* expoly_sv, ExPolygon* expolygon);
void from_SV(SV* line_sv, Line* THIS);
void from_SV_check(SV* line_sv, Line* THIS);
SV* to_AV(Line* THIS);
SV* to_SV_pureperl(const Line* THIS);
void from_SV(SV* poly_sv, MultiPoint* THIS);
void from_SV_check(SV* poly_sv, MultiPoint* THIS);
SV* to_AV(MultiPoint* THIS);
SV* to_SV_pureperl(const MultiPoint* THIS);
void from_SV_check(SV* poly_sv, Polygon* THIS);
void from_SV_check(SV* poly_sv, Polyline* THIS);
SV* to_SV_pureperl(const Point* THIS);
void from_SV(SV* point_sv, Point* point);
void from_SV_check(SV* point_sv, Point* point);
SV* to_SV_pureperl(const Pointf* point);
bool from_SV(SV* point_sv, Pointf* point);
bool from_SV_check(SV* point_sv, Pointf* point);
void from_SV_check(SV* surface_sv, Surface* THIS);
SV* to_SV(TriangleMesh* THIS);

}

#ifdef SLIC3R_HAS_BROKEN_CROAK
#undef croak
#ifdef _MSC_VER
    #define croak(...) confess_at(__FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)
#else
    #define croak(...) confess_at(__FILE__, __LINE__, __func__, __VA_ARGS__)
#endif
#endif

// Defined in wxPerlIface.cpp
// Return a pointer to the associated wxWidgets object instance given by classname.
extern void* wxPli_sv_2_object( pTHX_ SV* scalar, const char* classname );

using namespace Slic3r;

#endif
