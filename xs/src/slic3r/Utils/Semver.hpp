#ifndef slic3r_Semver_hpp_
#define slic3r_Semver_hpp_

#include <string>
#include <cstring>
#include <boost/optional.hpp>
#include <boost/format.hpp>

#include "semver/semver.h"

namespace Slic3r {


class Semver
{
public:
	struct Major { const int i;  Major(int i) : i(i) {} };
	struct Minor { const int i;  Minor(int i) : i(i) {} };
	struct Patch { const int i;  Patch(int i) : i(i) {} };

	static boost::optional<Semver> parse(const std::string &str)
	{
		semver_t ver;
		if (::semver_parse(str.c_str(), &ver) == 0) {
			return Semver(ver);
		} else {
			return boost::none;
		}
	}

	static const Semver zero()
	{
		static semver_t ver = { 0, 0, 0, nullptr, nullptr };
		return Semver(ver);
	}

	static const Semver inf()
	{
		static semver_t ver = { std::numeric_limits<int>::max(), std::numeric_limits<int>::max(), std::numeric_limits<int>::max(), nullptr, nullptr };
		return Semver(ver);
	}

	static const Semver invalid()
	{
		static semver_t ver = { -1, 0, 0, nullptr, nullptr };
		return Semver(ver);
	}

	Semver(Semver &&other) { *this = std::move(other); }
	Semver(const Semver &other) { *this = other; }

	Semver &operator=(Semver &&other)
	{
		ver = other.ver;
		other.ver.major = other.ver.minor = other.ver.patch = 0;
		other.ver.metadata = other.ver.prerelease = nullptr;
		return *this;
	}

	Semver &operator=(const Semver &other)
	{
		::semver_free(&ver);
		ver = other.ver;
		if (other.ver.metadata != nullptr) { std::strcpy(ver.metadata, other.ver.metadata); }
		if (other.ver.prerelease != nullptr) { std::strcpy(ver.prerelease, other.ver.prerelease); }
		return *this;
	}

	~Semver() { ::semver_free(&ver); }

	// Comparison
	bool operator<(const Semver &b)  const { return ::semver_compare(ver, b.ver) == -1; }
	bool operator<=(const Semver &b) const { return ::semver_compare(ver, b.ver) <= 0; }
	bool operator==(const Semver &b) const { return ::semver_compare(ver, b.ver) == 0; }
	bool operator!=(const Semver &b) const { return ::semver_compare(ver, b.ver) != 0; }
	bool operator>=(const Semver &b) const { return ::semver_compare(ver, b.ver) >= 0; }
	bool operator>(const Semver &b)  const { return ::semver_compare(ver, b.ver) == 1; }
	// We're using '&' instead of the '~' operator here as '~' is unary-only:
	// Satisfies patch if Major and minor are equal.
	bool operator&(const Semver &b) const { return ::semver_satisfies_patch(ver, b.ver); }
	bool operator^(const Semver &b) const { return ::semver_satisfies_caret(ver, b.ver); }
	bool in_range(const Semver &low, const Semver &high) const { return low <= *this && *this <= high; }

	// Conversion
	std::string to_string() const {
		auto res = (boost::format("%1%.%2%.%3%") % ver.major % ver.minor % ver.patch).str();
		if (ver.prerelease != nullptr) { res += '-'; res += ver.prerelease; }
		if (ver.metadata != nullptr)   { res += '+'; res += ver.metadata; }
		return res;
	}

	// Arithmetics
	Semver& operator+=(const Major &b) { ver.major += b.i; return *this; }
	Semver& operator+=(const Minor &b) { ver.minor += b.i; return *this; }
	Semver& operator+=(const Patch &b) { ver.patch += b.i; return *this; }
	Semver& operator-=(const Major &b) { ver.major -= b.i; return *this; }
	Semver& operator-=(const Minor &b) { ver.minor -= b.i; return *this; }
	Semver& operator-=(const Patch &b) { ver.patch -= b.i; return *this; }
	Semver operator+(const Major &b) const { Semver res(*this); return res += b; }
	Semver operator+(const Minor &b) const { Semver res(*this); return res += b; }
	Semver operator+(const Patch &b) const { Semver res(*this); return res += b; }
	Semver operator-(const Major &b) const { Semver res(*this); return res -= b; }
	Semver operator-(const Minor &b) const { Semver res(*this); return res -= b; }
	Semver operator-(const Patch &b) const { Semver res(*this); return res -= b; }

private:
	semver_t ver;

	Semver(semver_t ver) : ver(ver) {}
};


}
#endif
