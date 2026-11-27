#!/usr/bin/env python3
"""Make a model from Sketchfab (or anywhere) ready for a Cabby game.

    tools/prepare-model.py IN.glb OUT.glb [--scale S] [--turn DEGREES]
        [--lift Y] [--rename OLD=NEW ...] [--keep NAME ...] [--still NEW=OLD ...]

  --scale S         made S times as big
  --turn DEGREES    turned round, to its left (as `turn` turns a shape):
                    a model facing +x (to the right) needs --turn 90 to
                    face -z, forwards, as Cabby's shapes do
  --lift Y          moved up (or down, given less than 0): its feet at 0
  --rename OLD=NEW  an animation renamed: "Armature|Walk.001"=walk
  --keep NAME       only the animations named (after renaming) are kept
  --still NEW=OLD   a new animation, NEW, of OLD's first moment only: a
                    model with only a walk can stand still with it (idle)

The model's own nodes are put under one new node that scales, turns and
lifts them all, so a skinned character bends as before. Nothing else is
changed: its meshes, pictures and the rest are copied as they are. Run
it with no options to see what a model has.
"""
import argparse
import json
import math
import struct
import sys

SIZES = {5120: 1, 5121: 1, 5122: 2, 5123: 2, 5125: 4, 5126: 4}
COUNTS = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4, 'MAT4': 16}


def read_glb(path):
    data = open(path, 'rb').read()
    magic, version, length = struct.unpack('<4sII', data[:12])
    if magic != b'glTF':
        sys.exit(f'{path}: not a .glb file')
    gltf, binary, at = None, b'', 12
    while at < length:
        size, kind = struct.unpack('<I4s', data[at:at + 8])
        chunk = data[at + 8:at + 8 + size]
        if kind == b'JSON':
            gltf = json.loads(chunk)
        elif kind == b'BIN\0':
            binary = chunk
        at += 8 + size
    return gltf, bytearray(binary)


def write_glb(path, gltf, binary):
    text = json.dumps(gltf, separators=(',', ':')).encode()
    text += b' ' * (-len(text) % 4)
    binary = bytes(binary) + b'\0' * (-len(binary) % 4)
    total = 12 + 8 + len(text) + (8 + len(binary) if binary else 0)
    with open(path, 'wb') as f:
        f.write(struct.pack('<4sII', b'glTF', 2, total))
        f.write(struct.pack('<I4s', len(text), b'JSON') + text)
        if binary:
            f.write(struct.pack('<I4s', len(binary), b'BIN\0') + binary)


def first_element(gltf, binary, index, keys):
    """the bytes of an accessor's first key's value (for weights, all of them)"""
    acc = gltf['accessors'][index]
    view = gltf['bufferViews'][acc['bufferView']]
    size = SIZES[acc['componentType']] * COUNTS[acc['type']]
    per_key = acc['count'] // keys        # (weights: one per morph target)
    stride = view.get('byteStride', size)
    start = view.get('byteOffset', 0) + acc.get('byteOffset', 0)
    out = b''.join(bytes(binary[start + i * stride:start + i * stride + size]) for i in range(per_key))
    return out, per_key


def add_view(gltf, binary, data):
    binary.extend(b'\0' * (-len(binary) % 4))
    gltf.setdefault('bufferViews', []).append({'buffer': 0, 'byteOffset': len(binary), 'byteLength': len(data)})
    binary.extend(data)
    return len(gltf['bufferViews']) - 1


def add_still(gltf, binary, new, old):
    anims = gltf.get('animations', [])
    src = next((a for a in anims if a.get('name') == old), None)
    if src is None:
        sys.exit(f'no animation named "{old}" (it has: {", ".join(a.get("name", "") for a in anims)})')
    time = add_view(gltf, binary, struct.pack('<f', 0.0))
    gltf['accessors'].append({'bufferView': time, 'componentType': 5126, 'count': 1, 'type': 'SCALAR',
                              'min': [0.0], 'max': [0.0]})
    time_acc = len(gltf['accessors']) - 1
    samplers = []
    for s in src['samplers']:
        keys = gltf['accessors'][s['input']]['count']
        value, per_key = first_element(gltf, binary, s['output'], keys)
        acc = dict(gltf['accessors'][s['output']])
        for k in ('byteOffset', 'min', 'max', 'sparse'):
            acc.pop(k, None)
        acc['bufferView'] = add_view(gltf, binary, value)
        acc['count'] = per_key
        gltf['accessors'].append(acc)
        samplers.append({'input': time_acc, 'output': len(gltf['accessors']) - 1, 'interpolation': 'STEP'})
    anims.append({'name': new, 'samplers': samplers, 'channels': [dict(c) for c in src['channels']]})


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('input')
    p.add_argument('output', nargs='?')
    p.add_argument('--scale', type=float, default=1)
    p.add_argument('--turn', type=float, default=0)
    p.add_argument('--lift', type=float, default=0)
    p.add_argument('--rename', action='append', default=[])
    p.add_argument('--keep', action='append', default=[])
    p.add_argument('--still', action='append', default=[])
    a = p.parse_args()
    gltf, binary = read_glb(a.input)
    if not a.output:
        tris = sum(gltf['accessors'][q['indices']]['count'] // 3 if 'indices' in q
                   else gltf['accessors'][q['attributes']['POSITION']]['count'] // 3
                   for m in gltf.get('meshes', []) for q in m['primitives'])
        print(f'{a.input}: {tris} triangles')
        for anim in gltf.get('animations', []):
            end = max(gltf['accessors'][s['input']].get('max', [0])[0] for s in anim['samplers'])
            print(f'  animation "{anim.get("name", "")}": {end:.2f} seconds')
        extras = gltf.get('asset', {}).get('extras', {})
        for k in ('title', 'author', 'source', 'license'):
            if k in extras:
                print(f'  {k}: {extras[k]}')
        return
    for pair in a.rename:
        old, new = pair.split('=', 1)
        found = [x for x in gltf.get('animations', []) if x.get('name') == old]
        if not found:
            sys.exit(f'no animation named "{old}"')
        found[0]['name'] = new
    for pair in a.still:
        new, old = pair.split('=', 1)
        add_still(gltf, binary, new, old)
    if a.keep:
        gltf['animations'] = [x for x in gltf.get('animations', []) if x.get('name') in a.keep]
        if not gltf['animations']:
            del gltf['animations']
    if a.scale != 1 or a.turn or a.lift:
        half = math.radians(a.turn) / 2
        wrapper = {'name': 'prepared', 'children': list(gltf['scenes'][gltf.get('scene', 0)]['nodes'])}
        if a.scale != 1:
            wrapper['scale'] = [a.scale] * 3
        if a.turn:
            wrapper['rotation'] = [0, math.sin(half), 0, math.cos(half)]
        if a.lift:
            wrapper['translation'] = [0, a.lift, 0]
        gltf['nodes'].append(wrapper)
        gltf['scenes'][gltf.get('scene', 0)]['nodes'] = [len(gltf['nodes']) - 1]
    if binary:
        gltf['buffers'][0]['byteLength'] = len(binary) + (-len(binary) % 4)
    write_glb(a.output, gltf, binary)


if __name__ == '__main__':
    main()
