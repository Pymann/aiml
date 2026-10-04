#!/usr/bin/env python3
"""Writes tests/material_extras.glb: one triangle, node 'tri_node', material 'painted' whose extras
carry a shader declaration (spec/43_1 §5 mat.extras). Run from the aiml root."""
import json, struct
pos = struct.pack('<9f', 0, 0, 0, 1, 0, 0, 0, 1, 0)
idx = struct.pack('<3H', 0, 1, 2) + b'\0\0'
extras = {"shader": {"passes": [
    {"source": "#version 330 core\nvoid main() {}\n", "cull": "front",
     "params": {"width": 0.01, "color": [0, 0, 0, 1]}},
    {"source": "#version 330 core\n// toon\n", "cull": "back", "params": {"bands": 3}}]}}
doc = {"asset": {"version": "2.0"}, "scene": 0, "scenes": [{"nodes": [0]}],
       "nodes": [{"name": "tri_node", "mesh": 0}],
       "meshes": [{"name": "tri", "primitives": [{"attributes": {"POSITION": 0}, "indices": 1, "material": 0}]}],
       "materials": [{"name": "painted", "pbrMetallicRoughness": {"baseColorFactor": [1, 0.5, 0, 1]},
                      "extras": extras}],
       "accessors": [{"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3",
                      "min": [0, 0, 0], "max": [1, 1, 0]},
                     {"bufferView": 1, "componentType": 5123, "count": 3, "type": "SCALAR"}],
       "bufferViews": [{"buffer": 0, "byteLength": len(pos)},
                       {"buffer": 0, "byteOffset": len(pos), "byteLength": 6}],
       "buffers": [{"byteLength": len(pos) + len(idx)}]}
js = json.dumps(doc).encode(); js += b' ' * (-len(js) % 4)
binc = pos + idx
body = struct.pack('<II', len(js), 0x4E4F534A) + js + struct.pack('<II', len(binc), 0x004E4942) + binc
open('tests/material_extras.glb', 'wb').write(struct.pack('<III', 0x46546C67, 2, 12 + len(body)) + body)
