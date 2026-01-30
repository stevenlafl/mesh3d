#pragma once
#include "render/mesh.h"
#include <vector>
#include <cstdint>

namespace mesh3d {

/* Generate an icosphere mesh for node markers.
   subdivisions: 0=icosahedron(12 verts), 1=42v, 2=162v, 3=642v */
Mesh build_icosphere(int subdivisions);

} // namespace mesh3d
