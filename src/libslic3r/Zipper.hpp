#ifndef ZIPPER_HPP
#define ZIPPER_HPP

#include <string>
#include <memory>

namespace Slic3r {

class Zipper {
public:
    enum e_compression {
        NO_COMPRESSION,
        FAST_COMPRESSION,
        TIGHT_COMPRESSION
    };

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
    std::string m_data;
    std::string m_entry;
    e_compression m_compression;
    std::string m_zipname;

public:

    // Will blow up in a runtime exception if the file cannot be created.
    explicit Zipper(const std::string& zipfname,
                    e_compression level = NO_COMPRESSION);
    ~Zipper();

    Zipper(const Zipper&) = delete;
    Zipper& operator=(const Zipper&) = delete;

    Zipper(Zipper&&) = default;
    Zipper& operator=(Zipper&&) = default;

    void add_entry(const std::string& name);
    void finish_entry();

    inline Zipper& operator<<(const std::string& content) {
        std::copy(content.begin(), content.end(), std::back_inserter(m_data));
        return *this;
    }

    std::string get_name() const;
};

}

#endif // ZIPPER_HPP
