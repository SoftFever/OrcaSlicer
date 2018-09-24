#include "callback.hpp"

#include <xsinit.h>

void PerlCallback::register_callback(void *sv)
{ 
    if (! SvROK((SV*)sv) || SvTYPE(SvRV((SV*)sv)) != SVt_PVCV)
        croak("Not a Callback %_ for PerlFunction", (SV*)sv);
    if (m_callback)
        SvSetSV((SV*)m_callback, (SV*)sv);
    else
        m_callback = newSVsv((SV*)sv);
}

void PerlCallback::deregister_callback()
{
	if (m_callback) {
		sv_2mortal((SV*)m_callback);
		m_callback = nullptr;
	}
}

void PerlCallback::call() const
{
    if (! m_callback)
        return;
    dSP;
    ENTER;
    SAVETMPS;
    PUSHMARK(SP);
    PUTBACK; 
    perl_call_sv(SvRV((SV*)m_callback), G_DISCARD);
    FREETMPS;
    LEAVE;
}

void PerlCallback::call(int i) const
{
    if (! m_callback)
        return;
    dSP;
    ENTER;
    SAVETMPS;
    PUSHMARK(SP);
    XPUSHs(sv_2mortal(newSViv(i)));
    PUTBACK; 
    perl_call_sv(SvRV((SV*)m_callback), G_DISCARD);
    FREETMPS;
    LEAVE;
}

void PerlCallback::call(int i, int j) const
{
    if (! m_callback)
        return;
    dSP;
    ENTER;
    SAVETMPS;
    PUSHMARK(SP);
    XPUSHs(sv_2mortal(newSViv(i)));
    XPUSHs(sv_2mortal(newSViv(j)));
    PUTBACK; 
    perl_call_sv(SvRV((SV*)m_callback), G_DISCARD);
    FREETMPS;
    LEAVE;
}

void PerlCallback::call(const std::vector<int>& ints) const
{
    if (! m_callback)
        return;
    dSP;
    ENTER;
    SAVETMPS;
    PUSHMARK(SP);
    for (int i : ints)
    {
        XPUSHs(sv_2mortal(newSViv(i)));
    }
    PUTBACK;
    perl_call_sv(SvRV((SV*)m_callback), G_DISCARD);
    FREETMPS;
    LEAVE;
}

void PerlCallback::call(double a) const
{
    if (!m_callback)
        return;
    dSP;
    ENTER;
    SAVETMPS;
    PUSHMARK(SP);
    XPUSHs(sv_2mortal(newSVnv(a)));
    PUTBACK;
    perl_call_sv(SvRV((SV*)m_callback), G_DISCARD);
    FREETMPS;
    LEAVE;
}

void PerlCallback::call(double a, double b) const
{
    if (!m_callback)
        return;
    dSP;
    ENTER;
    SAVETMPS;
    PUSHMARK(SP);
    XPUSHs(sv_2mortal(newSVnv(a)));
    XPUSHs(sv_2mortal(newSVnv(b)));
    PUTBACK;
    perl_call_sv(SvRV((SV*)m_callback), G_DISCARD);
    FREETMPS;
    LEAVE;
}

void PerlCallback::call(double a, double b, double c) const
{
    if (!m_callback)
        return;
    dSP;
    ENTER;
    SAVETMPS;
    PUSHMARK(SP);
    XPUSHs(sv_2mortal(newSVnv(a)));
    XPUSHs(sv_2mortal(newSVnv(b)));
    XPUSHs(sv_2mortal(newSVnv(c)));
    PUTBACK;
    perl_call_sv(SvRV((SV*)m_callback), G_DISCARD);
    FREETMPS;
    LEAVE;
}

void PerlCallback::call(double a, double b, double c, double d) const
{
    if (!m_callback)
        return;
    dSP;
    ENTER;
    SAVETMPS;
    PUSHMARK(SP);
    XPUSHs(sv_2mortal(newSVnv(a)));
    XPUSHs(sv_2mortal(newSVnv(b)));
    XPUSHs(sv_2mortal(newSVnv(c)));
    XPUSHs(sv_2mortal(newSVnv(d)));
    PUTBACK;
    perl_call_sv(SvRV((SV*)m_callback), G_DISCARD);
    FREETMPS;
    LEAVE;
}

void PerlCallback::call(double a, double b, double c, double d, double e, double f) const
{
    if (!m_callback)
        return;
    dSP;
    ENTER;
    SAVETMPS;
    PUSHMARK(SP);
    XPUSHs(sv_2mortal(newSVnv(a)));
    XPUSHs(sv_2mortal(newSVnv(b)));
    XPUSHs(sv_2mortal(newSVnv(c)));
    XPUSHs(sv_2mortal(newSVnv(d)));
    XPUSHs(sv_2mortal(newSVnv(e)));
    XPUSHs(sv_2mortal(newSVnv(f)));
    PUTBACK;
    perl_call_sv(SvRV((SV*)m_callback), G_DISCARD);
    FREETMPS;
    LEAVE;
}

void PerlCallback::call(bool b) const
{
    call(b ? 1 : 0);
}