#include "TK4CPP/Utility/Version.h"

#include <tuple>

using namespace TK4CPP::Utility;

std::shared_ptr<Version> Version::create(int major, int minor, int patch, int tweak)
{
	return std::shared_ptr<Version>(new Version(major, minor, patch, tweak));
}

std::strong_ordering Version::operator<=>(const Version& other) const
{
	return std::tie(m_major, m_minor, m_patch, m_tweak)
	   <=> std::tie(other.m_major, other.m_minor, other.m_patch, other.m_tweak);
}

bool Version::operator==(const Version& other) const
{
	return std::tie(m_major, m_minor, m_patch, m_tweak)
	    == std::tie(other.m_major, other.m_minor, other.m_patch, other.m_tweak);
}

std::strong_ordering Version::operator<=>(const std::shared_ptr<Version>& other) const
{
	return *this <=> *other;
}

int Version::getMajor() const { return m_major; }
int Version::getMinor() const { return m_minor; }
int Version::getPatch() const { return m_patch; }
int Version::getTweak() const { return m_tweak; }

Version::Version(int major, int minor, int patch, int tweak) : m_major(major), m_minor(minor), m_patch(patch), m_tweak(tweak) {}
