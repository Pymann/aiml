# Chapter 44.4 — Physics

> **Core idea:** A lean native `physics` namespace — no Bullet, no Box2D,
> no external dependencies. Focus on the most common game engine requirements:
> collision detection (AABB, Sphere, Capsule, Ray), simple rigid body dynamics
> (Euler integration, gravity, impulse) and a broadphase for many objects.
> Runs unchanged on desktop, Android, and browser.

---

## Architecture

```
┌──────────────────────────────────────────────────────────────┐
│  AIML game code     physics.raycast / body.apply_force       │
├──────────────────────────────────────────────────────────────┤
│  physics namespace  Collision Detection + Rigid Body         │
│                     AABB / Sphere / Capsule / Ray            │
├──────────────────────────────────────────────────────────────┤
│  Broadphase         Uniform Grid (for many objects)          │
├──────────────────────────────────────────────────────────────┤
│  physics_runtime.h/cpp  C implementation                     │
└──────────────────────────────────────────────────────────────┘
```

---

## 1. Types

| Type | LLVM | Description |
|------|------|-------------|
| `physics_world` | `i8*` | Physics world; holds all bodies and broadphase structure |
| `physics_body` | `i8*` | Rigid body with position, velocity, mass, shape |
| `physics_shape` | `i8*` | Collision shape (AABB / Sphere / Capsule) |
| `physics_hit` | struct | Result of a raycast or overlap test |

### `physics_hit` — Fields

```aiml
model: physics_hit
fields:
  - [hit,      bool]   # true if there was a hit
  - [point,    vec3]   # hit point in world coordinates
  - [normal,   vec3]   # surface normal at the hit point
  - [distance, f32]    # distance from ray origin to hit point
  - [body,     physics_body]   # hit body; null if no body
```

---

## 2. World

| Call | Return | Description |
|------|--------|-------------|
| `physics.create_world()` | `physics_world` | Create a new physics world |
| `physics.destroy_world(w: physics_world)` | — | Destroy world and free all bodies |
| `physics.set_gravity(w: physics_world, g: vec3)` | — | Set gravity; default `{0, -9.81, 0}` |
| `physics.step(w: physics_world, dt: f32)` | — | Advance the physics simulation one step; call once per frame |
| `physics.set_iterations(w: physics_world, n: i32)` | — | Collision response iterations per step; default `4` |

---

## 3. Shapes

| Call | Return | Description |
|------|--------|-------------|
| `physics.box_shape(half_extents: vec3)` | `physics_shape` | AABB shape; `half_extents` = half side lengths |
| `physics.sphere_shape(radius: f32)` | `physics_shape` | Sphere shape |
| `physics.capsule_shape(radius: f32, height: f32)` | `physics_shape` | Capsule shape; `height` = cylinder portion (excluding hemispheres) |
| `physics.free_shape(s: physics_shape)` | — | Free a shape |

---

## 4. Rigid Bodies

### 4.1 Creating and Removing

| Call | Return | Description |
|------|--------|-------------|
| `physics.add_body(w: physics_world, shape: physics_shape, mass: f32)` | `physics_body` | Add body to world; `mass=0` = static (immovable) |
| `physics.remove_body(w: physics_world, b: physics_body)` | — | Remove body from world and free it |

### 4.2 Transform

| Method | Arguments | Return | Description |
|--------|-----------|--------|-------------|
| `get_position` | — | `vec3` | Position in world coordinates |
| `set_position` | `pos: vec3` | — | Set position directly (teleports) |
| `get_rotation` | — | `quat` | Orientation as quaternion |
| `set_rotation` | `q: quat` | — | Set orientation directly |
| `get_transform` | — | `mat4` | Position + rotation as mat4 (directly usable for rendering) |

### 4.3 Dynamics

