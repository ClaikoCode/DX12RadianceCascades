#pragma once

#include "Core\VectorMath.h"
#include <locale> // For wstring convert

namespace Math
{
	// Gives back the log of b with base of a.
	static float LogAB(float a, float b)
	{
		return Math::Log(b) / Math::Log(a);
	}

	// Calculates: a + ar^2 + ar^3 + ... + ar^(n - 1)
	static float GeometricSeriesSum(float a, float r, float n)
	{
		return a * (1.0f - Math::Pow(r, n)) / (1.0f - r);
	}
}

namespace Utils
{
	// This string conversion function was written by AI and checked by a human.
	static std::string WstringToString(const std::wstring& wstr) {
		// Initialize the converter for the current locale
		const std::codecvt<wchar_t, char, std::mbstate_t>& codecvt =
			std::use_facet<std::codecvt<wchar_t, char, std::mbstate_t>>(std::locale());

		std::mbstate_t state = std::mbstate_t();
		std::string result(wstr.size() * codecvt.max_length(), '\0');

		const wchar_t* from_next;
		char* to_next;

		// Perform the conversion
		codecvt.out(state,
			wstr.data(), wstr.data() + wstr.size(), from_next,
			result.data(), result.data() + result.size(), to_next);

		// Resize the result to the actual converted size
		result.resize(to_next - result.data());
		return result;
	}

	// This string conversion function was written by AI and checked by a human.
	static std::wstring StringToWstring(const std::string& str) {
		// Initialize the converter for the current locale
		const std::codecvt<wchar_t, char, std::mbstate_t>& codecvt =
			std::use_facet<std::codecvt<wchar_t, char, std::mbstate_t>>(std::locale());

		std::mbstate_t state = std::mbstate_t();
		std::wstring result(str.size(), L'\0');

		const char* from_next;
		wchar_t* to_next;

		// Perform the conversion
		codecvt.in(state,
			str.data(), str.data() + str.size(), from_next,
			result.data(), result.data() + result.size(), to_next);

		// Resize the result to the actual converted size
		result.resize(to_next - result.data());
		return result;
	}
}