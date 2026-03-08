# Vertex Buffer Object (VBO)

Ein **VBO** ist ein Speicherbereich auf der **GPU**.

Du packst Vertex-Daten (Positionen, Normals, UVs, Farben) einmal vom RAM in den GPU-VRAM,
damit der Shader sie direkt lesen kann — ohne dass bei jedem Frame Daten über den Bus
übertragen werden müssen.

```
CPU (RAM)                    GPU (VRAM)
──────────────────           ──────────────────────────
float verts[] = {            VBO
  -0.5, -0.5, 0.0,    →      [-0.5, -0.5, 0.0,
   0.5, -0.5, 0.0,            0.5, -0.5, 0.0,
   0.0,  0.5, 0.0             0.0,  0.5, 0.0]
};
```

**In OpenGL:**

```c
GLuint vbo;
glGenBuffers(1, &vbo);              // ID reservieren
glBindBuffer(GL_ARRAY_BUFFER, vbo); // aktivieren
glBufferData(GL_ARRAY_BUFFER,       // hochladen
    sizeof(verts), verts, GL_STATIC_DRAW);
```

---

## Zusammenspiel mit VAO und Shader

```
VAO  →  "wie sind die Daten im VBO strukturiert?"
         Attribut 0: Position = 3×f32, Stride 32 Bytes, Offset 0
         Attribut 1: UV       = 2×f32, Stride 32 Bytes, Offset 12
         Attribut 2: Normal   = 3×f32, Stride 32 Bytes, Offset 20

VBO  →  der eigentliche Datenpuffer auf der GPU

Shader → liest Attribut 0 als "in vec3 pos", Attribut 1 als "in vec2 uv"
```

Das **VAO** merkt sich die Beschreibung, das **VBO** hält die Rohdaten.

---

## `GL_STATIC_DRAW` vs. `GL_DYNAMIC_DRAW`

| Flag | Bedeutung |
|------|-----------|
| `STATIC_DRAW` | Daten werden einmal hochgeladen, viele Male gelesen (Geometrie) |
| `DYNAMIC_DRAW` | Daten werden häufig aktualisiert (Partikel, UI-Batch) |
| `STREAM_DRAW` | Daten werden fast jeden Frame neu hochgeladen |

---

## In AIML

```aiml
- decl: [vbo, gl_buffer, { call: [gl.create_buffer] }]
- call: [vbo.bind, "array"]
- call: [vbo.upload, vertex_data, "static"]
```
