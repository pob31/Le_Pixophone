// The single translation unit that compiles the sokol_gfx implementation.
// The backend macro (SOKOL_D3D11 / SOKOL_METAL / SOKOL_GLCORE) comes from
// CMake, per platform (D4). On macOS this file will need to become
// Objective-C++ (.mm) for the Metal backend (P5).

#define SOKOL_IMPL
#include "sokol_gfx.h"
