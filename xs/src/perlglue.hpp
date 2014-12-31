#ifndef slic3r_perlglue_hpp_
#define slic3r_perlglue_hpp_

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
};

#endif
