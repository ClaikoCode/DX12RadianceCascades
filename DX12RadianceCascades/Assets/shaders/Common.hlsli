#pragma once

#define NO_SECOND_UV 1

#define EPSILON 1.0e-5
#define MATH_PI 3.1415926535897932384f
#define MATH_TAU (MATH_PI * 2.0f)

#define OUT_OF_BOUNDS_RELATIVE(relPos) (relPos.x >= 1.0f || relPos.x < 0.0f || relPos.y >= 1.0f || relPos.x < 0.0f)
#define OUT_OF_BOUNDS(pos, bounds) (pos.x >= bounds.x || pos.x < 0 || pos.y >= bounds.y || pos.y < 0)

