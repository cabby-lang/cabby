#!/usr/bin/env python3
"""Makes tests/world/robot.glb: a small glTF model for the world library's
tests, written by hand (Python's standard library only) so its numbers are
known. It has what Blender exports: meshes hanging from nodes, a material
with a color and one with a picture in the file, a skin (an arm with a
shoulder and an elbow), and animations.

  Body      a box, 0.6 wide, 1 high, 0.4 deep, its feet at the origin; a
            picture on it (a 4 by 4 checkerboard, red and white)
    Head    a box 0.4 across, on top of the body; its own color (gold)
  Arm       a skinned column hanging from the shoulder, 0.9 long, bent by
  Shoulder  the shoulder (at the body's right, 0.9 up) and
    Elbow   the elbow, half way down
  animations: "Wave" (the shoulder turns the arm up, 1 second), "Nod" (the
  head tips forward and back, 1 second), "Hop" (the body jumps half a
  metre and down, 1 second)

The model faces +z, as glTF's do (Blender's -y). Run it from Cabby's
folder: python3 tools/make-test-model.py
"""
import json
import math
import struct
import zlib

bin_data = bytearray()
views = []
accessors = []


def add_view(data, target=None):
    while len(bin_data) % 4:
        bin_data.append(0)
    off = len(bin_data)
    bin_data.extend(data)
    v = {"buffer": 0, "byteOffset": off, "byteLength": len(data)}
    if target:
        v["target"] = target
    views.append(v)
    return len(views) - 1


def add_accessor(values, kind, comp=5126, target=None, minmax=False):
    sizes = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT4": 16}
    n = sizes[kind]
    fmt = {5126: "f", 5123: "H", 5121: "B"}[comp]
    flat = [x for v in values for x in (v if isinstance(v, (list, tuple)) else [v])]
    data = struct.pack("<%d%s" % (len(flat), fmt), *flat)
    a = {"bufferView": add_view(data, target), "componentType": comp, "count": len(values), "type": kind}
    if minmax:
        cols = [[v[i] if n > 1 else v for v in values] for i in range(n)]
        a["min"] = [min(c) for c in cols]
        a["max"] = [max(c) for c in cols]
    accessors.append(a)
    return len(accessors) - 1


def box(w, h, d, y0):
    """a box w by h by d, its bottom at y0: positions, normals, uvs, indices"""
    hw, hd = w / 2, d / 2
    faces = [
        ((0, 0, 1), [(-hw, 0, hd), (hw, 0, hd), (hw, h, hd), (-hw, h, hd)]),
        ((0, 0, -1), [(hw, 0, -hd), (-hw, 0, -hd), (-hw, h, -hd), (hw, h, -hd)]),
        ((1, 0, 0), [(hw, 0, hd), (hw, 0, -hd), (hw, h, -hd), (hw, h, hd)]),
        ((-1, 0, 0), [(-hw, 0, -hd), (-hw, 0, hd), (-hw, h, hd), (-hw, h, -hd)]),
        ((0, 1, 0), [(-hw, h, hd), (hw, h, hd), (hw, h, -hd), (-hw, h, -hd)]),
        ((0, -1, 0), [(-hw, 0, -hd), (hw, 0, -hd), (hw, 0, hd), (-hw, 0, hd)]),
    ]
    pos, nor, uv, idx = [], [], [], []
    for n, corners in faces:
        b = len(pos)
        for (x, y, z), (u, v) in zip(corners, [(0, 1), (1, 1), (1, 0), (0, 0)]):
            pos.append((x, y + y0, z))
            nor.append(n)
            uv.append((u, v))
        idx += [b, b + 1, b + 2, b, b + 2, b + 3]
    return pos, nor, uv, idx


def png(w, h, pixels):
    raw = b"".join(b"\0" + bytes(pixels[y * w * 3:(y + 1) * w * 3]) for y in range(h))

    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))


def quat_z(deg):
    a = math.radians(deg) / 2
    return (0.0, 0.0, math.sin(a), math.cos(a))


def quat_x(deg):
    a = math.radians(deg) / 2
    return (math.sin(a), 0.0, 0.0, math.cos(a))


meshes = []


def mesh_from(pos, nor, uv, idx, material, extra=None):
    attrs = {
        "POSITION": add_accessor(pos, "VEC3", target=34962, minmax=True),
        "NORMAL": add_accessor(nor, "VEC3", target=34962),
        "TEXCOORD_0": add_accessor(uv, "VEC2", target=34962),
    }
    if extra:
        attrs.update(extra)
    prim = {"attributes": attrs, "indices": add_accessor(idx, "SCALAR", 5123, target=34963), "material": material}
    meshes.append({"primitives": [prim]})
    return len(meshes) - 1


# the picture: a 4 by 4 checkerboard
checks = []
for y in range(4):
    for x in range(4):
        checks += [220, 40, 40] if (x + y) % 2 == 0 else [255, 255, 255]
image_view = add_view(png(4, 4, checks))

