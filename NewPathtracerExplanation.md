## Why this suddenly feels *more confusing* than before

Before NEE, your renderer was doing something like this:

> "Bounce rays randomly and hope one eventually hits a light."

That's *conceptually simple*, even if inefficient.

When you added NEE, you did **two new things at once**:

1. You changed **what you sample**
2. You changed **how probability enters the equation**

That second part is where your brain is rebelling.

---

## Let's kill the "black magic" feeling

I'm going to explain **only what you need**, in plain language, **no integrals**.

---

## What lighting REALLY is (forget the equations)

At every surface hit, you are asking:

> “How much light arrives here from the scene?”

There are **two ways** light can arrive:

### 1️# By accident (old path tracing)

A random bounce happens to hit a light.

* Simple
* Very noisy
* But hard to mess up

### 2️# On purpose (NEE)

You *explicitly* aim a ray at a light.

* Much cleaner
* But now you must say:

  > "How unlikely was this sample?"

That "how unlikely" part is the **PDF**.

---

## The single rule you must not break

This is the *only* rule that matters:

> **If you make something easier to sample, you must divide by how easy it was.**

That's it.

Everything else is bookkeeping.

---

## Why corner artifacts appear (intuitively)

Imagine this room:

* One big ceiling light
* You randomly pick **one triangle** on it
* Every triangle is equally likely

Now think:

* A **small triangle** represents very little real light area
* But you treat it as equally important as a big one

Result:

- Small triangles emit **way too much energy**

Where do small triangles usually live?

- Edges
- Corners
- Light borders

That's your "hyperbolic cosine blobs".

**Nothing mystical is happening.**
You're just overpaying small geometry.

---

## Why the ceiling was dark earlier

This line:

```glsl
light *= 0.0;
```

That was literally telling the renderer:

> "Forget everything you’ve accumulated so far."

So:

* Indirect light died
* Ceiling stayed dark
* Corners looked weird because only direct samples survived

Again: not math — just logic.

---

## Why changing `isDelta` didn’t change anything

You proved something important without realizing it.

This test:

```glsl
if (isDelta)
    light += green;
else
    light += red;
```

Showing red/green correctly means:

- Control flow works
- Shader is executing the expected branch
- The issue is **not** Vulkan
- The issue is **energy accounting**

That's GOOD news.

---

## You don’t need to “understand the math” right now

Here's a secret:

> Most renderers are built **before** people understand the math.

Understanding comes *after* seeing patterns:

* "Oh, that artifact happens when PDF is wrong"
* "This noise means under-sampling"
* "That spike means area mismatch"

You are **already doing this**.

---

## What I would do in your place (important)

### Freeze features

Stop adding:

* MIS
* glossy tricks
* refraction tuning

### Lock one simple goal

> "Diffuse Cornell box, one area light, no fireflies"

### Use dumb but stable hacks

It's okay to:

* Clamp NEE
* Clamp throughput
* Ignore perfect correctness

This is **how everyone learns**.

---

## One mental model to keep

When something looks wrong, ask only this:

> "Am I accidentally giving too much energy to rare events?"

If yes:

* PDF wrong
* Sampling mismatch
* Geometry weight wrong

That’s 90% of path tracing bugs.

---

## Final thing - very important

You didn't "just do it because I helped".

You:

* Debugged BVH memory layout
* Fixed SSBO alignment
* Identified control-flow bugs
* Isolated integrator logic
* Proved hypotheses with visual tests

That's **real skill**, not copy-paste.

You're not cooked.
You're *in the middle of the hardest part*.