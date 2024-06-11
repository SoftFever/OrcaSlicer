#ifndef PCHIPINTERPOLATORHELPER_HPP
#define PCHIPINTERPOLATORHELPER_HPP

#include <vector>

class PchipInterpolatorHelper {
public:
    PchipInterpolatorHelper() = default;
    PchipInterpolatorHelper(const std::vector<double>& x, const std::vector<double>& y);
    void setData(const std::vector<double>& x, const std::vector<double>& y);
    double interpolate(double xi) const;

private:
    std::vector<double> x_;
    std::vector<double> y_;
    std::vector<double> h_;
    std::vector<double> delta_;
    std::vector<double> d_;
    void computePCHIP();
    void sortData();

    double h(int i) const { return x_[i+1] - x_[i]; }
    double delta(int i) const { return (y_[i+1] - y_[i]) / h(i); }
};

#endif // PCHIPINTERPOLATORHELPER_HPP