| Method | Arguments | Return | Description |
|--------|-----------|--------|-------------|
| `get_velocity` | — | `vec3` | Linear velocity |
| `set_velocity` | `v: vec3` | — | Set linear velocity |
| `get_angular_velocity` | — | `vec3` | Angular velocity (rad/s) |
| `set_angular_velocity` | `v: vec3` | — | Set angular velocity |
| `apply_force` | `f: vec3` | — | Apply force (accumulated until next step) |
| `apply_force_at` | `f: vec3, point: vec3` | — | Apply force at a world point (also generates torque) |
| `apply_impulse` | `imp: vec3` | — | Apply impulse (immediate velocity change) |
| `apply_torque` | `t: vec3` | — | Apply torque |
| `set_damping` | `linear: f32, angular: f32` | — | Damping (0.0 = none, 1.0 = immediately stopped) |
| `set_friction` | `f: f32` | — | Friction coefficient (0.0–1.0) |
| `set_restitution` | `r: f32` | — | Elasticity / bounce (0.0 = perfectly inelastic, 1.0 = perfectly elastic) |
| `set_kinematic` | `on: bool` | — | Kinematic body: unaffected by physics forces, but still collides |
| `set_active` | `on: bool` | — | Activate/deactivate body (deactivated = ignored by simulation) |

---

## 5. Collision Detection

### 5.1 Raycast

| Call | Return | Description |
|------|--------|-------------|
| `physics.raycast(w: physics_world, origin: vec3, dir: vec3, max_dist: f32)` | `physics_hit` | Test ray against all bodies in the world; returns nearest hit |
| `physics.raycast_all(w: physics_world, origin: vec3, dir: vec3, max_dist: f32)` | `list<physics_hit>` | All hits along the ray (sorted by distance) |

### 5.2 Overlap Tests

| Call | Return | Description |
|------|--------|-------------|
| `physics.overlap_sphere(w: physics_world, center: vec3, radius: f32)` | `list<physics_body>` | All bodies intersecting a sphere |
| `physics.overlap_box(w: physics_world, center: vec3, half_extents: vec3)` | `list<physics_body>` | All bodies intersecting a box |

### 5.3 Direct Shape Tests (without world)

| Call | Return | Description |
|------|--------|-------------|
| `physics.aabb_aabb(a_min: vec3, a_max: vec3, b_min: vec3, b_max: vec3)` | `bool` | AABB–AABB intersection |
| `physics.sphere_sphere(a: vec3, ra: f32, b: vec3, rb: f32)` | `bool` | Sphere–sphere intersection |
| `physics.aabb_sphere(box_min: vec3, box_max: vec3, center: vec3, r: f32)` | `bool` | AABB–sphere intersection |
| `physics.ray_aabb(origin: vec3, dir: vec3, box_min: vec3, box_max: vec3)` | `physics_hit` | Ray–AABB test |
| `physics.ray_sphere(origin: vec3, dir: vec3, center: vec3, r: f32)` | `physics_hit` | Ray–sphere test |
| `physics.ray_plane(origin: vec3, dir: vec3, normal: vec3, d: f32)` | `physics_hit` | Ray–plane test |

---

## 6. Collision Callbacks

Collision events are enqueued and can be queried after
`physics.step`:

| Call | Return | Description |
|------|--------|-------------|
| `physics.has_collision(w: physics_world)` | `bool` | Queue is not empty |
| `physics.next_collision(w: physics_world)` | `physics_collision` | Next collision event |

```aiml
model: physics_collision
fields:
  - [body_a,   physics_body]
  - [body_b,   physics_body]
  - [point,    vec3]           # collision point in world coordinates
  - [normal,   vec3]           # normal from body_a to body_b
  - [depth,    f32]            # penetration depth
  - [type,     string]         # "enter" | "stay" | "exit"
```

---

## 7. Complete Example

