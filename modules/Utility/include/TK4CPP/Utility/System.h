/**
 * @file System.h
 * @brief Queries about the machine the process is running on.
 * @since 0.1.0
 */

#pragma once

#include "TK4CPP/Utility/Export.h"

namespace TK4CPP
{
	namespace Utility
	{
		/**
		 * @class System
		 * @brief Provides system utility functions.
		 *
		 * @note The platform headers (<tt>windows.h</tt>, <tt>sys/sysinfo.h</tt>, ...) are
		 *       deliberately kept out of this header and only included by the implementation.
		 */
		class TK4CPP_UTILITY_EXPORT System
		{
		public:
			/**
			 * @brief Gets the amount of memory that is available for new allocations.
			 *
			 * On Linux this is <tt>MemAvailable</tt> from <tt>/proc/meminfo</tt> (falling back to
			 * <tt>MemFree</tt> on very old kernels), on Windows <tt>ullAvailPhys</tt>, on macOS the
			 * free + inactive pages.
			 *
			 * @return The amount of available memory in bytes, or 0 if it cannot be determined.
			 */
			static unsigned long long freeMemory();

			/**
			 * @brief Gets the number of physical CPU cores available on the system.
			 *
			 * @return The number of physical CPU cores, or the number of logical cores if the
			 *         physical count cannot be determined.
			 */
			static unsigned int physicalCores();

			/**
			 * @brief Gets the number of logical CPU cores available on the system.
			 *
			 * @return The number of logical CPU cores (hardware threads).
			 */
			static unsigned int logicalCores();
		};
	}
}
