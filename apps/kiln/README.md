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
| `kiln.cby` | starts the editor, on the model named (or `untitled.glb`) |
| `editor.hby` | the editor: the window, the view, choosing, the keys; bones, posing and the animation |
| `model.hby`, `model.cby` | the model: parts made of corners and faces, what's done to them, and `.glb` files read and written |
| `examples/` | scripts: `figure.cby` makes a small figure, rigged, that waves |
| `tests/` | its tests (`tests/run_kiln_tests.sh`, from `make test`) |

## The editor

The view: the **middle mouse button** (or **Alt** and the left) drags it
round what it looks at, with **Shift** across; the **wheel** (or `=` and
`-`) brings it nearer or further; the arrow keys turn it too, and **Home**
looks at the middle again. The grid is a metre a square, the red line x,
the blue z.

In **object mode**, a click chooses a part (its edges orange). **Tab**
edits it, and back. In **edit mode**, **1**, **2** and **3** choose
corners, edges or faces; a click chooses the nearest to the mouse, and
**Shift**-click adds more (or takes one away); **A** chooses all, or none.

| Key | Does |
|---|---|
| **G**, **R**, **S** | Moves, turns or scales what's chosen, following the mouse; then **X**, **Y** or **Z** keeps it to that axis. A click or **Enter** is done; **Escape** (or the right button) puts it back. Turning goes round the axis most nearly towards you, unless one's said. |
| **E** | Extrudes the faces chosen, then pulls them out, along the way they face. |
| **F** | A face of the corners chosen, in the order they were chosen. |
| **M** | Merges the corners chosen into one, at their middle. |
| **X**, **Delete** | Deletes what's chosen: a part, corners (and their faces), or faces. |
| **Shift-A**, then **1** to **6** | Adds a cube, plane, grid, cylinder, cone or sphere where the view looks: a new part, or (editing) more of this one. |
| **Shift-D** | A copy of the part, a metre along. |
| **C** | The part's next colour. |
| **F5** | Runs the model's script on it (below). |
| **Ctrl-S**, **Ctrl-Z** | Saves the `.glb`; undoes the last change. |

### Bones, skinning and posing (rigging)

| Key | Does |
|---|---|
| **B** (object mode) | Bones mode: the armature, at rest. A click chooses a bone (the one whose middle is nearest); **Shift-A** adds one where the view looks, a metre up; **E** extrudes a new bone from the chosen one's tail, and it follows the mouse; **G** moves the chosen bone's tail; **X** deletes it (those after it hang from what it hung from). **Tab** or **Escape**: back to objects. |
| **Ctrl-P** (object mode) | Skins the part chosen to the bones: each corner moved by the two nearest, the nearer more (automatic weights). A part edited since is at rest until it's skinned again. |
| **P** (object mode) | Pose mode: a click chooses a bone; **R** turns it round its head, following the mouse (round the axis most nearly towards you, or **X**, **Y**, **Z**); **Alt-R** puts it back at rest. The skinned parts move with the bones. |

### Animation

In pose mode, the time is at the top, in frames (24 a second):

| Key | Does |
|---|---|
| **,** and **.** | A frame back, or on; **Shift-,** back to the start. The bones are posed as the animation has them there. |
| **I** | Keys every bone's turn at this time, in the scene's animation (`action`, made by the first key). |
| **Space** | Plays the animation, round and round, or stops it. |

Between keys, each bone turns from one key's turn to the next's, the
shorter way round. Saved, the bones are the `.glb`'s skin, and the
animation one of its animations: a game plays it with `play "action"`.

### F5: a model's own script

A model's script is beside it, its name with `.cby`: `house.cby` for
`house.glb`. F5 saves the model, runs the script with the model's path
(`Argv[1]`), and reads the model again, with whatever the script did. If
the script fails, the model is as it was. (`cabby` is run by name; set
`CABBY` to where it is, if it isn't found.) `tests/grow.cby` is one.

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

### Bones (rigging) and animations

A scene's `Bones` are its armature; a part's `Joints` and `Weights` say
which bones move each corner, and how much.

| Member | What it is, or does |
|---|---|
| `int S.add_bone const str &Name, int Parent, const point3 &Head, const point3 &Tail` | A bone from Head to Tail, at rest, in the scene, hanging from Parent (-1: none): its number. |
| `B.Name`, `B.Parent`, `B.Head`, `B.Tail`, `B.Turn` | A bone's name, parent, ends at rest, and its pose: a `quat`, turned from rest round its head. |
| `void S.skin int Part` | Automatic weights: each corner moved by the two nearest bones, the nearer more. |
| `bool P.skinned` | Whether a part has a weight for each corner. |
| `list point3 S.posed_points int Part` | Its corners, as the bones have them now. |
| `quat S.posed_turn int B`, `point3 S.posed_head int B`, `point3 S.posed_by int B, const point3 &P` | A bone's turn with its parents', its head, and a place moved with it, posed. |
| `void S.rest` | Every bone at rest again. |
| `int S.action_named const str &Name` | The animation called that (made, if it isn't there): its number. |
| `void A.set_key int Bone, double Time, const quat &Turn`, `quat A.turn_at int Bone, double Time, const quat &As`, `double A.length` | A key; a bone's turn at a time, between keys; and the last key's time. |
| `void S.pose_at int Action, double Time` | The bones posed as the animation has them then. |

Turns are quaternions: `quat_turn "z", 90` is 90 degrees round z (as
`turn_points` turns), `quat_times A, B` is B and then A, `quat_apply Q, P`
turns a place, `quat_between A, B, T` is T of the way from A to B, and
`quat_back Q` undoes Q.
