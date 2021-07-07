#include "LocalesUtils.hpp"

#include <stdexcept>

namespace Slic3r {


CNumericLocalesSetter::CNumericLocalesSetter()
{
#ifdef _WIN32
    _configthreadlocale(_ENABLE_PER_THREAD_LOCALE);
    m_orig_numeric_locale = std::setlocale(LC_NUMERIC, nullptr);
    std::setlocale(LC_NUMERIC, "C");
#elif __linux__
    m_original_locale = uselocale((locale_t)0);
    m_new_locale = duplocale(m_original_locale);
    m_new_locale = newlocale(LC_NUMERIC_MASK, "C", m_new_locale);
    uselocale(m_new_locale);
#else // APPLE
    m_original_locale = uselocale((locale_t)0);
    m_new_locale = newlocale(LC_NUMERIC_MASK, "C", m_original_locale);
    uselocale(m_new_locale);
#endif
}



CNumericLocalesSetter::~CNumericLocalesSetter()
{
#ifdef _WIN32
    std::setlocale(LC_NUMERIC, m_orig_numeric_locale.data());
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


double string_to_double_decimal_point(const std::string& str, size_t* pos /* = nullptr*/)
{
    double out;
    std::istringstream stream(str);
    if (! (stream >> out))
        throw std::invalid_argument("string_to_double_decimal_point conversion failed.");
    if (pos) {
        if (stream.eof())
            *pos = str.size();
        else
            *pos = stream.tellg();
    }
    return out;
}

std::string float_to_string_decimal_point(double value, int precision/* = -1*/)
{
    std::stringstream buf;
    if (precision >= 0)
        buf << std::fixed << std::setprecision(precision);
    buf << value;
    return buf.str();
}


} // namespace Slic3r

