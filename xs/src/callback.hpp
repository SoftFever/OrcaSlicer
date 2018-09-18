#ifndef slic3r_PerlCallback_hpp_
#define slic3r_PerlCallback_hpp_

#include <locale>

#include "libslic3r.h"

namespace Slic3r {

class PerlCallback {
public:
    PerlCallback(void *sv) : m_callback(nullptr) { this->register_callback(sv); }
    PerlCallback() : m_callback(nullptr) {}
    ~PerlCallback() { this->deregister_callback(); }
    void register_callback(void *sv);
    void deregister_callback();
    void call() const;
    void call(int i) const;
    void call(int i, int j) const;
    void call(const std::vector<int>& ints) const;
    void call(double a) const;
    void call(double a, double b) const;
    void call(double a, double b, double c) const;
    void call(double a, double b, double c, double d) const;
    void call(bool b) const;
private:
    void *m_callback;
};

} // namespace Slic3r

#endif /* slic3r_PerlCallback_hpp_ */
