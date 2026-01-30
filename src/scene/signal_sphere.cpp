#include "scene/signal_sphere.h"
#include "scene/node_marker.h"

namespace mesh3d {

Mesh build_signal_sphere() {
    return build_icosphere(3); // 642 vertices
}

} // namespace mesh3d
