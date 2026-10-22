#include "TK4CPP/Utility/UUIDHandler.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <random>

using namespace TK4CPP;
using namespace TK4CPP::Utility;

namespace
{
	using Bytes = std::array<std::uint8_t, 16>;

	/// Fills @p out[from, to) with random bytes.
	void fillRandom(Bytes& out, std::size_t from, std::size_t to)
	{
		static thread_local std::random_device rd;
		std::size_t i = from;
		while (i < to) {
			std::uint32_t word = rd();
			for (int b = 0; b < 4 && i < to; ++b, word >>= 8)
				out[i++] = static_cast<std::uint8_t>(word & 0xFFu);
		}
	}

	/// Sets the 4-bit version in byte 6 and the RFC variant (10xx) in byte 8.
	void setVersionAndVariant(Bytes& b, std::uint8_t version)
	{
		b[6] = static_cast<std::uint8_t>((b[6] & 0x0Fu) | (version << 4));
		b[8] = static_cast<std::uint8_t>((b[8] & 0x3Fu) | 0x80u);
	}

	std::string toString(const Bytes& b)
	{
		static constexpr char hex[] = "0123456789abcdef";
		std::string s;
		s.reserve(36);
		for (std::size_t i = 0; i < b.size(); ++i) {
			if (i == 4 || i == 6 || i == 8 || i == 10)
				s.push_back('-');
			s.push_back(hex[b[i] >> 4]);
			s.push_back(hex[b[i] & 0x0Fu]);
		}
		return s;
	}

	std::mutex g_stateMutex;
	std::uint64_t g_lastV6Ticks = 0;   // 100 ns ticks since 1582-10-15
	std::uint64_t g_lastV7Millis = 0;  // ms since the Unix epoch
	std::uint16_t g_v7Counter = 0;     // 12-bit counter used within the same millisecond
}

std::string UUIDHandler::Generate_V4()
{
	Bytes b{};
	fillRandom(b, 0, 16);
	setVersionAndVariant(b, 4);
	return toString(b);
}

std::string UUIDHandler::Generate_V6()
{
	using namespace std::chrono;
	// Offset between the Gregorian epoch (1582-10-15) and the Unix epoch, in 100 ns ticks.
	constexpr std::uint64_t gregorianToUnix = 0x01B21DD213814000ULL;

	std::uint64_t ticks;
	{
		const auto now = system_clock::now().time_since_epoch();
		const std::uint64_t unixTicks = static_cast<std::uint64_t>(duration_cast<nanoseconds>(now).count() / 100);
		std::lock_guard<std::mutex> lock(g_stateMutex);
		ticks = unixTicks + gregorianToUnix;
		if (ticks <= g_lastV6Ticks)
			ticks = g_lastV6Ticks + 1;
		g_lastV6Ticks = ticks;
	}

	Bytes b{};
	fillRandom(b, 8, 16);                                   // clock_seq + node
	b[10] = static_cast<std::uint8_t>(b[10] | 0x01u);       // node multicast bit (first node octet = byte 10)

	// time_high (32) | time_mid (16) | version (4) + time_low (12)
	b[0] = static_cast<std::uint8_t>(ticks >> 52);
	b[1] = static_cast<std::uint8_t>(ticks >> 44);
	b[2] = static_cast<std::uint8_t>(ticks >> 36);
	b[3] = static_cast<std::uint8_t>(ticks >> 28);
	b[4] = static_cast<std::uint8_t>(ticks >> 20);
	b[5] = static_cast<std::uint8_t>(ticks >> 12);
	b[6] = static_cast<std::uint8_t>((ticks >> 8) & 0x0Fu);
	b[7] = static_cast<std::uint8_t>(ticks);
	setVersionAndVariant(b, 6);
	return toString(b);
}

std::string UUIDHandler::Generate_V7()
{
	using namespace std::chrono;

	std::uint64_t millis;
	std::uint16_t counter;
	{
		const auto now = system_clock::now().time_since_epoch();
		std::uint64_t current = static_cast<std::uint64_t>(duration_cast<milliseconds>(now).count());
		std::lock_guard<std::mutex> lock(g_stateMutex);
		if (current > g_lastV7Millis) {
			g_lastV7Millis = current;
			g_v7Counter = 0;
		} else if (++g_v7Counter > 0x0FFF) {
			// Counter exhausted inside the same millisecond (or the clock went back): borrow the next one.
			++g_lastV7Millis;
			g_v7Counter = 0;
		}
		millis = g_lastV7Millis;
		counter = g_v7Counter;
	}

	Bytes b{};
	fillRandom(b, 8, 16);
	b[0] = static_cast<std::uint8_t>(millis >> 40);
	b[1] = static_cast<std::uint8_t>(millis >> 32);
	b[2] = static_cast<std::uint8_t>(millis >> 24);
	b[3] = static_cast<std::uint8_t>(millis >> 16);
	b[4] = static_cast<std::uint8_t>(millis >> 8);
	b[5] = static_cast<std::uint8_t>(millis);
	b[6] = static_cast<std::uint8_t>(counter >> 8);   // rand_a holds the monotonic counter
	b[7] = static_cast<std::uint8_t>(counter);
	setVersionAndVariant(b, 7);
	return toString(b);
}
