# Kapitel 44.4 — Physics

> **Leitgedanke:** Ein schlanker nativer `physics`-Namespace — kein Bullet, kein Box2D,
> keine externen Deps. Fokus auf die häufigsten Game-Engine-Anforderungen:
> Kollisionserkennung (AABB, Sphere, Capsule, Ray), einfache Rigid-Body-Dynamik
> (Euler-Integration, Gravitation, Impuls) und eine Broadphase für viele Objekte.
> Läuft unverändert auf Desktop, Android und Browser.

---

## Architektur

```
┌──────────────────────────────────────────────────────────────┐
│  AIML Spielcode     physics.raycast / body.apply_force       │
├──────────────────────────────────────────────────────────────┤
│  physics Namespace  Collision Detection + Rigid Body         │
│                     AABB / Sphere / Capsule / Ray            │
├──────────────────────────────────────────────────────────────┤
│  Broadphase         Uniform Grid (für viele Objekte)         │
├──────────────────────────────────────────────────────────────┤
│  physics_runtime.h/cpp  C-Implementierung                    │
└──────────────────────────────────────────────────────────────┘
```

---

## 1. Typen

| Typ | LLVM | Beschreibung |
|-----|------|--------------|
| `physics_world` | `i8*` | Physik-Welt; hält alle Bodies und Broadphase-Struktur |
| `physics_body` | `i8*` | Rigid Body mit Position, Velocity, Masse, Shape |
| `physics_shape` | `i8*` | Kollisionsform (AABB / Sphere / Capsule) |
| `physics_hit` | struct | Ergebnis eines Raycasts oder Overlap-Tests |

### `physics_hit` — Felder

```aiml
model: physics_hit
fields:
  - [hit,      bool]   # true wenn Treffer
  - [point,    vec3]   # Auftreffpunkt in Weltkoordinaten
  - [normal,   vec3]   # Oberflächennormale am Auftreffpunkt
  - [distance, f32]    # Distanz vom Strahl-Ursprung zum Auftreffpunkt
  - [body,     physics_body]   # getroffener Body; null wenn kein Body
```

---

## 2. Welt

| Aufruf | Rückgabe | Beschreibung |
|--------|----------|--------------|
| `physics.create_world()` | `physics_world` | Neue Physik-Welt erstellen |
| `physics.destroy_world(w: physics_world)` | — | Welt und alle Bodies freigeben |
| `physics.set_gravity(w: physics_world, g: vec3)` | — | Gravitation setzen; Default `{0, -9.81, 0}` |
| `physics.step(w: physics_world, dt: f32)` | — | Physik-Simulation einen Schritt vorwärts; einmal pro Frame aufrufen |
| `physics.set_iterations(w: physics_world, n: i32)` | — | Collision-Response-Iterationen pro Step; Default `4` |

---

## 3. Formen (Shapes)

| Aufruf | Rückgabe | Beschreibung |
|--------|----------|--------------|
| `physics.box_shape(half_extents: vec3)` | `physics_shape` | AABB-Form; `half_extents` = halbe Seitenlängen |
| `physics.sphere_shape(radius: f32)` | `physics_shape` | Kugelform |
| `physics.capsule_shape(radius: f32, height: f32)` | `physics_shape` | Kapselform; `height` = Zylinderanteil (ohne Halbkugeln) |
| `physics.free_shape(s: physics_shape)` | — | Form freigeben |

---

## 4. Rigid Bodies

### 4.1 Erstellen und Entfernen

| Aufruf | Rückgabe | Beschreibung |
|--------|----------|--------------|
| `physics.add_body(w: physics_world, shape: physics_shape, mass: f32)` | `physics_body` | Body zur Welt hinzufügen; `mass=0` = statisch (unbeweglich) |
| `physics.remove_body(w: physics_world, b: physics_body)` | — | Body aus Welt entfernen und freigeben |

### 4.2 Transform

| Methode | Argumente | Rückgabe | Beschreibung |
|---------|-----------|----------|--------------|
| `get_position` | — | `vec3` | Position in Weltkoordinaten |
| `set_position` | `pos: vec3` | — | Position direkt setzen (teleportiert) |
| `get_rotation` | — | `quat` | Orientierung als Quaternion |
| `set_rotation` | `q: quat` | — | Orientierung direkt setzen |
| `get_transform` | — | `mat4` | Position + Rotation als mat4 (direkt für Render nutzbar) |

### 4.3 Dynamik

