#pragma once
#include "render/mesh.h"

namespace mesh3d {

/* Build a signal coverage sphere (icosphere subdiv 3 = 642 verts).
   Same geometry as node_marker icosphere but at higher subdivision. */
Mesh build_signal_sphere();

} // namespace mesh3d
