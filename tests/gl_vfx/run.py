#!/usr/bin/env python3
"""Run after exodus build --all. Requires a display; logs/binaries stay in --output."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import struct

ROOT = Path(__file__).resolve().parents[2]
os.chdir(ROOT)
parser = argparse.ArgumentParser()
parser.add_argument('--output', default='/tmp/aiml-vfx-tests')
parser.add_argument('--valgrind', action='store_true')
args = parser.parse_args()
out = Path(args.output).resolve()
out.mkdir(parents=True, exist_ok=True)
aiml = str(ROOT / 'out/aiml/aiml')
config = json.loads((ROOT / 'exodus.json').read_text())
includes = [f'-I{ROOT / p}' for p in config['search_paths']]
paths = [ROOT / 'out/libaiml'] + [ROOT / p for p in config['linker']['library_paths']]
link = [f'-L{p}' for p in paths] + [f'-Wl,-rpath,{p}' for p in paths]

def run(label, command, *, reject=None, timeout=120):
    result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            text=True, timeout=timeout)
    (out / (label + '.log')).write_text(result.stdout)
    if reject:
        assert result.returncode != 0 and reject in result.stdout, (label, result.returncode, result.stdout[-2000:])
    else:
        assert result.returncode == 0, (label, result.returncode, result.stdout[-2000:])
    print('PASS', label, flush=True)

run('jit', [aiml, 'tests/test_gl_vfx.aiml', '-r'])
run('aot_compile', [aiml, 'tests/test_gl_vfx.aiml', '-o', str(out / 'vfx.o')])
run('aot_link', ['g++', str(out / 'vfx.o'), *link, '-laiml', '-lpthread', '-ldl', '-lm', '-o', str(out / 'vfx')])
run('aot', [str(out / 'vfx')])
run('native_compile', ['g++', '-std=c++17', '-g', '-Isrc/sys', *includes, 'tests/gl_vfx/runtime.cpp',
                       *link, '-laiml', '-lGLEW', '-lGL', '-o', str(out / 'native')])
run('native', [str(out / 'native')])

cases = {
    'arity': ('      - decl: [vao, gl_vao]\n      - call: [vao.attrib_divisor, 1]', 'wrong argument count'),
    'float': ('      - decl: [vao, gl_vao]\n      - call: [vao.attrib_divisor, 1, 1.5]', 'invalid argument type'),
    'buffer_type': ('      - decl: [gpu, gl_buffer]\n      - decl: [data, buffer<f32>]\n      - call: [gpu.update_range, 0, data]', 'expected buffer<u8>'),
    'clone': ('      - decl: [gpu, gl_buffer]\n      - decl: [other, gl_buffer, { clone: gpu }]', 'cannot clone an owned GL resource'),
    'handle_type': ('      - decl: [gpu, gl_buffer]\n      - call: [gl.draw_elements_instanced, gpu, 3, 2]', 'expected gl_vao'),
}
for name, (body, diagnostic) in cases.items():
    source = out / (name + '.aiml')
    source.write_text('- def:\n    name: main\n    return: i32\n    body:\n' + body + '\n      - return: 0\n')
    run('negative_' + name, [aiml, str(source)], reject=diagnostic)
# Clone is forbidden recursively, including a shareable list of owning models.
for shape in ('model', 'container'):
    source = out / ('clone_' + shape + '.aiml')
    typename = 'Owner' if shape == 'model' else 'list<Owner>'
    source.write_text('- model: { name: Owner, fields: [[gpu, gl_buffer]] }\n'
                      '- def:\n    name: main\n    return: i32\n    body:\n'
                      f'      - decl: [source, {typename}]\n'
                      f'      - decl: [target, {typename}, {{ clone: source }}]\n'
                      '      - return: 0\n')
    run('negative_clone_' + shape, [aiml, str(source)], reject='cannot clone an owned GL resource')

for name, body, diagnostic in [
    ('borrow_delete', '      - call: [gpu.delete]', 'cannot delete a borrowed'),
    ('borrow_move', '      - decl: [owned, gl_buffer, gpu]', 'cannot move a borrowed'),
]:
    source = out / (name + '.aiml')
    source.write_text('- def:\n    name: probe\n    args: [[gpu, gl_buffer]]\n    body:\n' + body + '\n- def:\n    name: main\n    return: i32\n    body: [{ return: 0 }]\n')
    run('negative_' + name, [aiml, str(source)], reject=diagnostic)

# Independent transport fixture: preserve embedded images, add distinct UV1 and root VFX metadata.
raw = (ROOT / 'tests/texture_info.glb').read_bytes()
json_size = struct.unpack_from('<I', raw, 12)[0]
document = json.loads(raw[20:20 + json_size])
binary_start = 20 + json_size
binary_size = struct.unpack_from('<I', raw, binary_start)[0]
binary = raw[binary_start + 8:binary_start + 8 + binary_size]
binary += b'\0' * (-len(binary) % 4)
view = len(document['bufferViews'])
accessor = len(document['accessors'])
document['bufferViews'].append({'buffer': 0, 'byteOffset': len(binary), 'byteLength': 24})
binary += struct.pack('<6f', .25, .75, .5, 1., 1., .25)
document['accessors'].append({'bufferView': view, 'componentType': 5126, 'count': 3, 'type': 'VEC2'})
document['meshes'][0]['primitives'][0]['attributes']['TEXCOORD_1'] = accessor
document['extras']['shaper_vfx'] = {'version': 1, 'hash_revision': 1, 'seed': 41}
document['buffers'][0]['byteLength'] = len(binary)
encoded = json.dumps(document, separators=(',', ':')).encode()
encoded += b' ' * (-len(encoded) % 4)
fixture = out / 'transport.glb'
fixture.write_bytes(struct.pack('<III', 0x46546c67, 2, 28 + len(encoded) + len(binary))
                    + struct.pack('<II', len(encoded), 0x4e4f534a) + encoded
                    + struct.pack('<II', len(binary), 0x004e4942) + binary)
source = out / 'transport.aiml'
source.write_text("""- def:
    name: main
    return: i32
    body:
      - decl: [file, path, FIXTURE]
      - decl: [scene, gltf_scene, { call: [gltf.load, file] }]
      - decl: [meshes, list<gltf_mesh>, { call: [scene.meshes] }]
      - decl: [mesh, gltf_mesh, { call: [meshes.get, 0] }]
      - decl: [primitive, gltf_primitive, { call: [mesh.primitive, 0] }]
      - decl: [flow_uv, buffer<f32>, { call: [primitive.uvs, 1] }]
      - decl: [first, f32, { call: [flow_uv.get, 0] }]
      - decl: [second, f32, { call: [flow_uv.get, 1] }]
      - if: { condition: { neq: [first, 0.25] }, then: [{ return: 1 }] }
      - if: { condition: { neq: [second, 0.75] }, then: [{ return: 2 }] }
      - decl: [extra, string, { call: [scene.extras] }]
      - if: { condition: { lt: [{ call: [extra.index_of, shaper_vfx] }, 0] }, then: [{ return: 3 }] }
      - decl: [materials, list<gltf_material>, { call: [scene.materials] }]
      - decl: [material, gltf_material, { call: [materials.get, 0] }]
      - decl: [texture, gltf_texture, { call: [material.base_color_texture] }]
      - decl: [image, gltf_image, { call: [texture.image] }]
      - decl: [pixels, buffer<u8>, { call: [image.pixels_rgba] }]
      - decl: [width, i32, { call: [image.width] }]
      - if: { condition: { neq: [width, 2] }, then: [{ return: 4 }] }
      - decl: [alpha, i32, { cast: [{ call: [pixels.get, 3] }, i32] }]
      - if: { condition: { neq: [alpha, 255] }, then: [{ return: 5 }] }
      - call: [scene.free]
      - print: ["PASS VFX transport: UV1, extras, embedded PNG"]
      - return: 0
""".replace('FIXTURE', json.dumps(str(fixture))).replace('extra.index_of, shaper_vfx', 'extra.index_of, "shaper_vfx"'))
run('gltf_transport', [aiml, str(source), '-r'])

for test in ['test_gl_studio_formats', 'test_gl_texture_owned', 'test_gl_timer_query',
             'test_gltf_texture_info', 'test_gltf_anim_sample', 'test_gltf_image_owned']:
    run(test, [aiml, f'tests/{test}.aiml', '-r'])
if args.valgrind:
    run('valgrind', ['valgrind', '--error-exitcode=99', '--leak-check=full', '--show-leak-kinds=definite,indirect',
                     '--errors-for-leak-kinds=definite,indirect', str(out / 'vfx')], timeout=180)
print('All VFX checks passed; logs:', out)