| Methode | Argumente | Rückgabe | Beschreibung |
|---------|-----------|----------|--------------|
| `get_velocity` | — | `vec3` | Lineare Geschwindigkeit |
| `set_velocity` | `v: vec3` | — | Lineare Geschwindigkeit setzen |
| `get_angular_velocity` | — | `vec3` | Winkelgeschwindigkeit (rad/s) |
| `set_angular_velocity` | `v: vec3` | — | Winkelgeschwindigkeit setzen |
| `apply_force` | `f: vec3` | — | Kraft anwenden (akkumuliert bis nächstem Step) |
| `apply_force_at` | `f: vec3, point: vec3` | — | Kraft an Weltpunkt anwenden (erzeugt auch Drehmoment) |
| `apply_impulse` | `imp: vec3` | — | Impuls anwenden (sofortige Geschwindigkeitsänderung) |
| `apply_torque` | `t: vec3` | — | Drehmoment anwenden |
| `set_damping` | `linear: f32, angular: f32` | — | Dämpfung (0.0 = keine, 1.0 = sofort gestoppt) |
| `set_friction` | `f: f32` | — | Reibungskoeffizient (0.0–1.0) |
| `set_restitution` | `r: f32` | — | Elastizität / Rückprall (0.0 = perfekt inelastisch, 1.0 = perfekt elastisch) |
| `set_kinematic` | `on: bool` | — | Kinematischer Body: von Physik unbeeinflusst, aber kollidiert |
| `set_active` | `on: bool` | — | Body aktivieren/deaktivieren (deaktiviert = ignoriert von Simulation) |

---

## 5. Kollisionserkennung

### 5.1 Raycast

| Aufruf | Rückgabe | Beschreibung |
|--------|----------|--------------|
| `physics.raycast(w: physics_world, origin: vec3, dir: vec3, max_dist: f32)` | `physics_hit` | Strahl gegen alle Bodies der Welt testen; gibt nächsten Treffer zurück |
| `physics.raycast_all(w: physics_world, origin: vec3, dir: vec3, max_dist: f32)` | `list<physics_hit>` | Alle Treffer entlang des Strahls (sortiert nach Distanz) |

### 5.2 Overlap-Tests

| Aufruf | Rückgabe | Beschreibung |
|--------|----------|--------------|
| `physics.overlap_sphere(w: physics_world, center: vec3, radius: f32)` | `list<physics_body>` | Alle Bodies die eine Kugel schneiden |
| `physics.overlap_box(w: physics_world, center: vec3, half_extents: vec3)` | `list<physics_body>` | Alle Bodies die eine Box schneiden |

### 5.3 Direkte Shape-Tests (ohne Welt)

| Aufruf | Rückgabe | Beschreibung |
|--------|----------|--------------|
| `physics.aabb_aabb(a_min: vec3, a_max: vec3, b_min: vec3, b_max: vec3)` | `bool` | AABB–AABB Überschneidung |
| `physics.sphere_sphere(a: vec3, ra: f32, b: vec3, rb: f32)` | `bool` | Kugel–Kugel Überschneidung |
| `physics.aabb_sphere(box_min: vec3, box_max: vec3, center: vec3, r: f32)` | `bool` | AABB–Kugel Überschneidung |
| `physics.ray_aabb(origin: vec3, dir: vec3, box_min: vec3, box_max: vec3)` | `physics_hit` | Strahl–AABB Test |
| `physics.ray_sphere(origin: vec3, dir: vec3, center: vec3, r: f32)` | `physics_hit` | Strahl–Kugel Test |
| `physics.ray_plane(origin: vec3, dir: vec3, normal: vec3, d: f32)` | `physics_hit` | Strahl–Ebene Test |

---

## 6. Kollisions-Callbacks

Kollisionsereignisse werden als Events in eine Queue eingetragen, die nach
`physics.step` abgefragt werden kann:

| Aufruf | Rückgabe | Beschreibung |
|--------|----------|--------------|
| `physics.has_collision(w: physics_world)` | `bool` | Queue nicht leer |
| `physics.next_collision(w: physics_world)` | `physics_collision` | Nächstes Kollisionsereignis |

```aiml
model: physics_collision
fields:
  - [body_a,   physics_body]
  - [body_b,   physics_body]
  - [point,    vec3]           # Kollisionspunkt in Weltkoordinaten
  - [normal,   vec3]           # Normale von body_a → body_b
  - [depth,    f32]            # Eindringtiefe
  - [type,     string]         # "enter" | "stay" | "exit"
```

---

## 7. Vollständiges Beispiel

