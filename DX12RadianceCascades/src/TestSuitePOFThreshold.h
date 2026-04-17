#pragma once

#include "TestSuite.h"
#include "Core\Math\Vector.h"

#include <vector>


class TestSuitePOFThreshold : public TestSuite
{
	static constexpr size_t sRandSeed = 128937621386478912ull;
	static constexpr size_t sNumTests = 10000ull;

	struct Input
	{
		bool usePOF;
		uint32_t transformIndex;
	};

	struct Output
	{
		std::vector<float> frameTimes = std::vector<float>(sNumTests);
		std::vector<float> filteredRayRatios = std::vector<float>(sNumTests);
	};
};
