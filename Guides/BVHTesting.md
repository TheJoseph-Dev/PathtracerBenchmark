## How to check if your BVH is effective

You’ll want both **qualitative** (visual, logical) and **quantitative** (performance) checks.

---

### 🔍 **A. Logical / structural checks**

Before you even trace rays, make sure your BVH is valid.

**Checklist:**

* Each node’s bounding box fully encloses all triangles in its subtree.
  → You can test this recursively in CPU and assert that no child bbox exceeds parent bbox.
* Each leaf node has ≤ N triangles (e.g., 1–4).
* No triangle appears in more than one leaf.
* No triangle is lost (verify total triangle count in leaves equals input triangle count).
* Tree height is reasonable (≈ log₂N for balanced BVHs).

**Debug trick:**
Export your BVH nodes and visualize bounding boxes (wireframes or simple colored cubes).
For example, draw all leaf boxes in 3D (can be a separate debug render pass in Vulkan).

---

### ⚡ **B. Effectiveness metrics**

Once you can trace rays, test efficiency.

1. **Compare against brute force.**

   * Disable the BVH, render with naïve all-triangles intersection.
   * Measure time per frame.
   * Then enable BVH traversal and measure again.
   * A decent SAH BVH should be **10×–100× faster** for scenes with thousands of triangles.

2. **Count intersection tests per ray.**

   * Instrument your traversal code to increment a counter every time you test a ray against a triangle or a bounding box.
   * Compare average `numTriTests` between BVH and brute-force.
   * BVH should reduce triangle tests drastically (down to ~log₂N behavior).

3. **Timing metrics.**

   * For small scenes: measure in milliseconds.
   * For large scenes: rays/sec or frame time at fixed spp.
   * Keep geometry, camera, and sample count constant when comparing.

4. **Visual verification.**

   * Render an image using BVH vs brute-force pathtracer.
   * If they look identical (within noise), your BVH traversal logic is correct.
   * If not, BVH likely misses intersections (bug in AABB or traversal order).

---

### 🧮 4. Quick formula sanity checks

If your BVH is built correctly:

* Build time ~ `O(N log N)`
* Memory usage ~ `2N` nodes roughly (depends on leaf size).
* Traversal cost per ray ~ `O(log N)` on average.

For example:

| Scene         | Triangles | Brute-force tri tests/ray | BVH tri tests/ray |
| ------------- | --------- | ------------------------- | ----------------- |
| Cornell box   | 3k        | ~3,000                    | ~30               |
| Complex model | 100k      | 100,000                   | ~50–80            |