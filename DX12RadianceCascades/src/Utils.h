#pragma once

#include "Core\VectorMath.h"

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