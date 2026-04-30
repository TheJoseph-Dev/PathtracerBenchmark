#include <catch2/catch_test_macros.hpp>

#include "include/acceleration_structures/BVH.h"
#include "include/acceleration_structures/KdTree.h"
#include "tests/fixtures/MeshFactory.h"

TEST_CASE("BVH build sanity (tiny mesh)", "[benchmark]") {
    auto geo = TestFixtures::MakeTwoTriangles();
    BVH bvh(geo);
    REQUIRE_NOTHROW(bvh.Build());
}

TEST_CASE("KdTree build sanity (tiny mesh)", "[benchmark]") {
    auto geo = TestFixtures::MakeTwoTriangles();
    KdTree kd(geo);
    REQUIRE_NOTHROW(kd.Build());
}
