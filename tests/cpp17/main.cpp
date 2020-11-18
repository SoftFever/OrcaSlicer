#include <cstdlib>
#include <iostream>
#include <tuple>
#include <string>

// Test new headers in cpp17
#include <variant>
#include <optional>
#include <any>
#include <string_view>

// Test for nested namespace definition
namespace PrusaSlicer::Cpp17 {

template<class T> class Foo
{
    T m_arg;
public:
    
    explicit Foo(T &&arg): m_arg{arg} {}
};

} // namespace PrusaSlicer::Cpp17

template<class T> std::string get_type(const T &v);

template<> std::string get_type(const int &) { return "int"; }
template<> std::string get_type(const double &) { return "double"; }
template<> std::string get_type(const float &) { return "double"; }

int main()
{
    // /////////////////////////////////////////////////////////////////////////
    // Template argument deduction for class templates
    // /////////////////////////////////////////////////////////////////////////
    
    auto foo = PrusaSlicer::Cpp17::Foo{1.f};
    
    // /////////////////////////////////////////////////////////////////////////
    // Structured bindings:
    // /////////////////////////////////////////////////////////////////////////
    
    auto my_tuple = std::make_tuple(0.2, 10);
    
    auto [a, b] = my_tuple;
    
    std::cout << "a is " << get_type(a) << std::endl;
    std::cout << "b is " << get_type(b) << std::endl;
    
    // /////////////////////////////////////////////////////////////////////////
    // Test for std::apply()
    // /////////////////////////////////////////////////////////////////////////
    
    auto fun = [] (auto a, auto b) {
        std::cout << "a (" << get_type(a) << ") = " << a << std::endl;
        std::cout << "b (" << get_type(b) << ") = " << b << std::endl;
    };
    
    std::apply(fun, my_tuple);
    
    // /////////////////////////////////////////////////////////////////////////
    // constexpr lambda and if
    // /////////////////////////////////////////////////////////////////////////
    
    auto isIntegral = [](auto v) constexpr -> bool {
        if constexpr (std::is_integral_v<decltype(v)>) {
            return true;
        } else {
            return false;
        }
    };
    
    static_assert (isIntegral(10), "" );
    // would fail to compile: static_assert (isIntegral(10.0), "" );
    
    std::cout << "Integer is integral: " << isIntegral(0) << std::endl;
    std::cout << "Floating point is not integral: " << isIntegral(0.0) << std::endl;
    
    return EXIT_SUCCESS;
}
