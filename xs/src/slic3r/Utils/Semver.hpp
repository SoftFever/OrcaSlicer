#ifndef slic3r_Semver_hpp_
#define slic3r_Semver_hpp_

#include <string>
#include <cstring>
#include <ostream>
#include <stdexcept>
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

	Semver() : ver(semver_zero()) {}

	Semver(int major, int minor, int patch,
		boost::optional<const std::string&> metadata = boost::none,
		boost::optional<const std::string&> prerelease = boost::none)
		: ver(semver_zero())
	{
		ver.major = major;
		ver.minor = minor;
		ver.patch = patch;
		set_metadata(metadata);
		set_prerelease(prerelease);
	}

	Semver(const std::string &str) : ver(semver_zero())
	{
		auto parsed = parse(str);
		if (! parsed) {
			throw std::runtime_error(std::string("Could not parse version string: ") + str);
		}
		ver = parsed->ver;
		parsed->ver = semver_zero();
	}

	static boost::optional<Semver> parse(const std::string &str)
	{
		semver_t ver = semver_zero();
		if (::semver_parse(str.c_str(), &ver) == 0) {
			return Semver(ver);
		} else {
			return boost::none;
		}
	}

	static const Semver zero() { return Semver(semver_zero()); }

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

	Semver(Semver &&other) : ver(other.ver) { other.ver = semver_zero(); }
	Semver(const Semver &other) : ver(::semver_copy(&other.ver)) {}

	Semver &operator=(Semver &&other)
	{
		::semver_free(&ver);
		ver = other.ver;
		other.ver = semver_zero();
		return *this;
	}

	Semver &operator=(const Semver &other)
	{
		::semver_free(&ver);
		ver = ::semver_copy(&other.ver);
		return *this;
	}

	~Semver() { ::semver_free(&ver); }

	// const accessors
	int 		maj()        const { return ver.major; }
	int 		min()        const { return ver.minor; }
	int 		patch() 	 const { return ver.patch; }
	const char*	prerelease() const { return ver.prerelease; }
	const char*	metadata() 	 const { return ver.metadata; }
	
	// Setters
	void set_maj(int maj) { ver.major = maj; }
	void set_min(int min) { ver.minor = min; }
	void set_patch(int patch) { ver.patch = patch; }
	void set_metadata(boost::optional<const std::string&> meta) { ver.metadata = meta ? strdup(*meta) : nullptr; }
	void set_prerelease(boost::optional<const std::string&> pre) { ver.prerelease = pre ? strdup(*pre) : nullptr; }

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

	static semver_t semver_zero() { return { 0, 0, 0, nullptr, nullptr }; }
	static char * strdup(const std::string &str) { return ::semver_strdup(const_cast<char*>(str.c_str())); }
};


}
#endif
