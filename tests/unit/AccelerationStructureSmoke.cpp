#include <catch2/catch_test_macros.hpp>

#include "include/acceleration_structures/AccelerationStructure.h"
#include "tests/fixtures/MeshFactory.h"

namespace {
    class TestAS final : public AccelerationStructure {
    public:
        explicit TestAS(const OBJLoader::MeshGeometry& geo)
            : AccelerationStructure(geo) {}

        void Build() override {}

        const std::vector<Triangle>& GetTrianglesInternal() const {
            return triangles;
        }
    };
}

TEST_CASE("AccelerationStructure converts mesh triangles") {
    auto geo = TestFixtures::MakeTwoTriangles();
    TestAS as(geo);
    const auto& tris = as.GetTrianglesInternal();
    REQUIRE(tris.size() == 2);
}
