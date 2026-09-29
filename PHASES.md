# iprop v0.2 — plan (draft)

The v0.2 plan, tracked like the v0.1 phases: numbered tasks with acceptance
criteria, checked off as they land. This is a DRAFT awaiting the final task
list — refine it together before implementing.

## Vision

Two threads:

1. **The game** — a simple playable game where every visible element is an
   iprop: the level, the entities, the props. The structured outputs
   (colliders, anchors, spawn lists) are the gameplay contract; the host is
   a thin loop (input, physics over the output rects, camera, blitting).
   Save = the seed.
2. **The platform completion** — the deferred native backends (Part D) and
   the language gaps hit while porting (typed function parameters).

## Tasks

### 0. Setup
- [ ] **0.1** the `develop` branch and this tracking document.

### A. Game assets (.ips content)
- [ ] **1** `dungeon.ips` — the level generator: floor tiles, wall rects,
  `list point` spawns, the exit — with collider outputs.
- [ ] **2** the entity set — `hero.ips`, `monster.ips`, `chest.ips`,
  `torch.ips`: sprites with `rect` colliders and `point` anchors.
- [ ] **3** the animated hero — walk-cycle frames from derived seeds
  (`hash(seed ^ frameConst)`), the host caches the frames.

### B. The host (HTML5 first)
- [ ] **4** the game loop — build the level from the outputs, the player
  movement with collision against the collider rects, the camera, the blit.
- [ ] **5** the save/restore — the whole state = seed + position; the URL
  hash shares it (playground-style).

### C. Native backends (the deferred Part D)
- [ ] **6** the SFML host — the desktop game/demo window (sf::Image output,
  seed cycling), conformance vs the reference.
- [ ] **7** the OpenGL host — CPU raster → texture → quad presentation,
  atlas grid.
- [ ] **8** the Vulkan host — parity with the OpenGL host, readback
  conformance.

### D. Language v0.2
- [ ] **9** typed function parameters (f32 params — unblocks the clean
  ports; removes the ageFromUnit inline workaround).
- [ ] **10** (open) trig in the VM — a spec change; decide whether it lands
  in v0.2 (new opcodes, §9/§12 update, golden regen).

### E. Release
- [ ] **11** the v0.2.0 release — the cross-backend conformance green, the
  CHANGELOG entry, the tag.

## Open questions

1. The game: top-down dungeon crawler (the proposal) or something else?
2. Scope: does v0.2 include the native backends (6–8) or focus on the game?
3. Trig in the VM: add in v0.2, or defer to v0.3?
