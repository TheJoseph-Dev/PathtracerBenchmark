#include <catch2/catch_test_macros.hpp>

#include "include/acceleration_structures/KdTree.h"
#include "tests/fixtures/MeshFactory.h"

TEST_CASE("KdTree builds for a single triangle") {
    auto geo = TestFixtures::MakeSingleTriangle();
    KdTree kd(geo);
    REQUIRE_NOTHROW(kd.Build());

    const auto& nodes = kd.GetTree();
    REQUIRE_FALSE(nodes.empty());
}

TEST_CASE("KdTree builds for two triangles") {
    auto geo = TestFixtures::MakeTwoTriangles();
    KdTree kd(geo);
    REQUIRE_NOTHROW(kd.Build());

    const auto& nodes = kd.GetTree();
    REQUIRE_FALSE(nodes.empty());
}
