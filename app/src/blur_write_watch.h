/**
 * blur-nx (2026-11-11, diagnostic, Windows PC): blur_diag_cube_writer (blur_write_watch.cpp). Elsewhere a no-op.
 */
#pragma once

#include <rex/types.h>

namespace blur::write_watch {
#if defined(_WIN32)
// D3D's LockSurface of an 8x8 texture (d3d/hooks/resource.cpp): slice and the address it returns.
void NoteCubeLock(u32 texture, u32 slice, u32 address);
// blur_diag_cube_lights_s: the cube filler sub_8216AE98 wrapped in the indirect-call table (renut_app.h OnPostSetup).
void InstallCubeFillerWrapper();
#else
inline void NoteCubeLock(u32, u32, u32) {}
inline void InstallCubeFillerWrapper() {}
#endif
}  // namespace blur::write_watch
