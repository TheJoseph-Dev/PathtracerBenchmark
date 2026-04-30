#include <catch2/catch_test_macros.hpp>

#include "include/acceleration_structures/BVH.h"
#include "tests/fixtures/MeshFactory.h"

TEST_CASE("BVH builds for a single triangle") {
    auto geo = TestFixtures::MakeSingleTriangle();
    BVH bvh(geo);
    REQUIRE_NOTHROW(bvh.Build());

    const auto& nodes = bvh.GetTree();
    REQUIRE_FALSE(nodes.empty());
    REQUIRE(nodes[0].triCount > 0);
}

TEST_CASE("BVH builds for two triangles") {
    auto geo = TestFixtures::MakeTwoTriangles();
    BVH bvh(geo);
    REQUIRE_NOTHROW(bvh.Build());

    const auto& nodes = bvh.GetTree();
    REQUIRE_FALSE(nodes.empty());
}
