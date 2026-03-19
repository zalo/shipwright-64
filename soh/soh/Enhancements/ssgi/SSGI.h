#pragma once

// GT-VBGI Screen-Space Global Illumination post-process for Shipwright.
// Original algorithm: https://www.shadertoy.com/view/XfcBDl by TinyTexel
//
// Hooks into the libultraship post-process callback and runs SSGI on the
// game's color + depth buffers each frame.

namespace SSGI {

// Called once from the SSGI enhancement registration function.
// Sets up the post-process hook on the interpreter.
void Register();

} // namespace SSGI
