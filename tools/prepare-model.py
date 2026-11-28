#!/usr/bin/env python3
"""Make a model from Sketchfab (or anywhere) ready for a Cabby game.

    tools/prepare-model.py IN.glb OUT.glb [--scale S] [--turn DEGREES]
        [--lift Y] [--forward Z] [--rename OLD=NEW ...] [--keep NAME ...]
        [--still NEW=OLD ...] [--color MATERIAL=#rrggbb ...]
        [--height H] [--center] [--pictures PIXELS]

  --scale S         made S times as big
  --turn DEGREES    turned round, to its left (as `turn` turns a shape):
                    a model facing +x (to the right) needs --turn 90 to
                    face -z, forwards, as Cabby's shapes do
  --lift Y          moved up (or down, given less than 0): its feet at 0
  --forward Z       moved forwards (or back, given less than 0), once it's
                    turned: its middle at 0, so a long animal stands where
                    it's put, not with its head a metre in front
  --rename OLD=NEW  an animation renamed: "Armature|Walk.001"=walk
  --keep NAME       only the animations named (after renaming) are kept
  --still NEW=OLD   a new animation, NEW, of OLD's first moment only: a
                    model with only a walk can stand still with it (idle)
  --height H        made H metres tall (instead of --scale), its feet at 0
  --center          its middle, seen from above, where it's put (0, 0)
  --pictures PIXELS its pictures made no bigger than that, across or down
                    (with Pillow: pip install pillow), so the file's smaller
  --color MATERIAL=#rrggbb
                    one of its materials (by name: "clothing") made that
                    color, as a screen shows it

The model's own nodes are put under one new node that scales, turns and
moves them all, so a skinned character bends as before. And every
animation is made to move every bone that any of them moves (those it
left alone, as they are in the model): played one after another, one
animation never leaves a leg where the last one put it. Nothing else is
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


def compact(gltf, binary):
    """only what's still used kept: the data of animations left out, or
    renamed away, goes (a model's file is as small as it can be)"""
    views = gltf.get('bufferViews', [])
    used_acc = set()
    for m in gltf.get('meshes', []):
        for q in m['primitives']:
            used_acc.update(q.get('attributes', {}).values())
            for t in q.get('targets', []):
                used_acc.update(t.values())
            if 'indices' in q:
                used_acc.add(q['indices'])
    for a in gltf.get('animations', []):
        for smp in a['samplers']:
            used_acc.update((smp['input'], smp['output']))
    for sk in gltf.get('skins', []):
        if 'inverseBindMatrices' in sk:
            used_acc.add(sk['inverseBindMatrices'])
    accs = gltf.get('accessors', [])
    acc_map, new_accs = {}, []
    for i, acc in enumerate(accs):
        if i in used_acc:
            acc_map[i] = len(new_accs)
            new_accs.append(acc)
    # each accessor's data copied on its own, packed tight (several often
    # share one block, with what's left out in it too); and each picture's
    new_views, out = [], bytearray()

    def keep(data, target=None):
        out.extend(b'\0' * (-len(out) % 8))
        view = {'buffer': 0, 'byteOffset': len(out), 'byteLength': len(data)}
        if target:
            view['target'] = target
        out.extend(data)
        new_views.append(view)
        return len(new_views) - 1
    copied = {}   # (the same data, used twice, copied once)
    for acc in new_accs:
        if acc.get('sparse'):
            sys.exit('a model with sparse accessors: not made smaller (not yet)')
        if 'bufferView' not in acc:
            continue
        v = views[acc['bufferView']]
        size = SIZES[acc['componentType']] * COUNTS[acc['type']]
        stride = v.get('byteStride', size)
        start = v.get('byteOffset', 0) + acc.get('byteOffset', 0)
        same = (acc['bufferView'], acc.get('byteOffset', 0), acc['count'], acc['componentType'], acc['type'])
        if same in copied:
            acc['bufferView'] = copied[same]
            acc.pop('byteOffset', None)
            continue
        # (a vertex's values start on a multiple of 4 bytes: three bytes of
        # color, say, are given a fourth, unused)
        step = size + (-size % 4) if COUNTS[acc['type']] > 1 else size
        pad = b'\0' * (step - size)
        data = b''.join(bytes(binary[start + i * stride:start + i * stride + size]) + pad for i in range(acc['count']))
        acc['bufferView'] = keep(data, v.get('target'))
        if step != size:
            new_views[-1]['byteStride'] = step
        copied[same] = acc['bufferView']
        acc.pop('byteOffset', None)
    view_map = {}
    for img in gltf.get('images', []):
        if 'bufferView' in img:
            old = img['bufferView']
            if old not in view_map:
                v = views[old]
                view_map[old] = keep(bytes(binary[v.get('byteOffset', 0):v.get('byteOffset', 0) + v['byteLength']]))
    for img in gltf.get('images', []):
        if 'bufferView' in img:
            img['bufferView'] = view_map[img['bufferView']]
    for m in gltf.get('meshes', []):
        for q in m['primitives']:
            q['attributes'] = {k: acc_map[v] for k, v in q['attributes'].items()}
            if 'indices' in q:
                q['indices'] = acc_map[q['indices']]
            if 'targets' in q:
                q['targets'] = [{k: acc_map[v] for k, v in t.items()} for t in q['targets']]
    for a in gltf.get('animations', []):
        for smp in a['samplers']:
            smp['input'] = acc_map[smp['input']]
            smp['output'] = acc_map[smp['output']]
    for sk in gltf.get('skins', []):
        if 'inverseBindMatrices' in sk:
            sk['inverseBindMatrices'] = acc_map[sk['inverseBindMatrices']]
    gltf['accessors'] = new_accs
    gltf['bufferViews'] = new_views
    return out


def multiply(a, b):
    return [sum(a[r + 4 * k] * b[k + 4 * c] for k in range(4)) for c in range(4) for r in range(4)]


def local_matrix(node):
    """a node's own matrix (column by column, as glTF keeps them)"""
    if 'matrix' in node:
        return list(node['matrix'])
    tx, ty, tz = node.get('translation', [0, 0, 0])
    x, y, z, w = node.get('rotation', [0, 0, 0, 1])
    sx, sy, sz = node.get('scale', [1, 1, 1])
    r = [1 - 2 * (y * y + z * z), 2 * (x * y + z * w), 2 * (x * z - y * w), 0,
         2 * (x * y - z * w), 1 - 2 * (x * x + z * z), 2 * (y * z + x * w), 0,
         2 * (x * z + y * w), 2 * (y * z - x * w), 1 - 2 * (x * x + y * y), 0,
         0, 0, 0, 1]
    return multiply([1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, tx, ty, tz, 1],
                    multiply(r, [sx, 0, 0, 0, 0, sy, 0, 0, 0, 0, sz, 0, 0, 0, 0, 1]))


def bounds(gltf):
    """the box round its meshes, as they're placed in its scene (as made:
    a skinned one's bones, as they stand, aren't counted): (lowest x, y, z),
    (highest x, y, z); or None, with no meshes"""
    lo, hi = [float('inf')] * 3, [float('-inf')] * 3

    def visit(i, parent):
        node = gltf['nodes'][i]
        m = multiply(parent, local_matrix(node))
        if 'mesh' in node:
            for q in gltf['meshes'][node['mesh']]['primitives']:
                acc = gltf['accessors'][q['attributes']['POSITION']]
                a, b = acc['min'], acc['max']
                for cx in (a[0], b[0]):
                    for cy in (a[1], b[1]):
                        for cz in (a[2], b[2]):
                            p = [m[k] * cx + m[4 + k] * cy + m[8 + k] * cz + m[12 + k] for k in range(3)]
                            for k in range(3):
                                lo[k] = min(lo[k], p[k])
                                hi[k] = max(hi[k], p[k])
        for c in node.get('children', []):
            visit(c, m)
    for i in gltf['scenes'][gltf.get('scene', 0)]['nodes']:
        visit(i, [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1])
    return (lo, hi) if lo[0] != float('inf') else None


def smaller_pictures(gltf, binary, most):
    """each picture bigger than most pixels, across or down, made smaller"""
    try:
        from PIL import Image
    except ImportError:
        sys.exit('--pictures needs Pillow, which reads and writes pictures: pip install pillow')
    import io
    views = gltf.get('bufferViews', [])
    for img in gltf.get('images', []):
        if 'bufferView' not in img:
            continue
        v = views[img['bufferView']]
        start = v.get('byteOffset', 0)
        picture = Image.open(io.BytesIO(bytes(binary[start:start + v['byteLength']])))
        if max(picture.size) <= most:
            continue
        picture.thumbnail((most, most))
        out = io.BytesIO()
        jpeg = img.get('mimeType') == 'image/jpeg'
        picture.save(out, 'JPEG' if jpeg else 'PNG', **({'quality': 88} if jpeg else {'optimize': True}))
        data = out.getvalue()
        img['bufferView'] = add_view(gltf, binary, data)   # (the old one's left out, as it's used no more)


def fill_animations(gltf, binary):
    """each animation moves every (node, path) any of them does: those it
    doesn't, held where the model has them"""
    anims = gltf.get('animations', [])
    moved = {(c['target'].get('node'), c['target']['path']) for a in anims for c in a['channels']
             if c['target']['path'] in ('translation', 'rotation', 'scale') and 'node' in c['target']}
    time_acc = None
    for a in anims:
        has = {(c['target'].get('node'), c['target']['path']) for c in a['channels']}
        for node, path in sorted(moved - has):
            if time_acc is None:
                view = add_view(gltf, binary, struct.pack('<f', 0.0))
                gltf['accessors'].append({'bufferView': view, 'componentType': 5126, 'count': 1,
                                          'type': 'SCALAR', 'min': [0.0], 'max': [0.0]})
                time_acc = len(gltf['accessors']) - 1
            rest = {'translation': [0, 0, 0], 'rotation': [0, 0, 0, 1], 'scale': [1, 1, 1]}[path]
            value = gltf['nodes'][node].get(path, rest)
            view = add_view(gltf, binary, struct.pack('<%df' % len(value), *value))
            gltf['accessors'].append({'bufferView': view, 'componentType': 5126, 'count': 1,
                                      'type': 'VEC4' if path == 'rotation' else 'VEC3'})
            a['samplers'].append({'input': time_acc, 'output': len(gltf['accessors']) - 1, 'interpolation': 'STEP'})
            a['channels'].append({'sampler': len(a['samplers']) - 1, 'target': {'node': node, 'path': path}})


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('input')
    p.add_argument('output', nargs='?')
    p.add_argument('--scale', type=float, default=1)
    p.add_argument('--turn', type=float, default=0)
    p.add_argument('--lift', type=float, default=0)
    p.add_argument('--forward', type=float, default=0)
    p.add_argument('--rename', action='append', default=[])
    p.add_argument('--keep', action='append', default=[])
    p.add_argument('--still', action='append', default=[])
    p.add_argument('--color', action='append', default=[])
    p.add_argument('--height', type=float, default=0)
    p.add_argument('--center', action='store_true')
    p.add_argument('--pictures', type=int, default=0)
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
        box = bounds(gltf)
        if box:
            lo, hi = box
            print(f'  size: {hi[0] - lo[0]:.2f} across, {hi[1] - lo[1]:.2f} high, {hi[2] - lo[2]:.2f} deep '
                  f'(from {lo[0]:.2f}, {lo[1]:.2f}, {lo[2]:.2f} to {hi[0]:.2f}, {hi[1]:.2f}, {hi[2]:.2f})')
        names = [m.get('name', '') for m in gltf.get('materials', [])]
        if names:
            print(f'  materials: {", ".join(names)}')
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
    for pair in a.color:
        name, hexa = pair.split('=', 1)
        found = [m for m in gltf.get('materials', []) if m.get('name') == name]
        if not found:
            sys.exit(f'no material named "{name}" (it has: {", ".join(m.get("name", "") for m in gltf.get("materials", []))})')
        h = hexa.lstrip('#')
        # (a glTF file keeps colors linear, as light adds up: from the sRGB a screen shows)
        srgb = [int(h[i:i + 2], 16) / 255 for i in (0, 2, 4)]
        linear = [c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4 for c in srgb]
        for m in found:
            pbr = m.setdefault('pbrMetallicRoughness', {})
            pbr['baseColorFactor'] = linear + [pbr.get('baseColorFactor', [1, 1, 1, 1])[3]]
            pbr.pop('baseColorTexture', None)
    for pair in a.still:
        new, old = pair.split('=', 1)
        add_still(gltf, binary, new, old)
    if a.keep:
        gltf['animations'] = [x for x in gltf.get('animations', []) if x.get('name') in a.keep]
        if not gltf['animations']:
            del gltf['animations']
    if a.pictures:
        smaller_pictures(gltf, binary, a.pictures)
    fill_animations(gltf, binary)
    box = bounds(gltf)
    if a.height and box:
        a.scale = a.height / (box[1][1] - box[0][1])
    if (a.height or a.center) and box:
        # (where its feet, and its middle, go, once it's scaled and turned)
        half = math.radians(a.turn)
        mx, mz = (box[0][0] + box[1][0]) / 2, (box[0][2] + box[1][2]) / 2
        if a.height:
            a.lift = -box[0][1] * a.scale
        if a.center:
            tx = math.cos(half) * mx + math.sin(half) * mz
            tz = -math.sin(half) * mx + math.cos(half) * mz
            a.side = -tx * a.scale
            a.forward = -tz * a.scale
    if binary:
        binary = compact(gltf, binary)
    side = getattr(a, 'side', 0)
    if a.scale != 1 or a.turn or a.lift or a.forward or side:
        half = math.radians(a.turn) / 2
        wrapper = {'name': 'prepared', 'children': list(gltf['scenes'][gltf.get('scene', 0)]['nodes'])}
        if a.scale != 1:
            wrapper['scale'] = [a.scale] * 3
        if a.turn:
            wrapper['rotation'] = [0, math.sin(half), 0, math.cos(half)]
        if a.lift or a.forward or side:
            wrapper['translation'] = [side, a.lift, a.forward]   # (a glTF file's front is +z)
        gltf['nodes'].append(wrapper)
        gltf['scenes'][gltf.get('scene', 0)]['nodes'] = [len(gltf['nodes']) - 1]
    if binary:
        gltf['buffers'][0]['byteLength'] = len(binary) + (-len(binary) % 4)
    write_glb(a.output, gltf, binary)


if __name__ == '__main__':
    main()