```aiml
# ── Welt erstellen ────────────────────────────────────────────────────────────
- decl: [world, physics_world, { call: [physics.create_world] }]
- call: [physics.set_gravity, world, { call: [math.vec3, 0.0, -9.81, 0.0] }]

# ── Statischer Boden (mass=0 → unbeweglich) ───────────────────────────────────
- decl: [floor_shape, physics_shape, { call: [physics.box_shape, { call: [math.vec3, 50.0, 0.5, 50.0] }] }]
- decl: [floor,       physics_body,  { call: [physics.add_body, world, floor_shape, 0.0] }]
- call: [floor.set_position, { call: [math.vec3, 0.0, -0.5, 0.0] }]
- call: [floor.set_friction, 0.8]

# ── Dynamischer Spieler-Body (Kapsel) ─────────────────────────────────────────
- decl: [player_shape, physics_shape, { call: [physics.capsule_shape, 0.4, 1.0] }]
- decl: [player_body,  physics_body,  { call: [physics.add_body, world, player_shape, 70.0] }]
- call: [player_body.set_position,    { call: [math.vec3, 0.0, 2.0, 0.0] }]
- call: [player_body.set_damping, 0.1, 0.9]
- call: [player_body.set_friction, 0.5]

# ── Update-Funktion ───────────────────────────────────────────────────────────
- def:
    name: update
    args: [[dt, f32]]
    body:
      # Bewegungs-Input → Kraft auf Player-Body
      - decl: [fwd,  f32, { call: [input.axis, "forward"] }]
      - decl: [side, f32, { call: [input.axis, "strafe"]  }]
      - decl: [force, vec3, { call: [math.vec3, side, 0.0, { "-": [0.0, fwd] }] }]
      - set: [force, { "*": [force, 400.0] }]
      - call: [player_body.apply_force, force]

      # Springen
      - decl: [jumped, bool, { call: [input.action_pressed, "jump"] }]
      - if: [{ ==: [jumped, true] }, true]
        then:
          - call: [player_body.apply_impulse, { call: [math.vec3, 0.0, 500.0, 0.0] }]

      # Physik-Simulation vorwärts
      - call: [physics.step, world, dt]

      # Kollisions-Events verarbeiten
      - while:
          condition: { call: [physics.has_collision, world] }
          body:
            - decl: [col, physics_collision, { call: [physics.next_collision, world] }]
            - if: [{ eq: [col.type, "enter"] }, true]
              then:
                - call: [on_collision, col]

      # Transform für Render holen
      - decl: [tfm, mat4, { call: [player_body.get_transform] }]
      - call: [render_player, tfm]

# ── Raycast-Beispiel: Maus-Klick auf Welt-Objekt ──────────────────────────────
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

## 8. Implementierungshinweise

### 8.1 Integration

Semi-implizite Euler-Integration pro Step:

```
velocity += (forces / mass + gravity) * dt
position += velocity * dt
forces    = {0, 0, 0}   (zurücksetzen nach jedem Step)
```

Für `dt` aus `engine.dt()` mit Sub-Stepping bei großen Frames:
`physics.step` unterteilt `dt > 0.02` automatisch in mehrere Teil-Steps
(max. 5 × 0.02s) — verhindert "Tunneling" bei hoher Geschwindigkeit.

### 8.2 Broadphase

Uniform Grid: Welt in Zellen aufgeteilt (Zellgröße = größte Shape-Ausdehnung).
Jeder Body trägt sich in die überlappenden Zellen ein. Nur Bodies in derselben
Zelle werden auf Narrowphase-Kollision getestet. O(1) Insert/Remove, O(n) Step
für gleichmäßig verteilte Objekte.

### 8.3 Narrowphase

| Paar | Algorithmus |
|------|-------------|
| Sphere–Sphere | Distanz der Mittelpunkte vs. Summe der Radien |
| AABB–AABB | SAT auf 3 Achsen |
| Sphere–AABB | Nächster Punkt auf AABB zu Kugelmittelpunkt |
| Capsule–Sphere | Nächster Punkt auf Kapsel-Segment zu Kugelmittelpunkt |
| Capsule–AABB | GJK (vereinfacht) |
| Capsule–Capsule | Segment–Segment Distanz |

### 8.4 Constraint Solver

Sequentieller Impulse-Solver (iterativ, `set_iterations` steuert Qualität).
Kontaktpunkte werden pro Step frisch berechnet — kein Warm-Starting in v1.

### 8.5 Kinematische Bodies

`set_kinematic(true)`: Position/Rotation werden direkt vom Spielcode gesetzt
(z.B. animierte Plattform). Der Body kollidiert mit dynamischen Bodies und schiebt
sie weg, wird selbst aber nicht von Kräften beeinflusst.
