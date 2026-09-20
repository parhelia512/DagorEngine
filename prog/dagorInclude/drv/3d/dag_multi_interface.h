//
// Dagor Engine 6.5
// Copyright (C) Gaijin Games KFT.  All rights reserved.
//
#pragma once

// _MULTI_INTERFACE selects where a d3d symbol lives.
// In a single-driver build it expands to nothing, so
//   namespace d3d _MULTI_INTERFACE { ... }
// is just namespace d3d. In a multi-driver build each driver's d3d_config.h
// redefines it to "::inline multi_<driver>", turning the block into an inline
// sub-namespace of d3d (e.g. d3d::inline multi_dx11). Every driver then defines
// its functions in its own sub-namespace, so identically-named definitions from
// different drivers do not collide.
//
// Rule for splitting a header between the two blocks:
//  - namespace d3d { ... } -- shared symbols: one definition common to all
//                             drivers (structs, enums, opaque forward declarations
//                             like struct RenderPass;).
//  - namespace d3d _MULTI_INTERFACE { ... } -- per-driver functions: each driver
//                             provides its own definition.
// _MULTI_INTERFACE must come AFTER ALL NESTED NAMESPACES, so the per-driver
// inline sub-namespace is the innermost segment. The general form is
//   namespace d3d<nested> _MULTI_INTERFACE { ... }
// where <nested> is nothing, ::raytrace, ::pcwin, etc., e.g.
//   namespace d3d::raytrace _MULTI_INTERFACE { ... }
// expands in multi-driver builds to d3d::raytrace::inline multi_<driver>.

#ifndef _MULTI_INTERFACE
#define _MULTI_INTERFACE
#endif
