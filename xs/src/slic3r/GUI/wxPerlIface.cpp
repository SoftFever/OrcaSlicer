// Derived from the following:

/////////////////////////////////////////////////////////////////////////////
// Name:        cpp/helpers.cpp
// Purpose:     implementation for helpers.h
// Author:      Mattia Barbon
// Modified by:
// Created:     29/10/2000
// RCS-ID:      $Id: helpers.cpp 3397 2012-09-30 02:26:07Z mdootson $
// Copyright:   (c) 2000-2011 Mattia Barbon
// Licence:     This program is free software; you can redistribute it and/or
//              modify it under the same terms as Perl itself
/////////////////////////////////////////////////////////////////////////////

#ifdef __cplusplus
extern "C" {
#endif
#include "EXTERN.h"
#include "perl.h"
#include "XSUB.h"
#include "ppport.h"
#undef do_open
#undef do_close
#ifdef __cplusplus
}
#endif

//#include <xsinit.h>

// ----------------------------------------------------------------------------
// Utility functions for working with MAGIC
// ----------------------------------------------------------------------------

struct my_magic
{
    my_magic() : object( NULL ), deleteable( true ) { }

    void*      object;
    bool       deleteable;
};

//STATIC MGVTBL my_vtbl = { 0, 0, 0, 0, 0, 0, 0, 0 };

my_magic* wxPli_get_magic( pTHX_ SV* rv )
{
    // check for reference
    if( !SvROK( rv ) )
        return NULL;
    SV* ref = SvRV( rv );

    // if it isn't a SvPVMG, then it can't have MAGIC
    // so it is deleteable
    if( !ref || SvTYPE( ref ) < SVt_PVMG )
        return NULL;

    // search for '~' / PERL_MAGIC_ext magic, and check the value
//    MAGIC* magic = mg_findext( ref, PERL_MAGIC_ext, &my_vtbl );
    MAGIC* magic = mg_find( ref, '~' );
    if( !magic )
        return NULL;

    return (my_magic*)magic->mg_ptr;
}

// gets 'this' pointer from a blessed scalar/hash reference
void* wxPli_sv_2_object( pTHX_ SV* scalar, const char* classname ) 
{
    // is it correct to use undef as 'NULL'?
    if( !SvOK( scalar ) ) 
    {
        return NULL;
    }

    if( !SvROK( scalar ) )
        croak( "variable is not an object: it must have type %s", classname );

    if( !classname || sv_derived_from( scalar, (char*) classname ) ) 
    {
        SV* ref = SvRV( scalar );

        my_magic* mg = wxPli_get_magic( aTHX_ scalar );

        // rationale: if this is an hash-ish object, it always
        // has both mg and mg->object; if however this is a
        // scalar-ish object that has been marked/unmarked deletable
        // it has mg, but not mg->object
        if( !mg || !mg->object )
            return INT2PTR( void*, SvOK( ref ) ? SvIV( ref ) : 0 );

        return mg->object;
    }
    else 
    {
        croak( "variable is not of type %s", classname );
        return NULL; // dummy, for compiler
    }
}
