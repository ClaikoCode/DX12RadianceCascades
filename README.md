# Radiance Cascades in DirectX 12 - Master Thesis

This repo contains a solution for real-time global illumination through a novel probe-based approach called Radiance Cascades (RC), introduced by Alexander Sannikov at Grinding Gear Games. The solution leverages hardware accelerated raytracing using DirectX 12. The implementation was built on [Microsoft's MiniEngine demo](https://github.com/microsoft/DirectX-Graphics-Samples/tree/master/MiniEngine), which gave useful as well as open set abstractions for many DX12 resources and usage patterns.

The project was part of my master thesis: 
[Analyzing the Performance Scaling of Radiance Cascades: Exploring a Novel Solution for Dynamic Diffuse Real-Time Global Illumination](https://www.diva-portal.org/smash/record.jsf?pid=diva2:2007296)

## Features

* An extension of MiniEngine for ray tracing pipelines:
    * RTPSO that mirrors MiniEngine's regular PSO abstractions.
    * A BLAS and TLAS builder that follows geometry data structure from MiniEngine's model loader.
* Hardware accelerated raytracing implementation of RC:
    * Probes are placed in world space, with its position calculated from the depth buffer.
    * One invocation of raytracing shader per probe ray for each probe.
    * Simple deferred rendering combined with RC output to solve for global illumination.
* A custom shader compiler using DirectX Shader Compiler DLL for compiling HLSL programs:
    * Watches for file updates on a separate thread.
    * Supports nested includes and keeps track of dependencies between files.
    * Shaders are hot-reloadable, enabling real-time shader code iteration and debugging.
    * Supports shader model 6.0 and higher.
* Interactive GUI (built using Dear ImGui):
    * Control rendering parameters.
    * Show debug views. 
    * Change app settings.
* GPU Profiler:
    * Measures and graphs execution time for specific passes.
    * Collects VRAM initialization costs by scope and presents them hierarchically.

## Getting started

1. Open **RadianceCascadesImpl.sln** (only tested on VS 2022)
2. Select configuration: **Release** or **Debug** (console output, allocation of debug visualization resources, no compiler optimizations)
3. Build solution and run

If you want to change resolution (defaulted to 1080p) find `Display.cpp` and change the preprocessor macro starting with RES_ to the desired resolution. Recompile to apply. 

For higher resolution monitors, it is recommended to enable *larger font scale* from the *global settings* menu in the *UI* section (during runtime).

## Controls

* Camera movement: `WASD keys` (FPS controls)
* Up/down: `E/Q`
* Yaw/pitch: `Mouse`
* Toggle slow movement: `left shift`
* Toggle UI: `U`
* Toggle mouse/camera control: `F`