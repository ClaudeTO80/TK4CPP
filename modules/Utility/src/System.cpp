#include "TK4CPP/Utility/System.h"

#include <cstddef>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <utility>

#if defined(_WIN32)
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#elif defined(__APPLE__)
#  include <mach/mach.h>
#  include <sys/sysctl.h>
#  include <sys/types.h>
#else
#  include <unistd.h>
#endif

using namespace TK4CPP;
using namespace TK4CPP::Utility;

namespace
{
#if defined(__APPLE__)
	/// Reads an integer value through sysctlbyname(); returns 0 on failure.
	unsigned int sysctlUInt(const char* name)
	{
		int value = 0;
		std::size_t size = sizeof(value);
		if (sysctlbyname(name, &value, &size, nullptr, 0) != 0 || value < 0)
			return 0;
		return static_cast<unsigned int>(value);
	}
#elif !defined(_WIN32)
	/// Returns the value (in kB) of a <tt>/proc/meminfo</tt> key such as "MemAvailable", 0 if missing.
	unsigned long long meminfoKb(const std::string& key)
	{
		std::ifstream meminfo("/proc/meminfo");
		std::string line;
		const std::string prefix = key + ":";
		while (std::getline(meminfo, line)) {
			if (line.rfind(prefix, 0) == 0) {
				std::istringstream iss(line.substr(prefix.size()));
				unsigned long long value = 0;
				iss >> value;
				return value;
			}
		}
		return 0;
	}
#endif
}

unsigned long long System::freeMemory()
{
#if defined(_WIN32)
	MEMORYSTATUSEX memInfo;
	memInfo.dwLength = sizeof(MEMORYSTATUSEX);
	if (!GlobalMemoryStatusEx(&memInfo))
		return 0;
	return memInfo.ullAvailPhys;
#elif defined(__APPLE__)
	vm_statistics64_data_t stats;
	mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
	if (host_statistics64(mach_host_self(), HOST_VM_INFO64,
	                      reinterpret_cast<host_info64_t>(&stats), &count) != KERN_SUCCESS)
		return 0;
	vm_size_t pageSize = 0;
	host_page_size(mach_host_self(), &pageSize);
	return (static_cast<unsigned long long>(stats.free_count) + stats.inactive_count) * pageSize;
#else
	unsigned long long kb = meminfoKb("MemAvailable");
	if (kb == 0)
		kb = meminfoKb("MemFree");
	return kb * 1024ULL;
#endif
}

unsigned int System::physicalCores()
{
#if defined(_WIN32)
	DWORD length = 0;
	GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &length);
	if (length == 0)
		return logicalCores();

	std::unique_ptr<std::byte[]> buffer(new std::byte[length]);
	auto* info = reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buffer.get());
	if (!GetLogicalProcessorInformationEx(RelationProcessorCore, info, &length))
		return logicalCores();

	unsigned int cores = 0;
	DWORD offset = 0;
	while (offset < length) {
		const auto* current = reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buffer.get() + offset);
		offset += current->Size;
		++cores;
	}
	return cores != 0 ? cores : logicalCores();
#elif defined(__APPLE__)
	const unsigned int n = sysctlUInt("hw.physicalcpu");
	return n != 0 ? n : logicalCores();
#else
	// Count distinct (physical id, core id) pairs; this is what lscpu does as well.
	std::ifstream cpuinfo("/proc/cpuinfo");
	std::string line;
	std::set<std::pair<int, int>> cores;
	int physicalId = 0;
	while (std::getline(cpuinfo, line)) {
		const auto colon = line.find(':');
		if (colon == std::string::npos)
			continue;
		if (line.rfind("physical id", 0) == 0)
			physicalId = std::atoi(line.c_str() + colon + 1);
		else if (line.rfind("core id", 0) == 0)
			cores.emplace(physicalId, std::atoi(line.c_str() + colon + 1));
	}
	return cores.empty() ? logicalCores() : static_cast<unsigned int>(cores.size());
#endif
}

unsigned int System::logicalCores()
{
	const unsigned int n = std::thread::hardware_concurrency();
	return n != 0 ? n : 1;
}
