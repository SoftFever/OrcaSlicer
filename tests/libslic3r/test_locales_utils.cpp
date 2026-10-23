#include <catch2/catch_all.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/catch_message.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "libslic3r/LocalesUtils.hpp"
#include "test_utils.hpp"

#include <cstdlib>
#include <stdlib.h>
#include <locale>
#include <locale.h>
#include <string>
#include <utility>
#include <vector>

using namespace Slic3r;

TEST_CASE("Floats print as printf prints them in the C locale", "[LocalesUtils]")
{
    const auto [value, precision, text] = GENERATE(table<double, int, std::string>({
        {0.5, -1, "0.5"},
        {25. / 3., -1, "8.33333"},
        {1500.5, -1, "1500.5"},
        {1e6, -1, "1e+06"},
        {-0.000123, -1, "-0.000123"},
        {25. / 3., 3, "8.333"},
        {2., 0, "2"},
        // Longer than the to_chars buffer.
        {1e21, 2, "1000000000000000000000.00"},
    }));
    CHECK(float_to_string_decimal_point(value, precision) == text);
}

TEST_CASE("Floats print with a decimal point in a locale whose decimal separator is a comma", "[LocalesUtils]")
{
    CommaNumericLocale comma;
    {
        CNumericLocalesSetter outer;
        if (! comma.apply())
            SKIP("no locale with a comma decimal separator is installed");
        CHECK(float_to_string_decimal_point(1500.5) == "1500.5");
        CHECK(float_to_string_decimal_point(25. / 3., 3) == "8.333");
    }
}

TEST_CASE("a setter nested in another leaves the C locale in place for the outer one", "[LocalesUtils]")
{
    CNumericLocalesSetter outer;
    {
        CNumericLocalesSetter inner;
        CHECK(is_decimal_separator_point());
    }
    CHECK(is_decimal_separator_point());
}

TEST_CASE("a setter nested in another sets C again when the locale changed between them", "[LocalesUtils]")
{
    CommaNumericLocale comma;
    {
        CNumericLocalesSetter outer;
        if (! comma.apply())
            SKIP("no locale with a comma decimal separator is installed");
        REQUIRE_FALSE(is_decimal_separator_point());
        {
            CNumericLocalesSetter inner;
            CHECK(is_decimal_separator_point());
        }
        CHECK_FALSE(is_decimal_separator_point());
    }
}

TEST_CASE("atof_decimal_point parses what atof parses in the C locale", "[LocalesUtils]")
{
    const auto [text, value] = GENERATE(table<const char*, double>({
        { "5", 5. },
        { "  12.5", 12.5 },
        { "\t+3", 3. },
        { "\r\n7", 7. },
        { "-1.25", -1.25 },
        { "1e2", 100. },
        { ".5", 0.5 },
        { "12.5;comment", 12.5 },
        { "+-5", 0. },
        { "", 0. },
        { "abc", 0. },
    }));
    INFO(text);
    CHECK_THAT(atof_decimal_point(text), Catch::Matchers::WithinAbs(value, 1e-12));
}

#if !defined(_WIN32) && !defined(__APPLE__)
namespace {

// Sets or unsets an environment variable and puts the previous value back.
class ScopedEnv
{
public:
    ScopedEnv(const char *name, const char *value) : m_name(name)
    {
        if (const char *old = std::getenv(name); old != nullptr) {
            m_had_value = true;
            m_old       = old;
        }
        set(value);
    }
    ~ScopedEnv() { set(m_had_value ? m_old.c_str() : nullptr); }

private:
    void set(const char *value)
    {
        if (value == nullptr)
            unsetenv(m_name.c_str());
        else
            setenv(m_name.c_str(), value, 1);
    }

    std::string m_name;
    bool        m_had_value{false};
    std::string m_old;
};

using LocaleVariables = std::vector<std::pair<std::string, std::string>>;

// A locale no system ships, so the environment names one glibc cannot load.
constexpr const char *unloadable_locale = "xx_YY.UTF-8";

// Unsets LC_ALL and every category variable, so only what a test sets counts.
class ClearedLocaleVariables
{
public:
    ClearedLocaleVariables()
    {
        for (const char *var : {"LC_ALL", "LC_CTYPE", "LC_NUMERIC", "LC_TIME", "LC_COLLATE", "LC_MONETARY", "LC_MESSAGES",
                                "LC_PAPER", "LC_NAME", "LC_ADDRESS", "LC_TELEPHONE", "LC_MEASUREMENT", "LC_IDENTIFICATION"})
            m_cleared.emplace_back(var, nullptr);
    }

private:
    std::vector<ScopedEnv> m_cleared;
};

// musl accepts any locale name, so there the environment can never be unloadable.
void skip_unless_locale_names_are_checked()
{
    if (locale_t loc = newlocale(LC_ALL_MASK, unloadable_locale, locale_t(0)); loc != locale_t(0)) {
        freelocale(loc);
        SKIP("this C library accepts any locale name");
    }
}

} // namespace

TEST_CASE("a loadable locale environment reports nothing", "[LocalesUtils]")
{
    ScopedEnv lc_all("LC_ALL", "C");
    CHECK(unloadable_locale_variables().empty());
}

TEST_CASE("the category variable naming an unloadable locale is reported", "[LocalesUtils]")
{
    skip_unless_locale_names_are_checked();
    ClearedLocaleVariables cleared;
    ScopedEnv lang("LANG", "C");
    ScopedEnv lc_time("LC_TIME", "C");
    ScopedEnv lc_monetary("LC_MONETARY", unloadable_locale);
    CHECK_THROWS(std::locale(""));
    CHECK(unloadable_locale_variables() == LocaleVariables{{"LC_MONETARY", unloadable_locale}});
}

TEST_CASE("an unloadable LC_ALL is reported alone since it overrides the rest", "[LocalesUtils]")
{
    skip_unless_locale_names_are_checked();
    ClearedLocaleVariables cleared;
    ScopedEnv lc_all("LC_ALL", unloadable_locale);
    ScopedEnv lang("LANG", "C");
    ScopedEnv lc_monetary("LC_MONETARY", unloadable_locale);
    CHECK_THROWS(std::locale(""));
    CHECK(unloadable_locale_variables() == LocaleVariables{{"LC_ALL", unloadable_locale}});
}

TEST_CASE("an unloadable LANG is reported once for all the categories it covers", "[LocalesUtils]")
{
    skip_unless_locale_names_are_checked();
    ClearedLocaleVariables cleared;
    ScopedEnv lc_time("LC_TIME", "C");
    ScopedEnv lang("LANG", unloadable_locale);
    CHECK_THROWS(std::locale(""));
    CHECK(unloadable_locale_variables() == LocaleVariables{{"LANG", unloadable_locale}});
}
#endif
