# Kiln

A small 3D modeller, as Blender is, written in Cabby: only what's needed to
begin with, and more as it's needed. It reads and writes `.glb` files (the
3D quest's models are `.glb`), and it's scripted in Cabby, as Blender is
in Python.

```text
cabby kiln.cby [model.glb]      (the editor, from this folder)
```

What's here:

| File | What it is |
|---|---|
| `model.hby`, `model.cby` | the model: parts made of corners and faces, what's done to them, and `.glb` files read and written |
| `tests/` | its tests (`tests/run_kiln_tests.sh`, from `make test`) |

## The model, for scripts

A script is a Cabby program that uses the model, as the editor does:

```cabby
use local kiln/model            // (from the folder kiln is in)

int main {
	scene S = load_glb "house.glb"
	part Roof = cone_part 4, 1.2, 0.8
	Roof.Name = S.free_name("roof")
	Roof.Color = "#a0522d"
	Roof.AtY = 1.4
	S.Parts.add Roof
	S.save_glb "house.glb"
}
```

Distances are in metres, y is up and -z is forward, as in the world
library. A `.glb` is turned round as it's written and read (its front is
+z, as Blender's is), so what's in front in Kiln is in front in a game, and
in Blender.

### A scene

| Member | What it does |
|---|---|
| `scene load_glb const str &Path` | A `.glb` file, read: each mesh a part, its colour and place. Corners in the same place are made one again, and the triangles of a face Kiln wrote are joined back into it. |
| `list part Parts` | Its parts. |
| `void save_glb const str &Path` | Written as a `.glb` file: each part a mesh, flat (each face its own way), its colour and place. |
| `void keep`, `bool undo` | Keeps how it is now, before a change; and goes back to how it was before the last change (false: nothing to undo). |
| `int part_named const str &Name`, `str free_name const str &Name` | The part called that (-1: none); and a name not taken yet ("cube", "cube.2" ...). |

### Shapes to begin with

Each is a part, its middle at 0, 0, 0, grey (`"#c8c8c8"`):

| Function | What it makes |
|---|---|
| `part cube_part double Size` | a box, Size each way |
| `part plane_part double Size` | flat, facing up |
| `part grid_part int Across, double Size` | flat, in Across by Across squares |
| `part cylinder_part int Sides, double Radius, double Height` | standing up |
| `part cone_part int Sides, double Radius, double Height` | its point up |
| `part sphere_part int Rings, int Sides, double Radius` | a ball, in bands |

### A part

| Member | What it is, or does |
|---|---|
| `str Name`, `str Color` | Its name, and its colour (`"#rrggbb"`). |
| `list point3 Points` | Its corners, each `X`, `Y`, `Z`, from its own middle. |
| `list list int Faces` | Its faces: each one's corners, counterclockwise seen from the side it faces (3 or more). |
| `double AtX`, `AtY`, `AtZ` | Where it is in the scene. |
| `int add_point double X, double Y, double Z`, `int add_face const list int &Corners` | A corner, or a face of corners: its number. |
| `void move_points const list int &Chosen, double Dx, double Dy, double Dz`, `void move_all ...` | Moves some corners (or all of them). |
| `void scale_points const list int &Chosen, double Kx, double Ky, double Kz, const point3 &Middle` | Each that many times as far from Middle, each way. |
| `void turn_points const list int &Chosen, const str &Axis, double Degrees, const point3 &Middle` | Turned round `"x"`, `"y"` or `"z"` through Middle. |
| `list int extrude const list int &Chosen` | The faces chosen pulled away from the rest: their corners copied, the faces moved onto the copies, and sides joining them. The new corners, to move. |
| `void delete_points const list int &Chosen`, `void delete_faces const list int &Chosen` | Corners gone (and the faces that used them); or faces gone (and the corners only they used). |
| `void merge_points const list int &Chosen` | The corners made one, at their middle. |
| `point3 normal_of int F`, `point3 normal_of_faces const list int &Chosen`, `point3 middle_of const list int &Corners` | The way a face faces (or some faces, together); the middle of some corners. |
| `list int edges` | Each edge once, two corners each. |
| `void triangles_into list double &Points, list int &Triangles` | Its faces as triangles, for the world library's `add_mesh`. |
| `part copy` | A copy of its own. |

`light_of "#rrggbb"` and `color_of R, G, B` turn a colour into the light a
`.glb` keeps for it (0 to 1 each), and back.
