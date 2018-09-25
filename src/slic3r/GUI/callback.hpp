// I AM A PHONY PLACEHOLDER FOR THE PERL CALLBACK.
// GET RID OF ME!

#ifndef slic3r_GUI_PerlCallback_phony_hpp_
#define slic3r_GUI_PerlCallback_phony_hpp_

#include <vector>

namespace Slic3r {

class PerlCallback {
public:
    PerlCallback(void *) {}
    PerlCallback() {}
    void register_callback(void *) {}
    void deregister_callback() {}
    void call() const {}
    void call(int) const {}
    void call(int, int) const {}
    void call(const std::vector<int>&) const {}
    void call(double) const {}
    void call(double, double) const {}
    void call(double, double, double) const {}
    void call(double, double, double, double) const {}
    void call(double, double, double, double, double) const {}
    void call(double, double, double, double, double, double) const {}
    void call(bool b) const {}
};

} // namespace Slic3r

#endif /* slic3r_GUI_PerlCallback_phony_hpp_ */