body_mesh = mesh_from(*box(0.6, 1.0, 0.4, 0.0), material=0)
head_mesh = mesh_from(*box(0.4, 0.4, 0.4, 0.0), material=1)

# the arm: a column 0.1 across, from the shoulder (y 0.9) down to y 0, in
# four bands; the top two follow the shoulder, the bottom two the elbow
apos, anor, auv, aidx, ajoints, aweights = [], [], [], [], [], []
bands = [0.9, 0.675, 0.45, 0.225, 0.0]
for i, y in enumerate(bands):
    for (dx, dz, n) in [(-0.05, 0.05, (0, 0, 1)), (0.05, 0.05, (0, 0, 1)), (0.05, -0.05, (0, 0, -1)), (-0.05, -0.05, (0, 0, -1))]:
        apos.append((0.4 + dx, y, dz))
        anor.append(n)
        auv.append((0.5, i / 4))
        ajoints.append((0, 0, 0, 0) if y > 0.45 else (1, 0, 0, 0))
        aweights.append((1.0, 0.0, 0.0, 0.0))
for i in range(4):
    a, b = 4 * i, 4 * (i + 1)
    for (p, q) in [(0, 1), (1, 2), (2, 3), (3, 0)]:
        aidx += [a + p, b + p, b + q, a + p, b + q, a + q]
arm_mesh = mesh_from(apos, anor, auv, aidx, material=1, extra={
    "JOINTS_0": add_accessor(ajoints, "VEC4", 5121, target=34962),
    "WEIGHTS_0": add_accessor(aweights, "VEC4", target=34962),
})

# inverse bind matrices: the joints at rest are only moved (shoulder 0.4,
# 0.9, 0; elbow 0.45 below it), so each is the opposite move
def inverse_move(x, y, z):
    return (1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -x, -y, -z, 1)


ibm = add_accessor([inverse_move(0.4, 0.9, 0), inverse_move(0.4, 0.45, 0)], "MAT4")

# animations
times = add_accessor([0.0, 0.5, 1.0], "SCALAR", minmax=True)
wave = add_accessor([quat_z(0), quat_z(75), quat_z(150)], "VEC4")
nod = add_accessor([quat_x(0), quat_x(20), quat_x(0)], "VEC4")
hop = add_accessor([(0, 0, 0), (0, 0.5, 0), (0, 0, 0)], "VEC3")

gltf = {
    "asset": {"version": "2.0", "generator": "Cabby's tools/make-test-model.py"},
    "scene": 0,
    "scenes": [{"nodes": [0, 2, 3]}],
    "nodes": [
        {"name": "Body", "mesh": body_mesh, "children": [1]},
        {"name": "Head", "mesh": head_mesh, "translation": [0, 1.0, 0]},
        {"name": "Arm", "mesh": arm_mesh, "skin": 0},
        {"name": "Shoulder", "translation": [0.4, 0.9, 0], "children": [4]},
        {"name": "Elbow", "translation": [0, -0.45, 0]},
    ],
    "meshes": meshes,
    "skins": [{"joints": [3, 4], "inverseBindMatrices": ibm}],
    "materials": [
        {"name": "Checks", "pbrMetallicRoughness": {"baseColorFactor": [1, 1, 1, 1], "baseColorTexture": {"index": 0}}},
        {"name": "Gold", "pbrMetallicRoughness": {"baseColorFactor": [1.0, 0.75, 0.1, 1]}},
    ],
    "textures": [{"source": 0, "sampler": 0}],
    "samplers": [{"magFilter": 9728, "minFilter": 9728}],
    "images": [{"bufferView": image_view, "mimeType": "image/png"}],
    "animations": [
        {"name": "Wave", "samplers": [{"input": times, "output": wave}], "channels": [{"sampler": 0, "target": {"node": 3, "path": "rotation"}}]},
        {"name": "Nod", "samplers": [{"input": times, "output": nod}], "channels": [{"sampler": 0, "target": {"node": 1, "path": "rotation"}}]},
        {"name": "Hop", "samplers": [{"input": times, "output": hop}], "channels": [{"sampler": 0, "target": {"node": 0, "path": "translation"}}]},
    ],
    "accessors": accessors,
    "bufferViews": views,
    "buffers": [{"byteLength": len(bin_data)}],
}

while len(bin_data) % 4:
    bin_data.append(0)
gltf["buffers"][0]["byteLength"] = len(bin_data)
js = json.dumps(gltf, separators=(",", ":")).encode()
while len(js) % 4:
    js += b" "
out = (struct.pack("<III", 0x46546C67, 2, 12 + 8 + len(js) + 8 + len(bin_data)) +
       struct.pack("<II", len(js), 0x4E4F534A) + js + struct.pack("<II", len(bin_data), 0x004E4942) + bytes(bin_data))
with open("tests/world/robot.glb", "wb") as f:
    f.write(out)
print("tests/world/robot.glb:", len(out), "bytes")
