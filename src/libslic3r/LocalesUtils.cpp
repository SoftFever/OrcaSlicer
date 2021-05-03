#include "LocalesUtils.hpp"


namespace Slic3r {


CNumericLocalesSetter::CNumericLocalesSetter()
{
#ifdef _WIN32
    _configthreadlocale(_ENABLE_PER_THREAD_LOCALE);
    m_orig_numeric_locale = std::setlocale(LC_NUMERIC, nullptr);
    std::setlocale(LC_NUMERIC, "C");
#else
    m_original_locale = uselocale((locale_t)0);
    m_new_locale = duplocale(m_original_locale);
    m_new_locale = newlocale(LC_NUMERIC_MASK, "C", m_new_locale);
    uselocale(m_new_locale);
#endif
}



CNumericLocalesSetter::~CNumericLocalesSetter()
{
#ifdef _WIN32
    std::setlocale(LC_NUMERIC, m_orig_numeric_locale)
#else
    uselocale(m_original_locale);
    freelocale(m_new_locale);
#endif
}



bool is_decimal_separator_point()
{
    char str[5] = "";
    sprintf(str, "%.1f", 0.5f);
    return str[1] == '.';
}

std::string float_to_string_decimal_point(double value, int precision/* = -1*/)
{
    assert(is_decimal_separator_point());
    std::stringstream buf;
    if (precision >= 0)
        buf << std::fixed << std::setprecision(precision);
    buf << value;
    return buf.str();
}

std::string float_to_string_decimal_point(float value, int precision/* = -1*/)
{
    return float_to_string_decimal_point(double(value), precision);
}


} // namespace Slic3r

