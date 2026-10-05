#!/usr/bin/env python3
"""Writes tests/texture_info.glb for spec/43_5 §3: two materials, two 2x1 PNG images referenced
crosswise by two textures, per-binding texCoord/scale/strength/transform, emissive strength,
extensionsUsed/Required and top-level extras. Run from the aiml root."""
import json, struct, zlib


def png(rgba_rows):
    h, w = len(rgba_rows), len(rgba_rows[0]) // 4
    raw = b''.join(b'\0' + bytes(r) for r in rgba_rows)

    def chunk(tag, data):
        return struct.pack('>I', len(data)) + tag + data + struct.pack('>I', zlib.crc32(tag + data))
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0))
            + chunk(b'IDAT', zlib.compress(raw)) + chunk(b'IEND', b''))


img_bw = png([[0, 0, 0, 255, 255, 255, 255, 255]])
img_flat = png([[128, 128, 255, 255, 128, 128, 255, 255]])
img_bw += b'\0' * (-len(img_bw) % 4)
pos = struct.pack('<9f', 0, 0, 0, 1, 0, 0, 0, 1, 0)
uvs = struct.pack('<6f', 0, 0, 1, 0, 0, 1)
idx = struct.pack('<3H', 0, 1, 2) + b'\0\0'
binc = pos + uvs + idx + img_bw + img_flat
o_uv, o_idx = len(pos), len(pos) + len(uvs)
o_bw = o_idx + len(idx)
o_flat = o_bw + len(img_bw)
doc = {
    "asset": {"version": "2.0"}, "scene": 0, "scenes": [{"nodes": [0]}],
    "extensionsUsed": ["KHR_texture_transform", "KHR_materials_emissive_strength"],
    "extensionsRequired": ["KHR_texture_transform"],
    "extras": {"look": "studio"},
    "nodes": [{"name": "tri_node", "mesh": 0}],
    "meshes": [{"name": "tri", "primitives": [
        {"attributes": {"POSITION": 0, "TEXCOORD_0": 1}, "indices": 2, "material": 0}]}],
    "materials": [
        {"name": "probe",
         "pbrMetallicRoughness": {"baseColorTexture": {"index": 0},
                                  "metallicRoughnessTexture": {"index": 1, "texCoord": 1}},
         "normalTexture": {"index": 1, "scale": 0.5},
         "occlusionTexture": {"index": 1, "strength": 0.25},
         "emissiveTexture": {"index": 0, "extensions": {"KHR_texture_transform": {"scale": [2, 2]}}},
         "emissiveFactor": [1, 1, 1],
         "extensions": {"KHR_materials_emissive_strength": {"emissiveStrength": 2.5}}},
        {"name": "probe"}],
    "textures": [{"source": 1, "sampler": 0}, {"source": 0}],
    "samplers": [{"wrapS": 33071, "wrapT": 33648, "magFilter": 9728, "minFilter": 9987}],
    "images": [{"bufferView": 3, "mimeType": "image/png"}, {"bufferView": 4, "mimeType": "image/png"}],
    "accessors": [{"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3",
                   "min": [0, 0, 0], "max": [1, 1, 0]},
                  {"bufferView": 1, "componentType": 5126, "count": 3, "type": "VEC2"},
                  {"bufferView": 2, "componentType": 5123, "count": 3, "type": "SCALAR"}],
    "bufferViews": [{"buffer": 0, "byteLength": len(pos)},
                    {"buffer": 0, "byteOffset": o_uv, "byteLength": len(uvs)},
                    {"buffer": 0, "byteOffset": o_idx, "byteLength": 6},
                    {"buffer": 0, "byteOffset": o_bw, "byteLength": len(img_bw)},
                    {"buffer": 0, "byteOffset": o_flat, "byteLength": len(img_flat)}],
    "buffers": [{"byteLength": len(binc)}]}
js = json.dumps(doc).encode()
js += b' ' * (-len(js) % 4)
binc += b'\0' * (-len(binc) % 4)
body = struct.pack('<II', len(js), 0x4E4F534A) + js + struct.pack('<II', len(binc), 0x004E4942) + binc
open('tests/texture_info.glb', 'wb').write(struct.pack('<III', 0x46546C67, 2, 12 + len(body)) + body)