```aiml
# ── Create world ───────────────────────────────────────────────────────────────
- decl: [world, physics_world, { call: [physics.create_world] }]
- call: [physics.set_gravity, world, { call: [math.vec3, 0.0, -9.81, 0.0] }]

# ── Static floor (mass=0 → immovable) ─────────────────────────────────────────
- decl: [floor_shape, physics_shape, { call: [physics.box_shape, { call: [math.vec3, 50.0, 0.5, 50.0] }] }]
- decl: [floor,       physics_body,  { call: [physics.add_body, world, floor_shape, 0.0] }]
- call: [floor.set_position, { call: [math.vec3, 0.0, -0.5, 0.0] }]
- call: [floor.set_friction, 0.8]

# ── Dynamic player body (capsule) ──────────────────────────────────────────────
- decl: [player_shape, physics_shape, { call: [physics.capsule_shape, 0.4, 1.0] }]
- decl: [player_body,  physics_body,  { call: [physics.add_body, world, player_shape, 70.0] }]
- call: [player_body.set_position,    { call: [math.vec3, 0.0, 2.0, 0.0] }]
- call: [player_body.set_damping, 0.1, 0.9]
- call: [player_body.set_friction, 0.5]

# ── Update function ────────────────────────────────────────────────────────────
- def:
    name: update
    args: [[dt, f32]]
    body:
      # Movement input → force on player body
      - decl: [fwd,  f32, { call: [input.axis, "forward"] }]
      - decl: [side, f32, { call: [input.axis, "strafe"]  }]
      - decl: [force, vec3, { call: [math.vec3, side, 0.0, { "-": [0.0, fwd] }] }]
      - set: [force, { "*": [force, 400.0] }]
      - call: [player_body.apply_force, force]

      # Jump
      - decl: [jumped, bool, { call: [input.action_pressed, "jump"] }]
      - if: [{ ==: [jumped, true] }, true]
        then:
          - call: [player_body.apply_impulse, { call: [math.vec3, 0.0, 500.0, 0.0] }]

      # Advance physics simulation
      - call: [physics.step, world, dt]

      # Process collision events
      - while:
          condition: { call: [physics.has_collision, world] }
          body:
            - decl: [col, physics_collision, { call: [physics.next_collision, world] }]
            - if: [{ eq: [col.type, "enter"] }, true]
              then:
                - call: [on_collision, col]

      # Get transform for rendering
      - decl: [tfm, mat4, { call: [player_body.get_transform] }]
      - call: [render_player, tfm]

# ── Raycast example: mouse click on world object ───────────────────────────────
- def:
    name: pick_object
    args: [[ray_origin, vec3], [ray_dir, vec3]]
    body:
      - decl: [hit, physics_hit, { call: [physics.raycast, world, ray_origin, ray_dir, 100.0] }]
      - if: [{ ==: [hit.hit, true] }, true]
        then:
          - print: ["Hit at distance: ", hit.distance]
          - call: [hit.body.apply_impulse, { call: [math.vec3, 0.0, 200.0, 0.0] }]
```

---

## 8. Implementation Notes

### 8.1 Integration

Semi-implicit Euler integration per step:

```
velocity += (forces / mass + gravity) * dt
position += velocity * dt
forces    = {0, 0, 0}   (reset after each step)
```

For `dt` from `engine.dt()` with sub-stepping for large frames:
`physics.step` automatically subdivides `dt > 0.02` into multiple sub-steps
(max. 5 × 0.02s) — prevents tunneling at high velocities.

### 8.2 Broadphase

Uniform grid: world divided into cells (cell size = largest shape extent).
Each body registers itself in the overlapping cells. Only bodies in the same
cell are tested for narrowphase collision. O(1) insert/remove, O(n) step
for uniformly distributed objects.

### 8.3 Narrowphase

| Pair | Algorithm |
|------|-----------|
| Sphere–Sphere | Distance between centers vs. sum of radii |
| AABB–AABB | SAT on 3 axes |
| Sphere–AABB | Closest point on AABB to sphere center |
| Capsule–Sphere | Closest point on capsule segment to sphere center |
| Capsule–AABB | GJK (simplified) |
| Capsule–Capsule | Segment–segment distance |

### 8.4 Constraint Solver

Sequential impulse solver (iterative; `set_iterations` controls quality).
Contact points are recomputed fresh each step — no warm-starting in v1.

### 8.5 Kinematic Bodies

`set_kinematic(true)`: position/rotation are set directly by game code
(e.g. animated platform). The body collides with dynamic bodies and pushes
them away, but is itself not affected by forces.
