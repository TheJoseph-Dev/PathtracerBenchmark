#include "View.h"

int main() {

    {
        Pathtracer::Config pathtracerConfig(
            Pathtracer::AccelerationStructure::BVH,
            Pathtracer::API::VULKAN,
            Pathtracer::Scene::CORNELL_BOX,
            Pathtracer::Resolution::R480x320,
            6
        );

        Pathtracer::View view(pathtracerConfig);
        view.run();
    }

    return 0;
}