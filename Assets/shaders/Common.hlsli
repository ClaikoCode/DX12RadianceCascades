#pragma once

#define NO_SECOND_UV 1

SamplerState defaultSampler : register(s10);

#define SAMPLE_TEX(texName, uv) texName.Sample(defaultSampler, uv)
