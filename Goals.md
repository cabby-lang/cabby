# Cabby: goals

Tick a box by changing `[ ]` to `[x]`. Text in *italics* under a goal is
planning notes; everything else is the goal as first written.

*Rebuilt on 27 November 2026 from TIMELINE.md and our conversations, after
the last copy was lost with a workspace, and kept in git from then on.
What's done is kept short here (TIMELINE.md has each version's story);
what's still to come has the detail.*

---

## Done, in short

- [x] The language, to 1.0.0 (14 to 21 November): types, objects with
  multiple inheritance, ownership as Rust's, `link T`, `else` after a
  statement, lists, lookups, sorting, text tools; the interpreter (with
  bytecode), the compiler (LLVM), `--check`, `--style`, the interactive
  session; the manual (tutorials 001 onwards) and its tests.
- [x] *Speed: the benchmarks in `bench/` against Python, C and Rust
  (0.43.1: compiled objects quicker than C's).*
- [x] *The libraries: gui (SDL, with GTK as `use gtk`), tui, game and
  tgame, shell, processes, threads, networks (web servers, WebSockets,
  HTTPS), emails, secrets, os and linux, users, images, sound, MIDI,
  speech, videos, archives, databases, csv, json, bluetooth, screens,
  mouse, keyboard, brightness, printing, desktop and power, accessibility
  (AT-SPI).*
- [x] *Packages: Arch, a Debian `.deb`, Homebrew; the website, with the
  manual a page per chapter.*
- [x] *Cabby in a browser (1.1.0 to 1.3.1): the interpreter as
  WebAssembly, `cabby -c --wasm`, windows and games on a page; the
  playground.*
- [x] *The playground takes arguments (the website's PR #1, 27 November):
  an Arguments box, in shared links too, and "Try it" fills it from a
  code block's `// args:` line.*
- [x] *3D, `use world` (0.10.0 on): shapes, light, a camera, text over it,
  models from Blender (glTF) with their bones and animations, shadows
  (1.4.0), physics (1.5.0), wedges (1.15.0), glowing shapes (1.20.0),
  models' colors as Blender shows them, and older materials (1.26.0).*
- [x] *Cabby Quest in 2D (tutorials 077 to 088).*
- [x] *Cabby Quest in 3D, parts 1 to 18 (tutorials 108 to 125, 1.6.0 to
  1.30.0): the town, areas joined edge to edge, people, the hills,
  battles, Rufus, the baker's spoon and saving, the title and menus, the
  lake and swimming, the day and the rain, the cave, sound, the coast,
  Ember following, arriving by train, trains every three minutes and
  waking beside the elder, and the models (Ember the fox, Bruno the black
  dog, the elder the old man, the field mouse, the hill crow, the gulls),
  credited in `tutorials/models/CREDITS.md`.*

## Waiting to be merged

- [ ] *#60, Cabby 1.31.0: `key_hold "shift"` (and ctrl, alt) in games and
  worlds; world text in Cabby's own font under a test script (the Debian
  failure); `make wasm` says where to get wasi-sdk; the GTK and manual
  tests skip videos without FFmpeg.*

---

## Cabby Quest in 3D: still to come

- [x] Cabbyballs: after a few quests, the elder gives the hero Cabbyballs
  to catch wild ones, and the book that lists what's been caught.
  - [x] *The menu shows the book, the Cabbymon in the party, and the
    bag (what's carried). Done (1.32.0, tutorial 126).*
  - [x] *The first in the party follows the hero (Ember, until then).
    Done (1.33.0, tutorial 127): who leads chosen in the menu; the
    party battles, the next sent out when one's worn out.*
  - [x] *Switching who's out in a battle, and the experience shared
    among all who battled. Done (1.36.0, tutorial 129).*
  - [x] *The Cabbyball thrown: the wild one shrinking in, wobbles, and
    caught or bursting open. Done (1.38.0, tutorial 131).*
  - [x] *New Cabbymon from your models: the bear, the border collie, the
    bison, the cow. Done (1.37.0, tutorial 130): in the woods and the
    meadow, which grow tall grass.*
  - [ ] *One day a fishing one (the anglerfish).*
- [x] The town living through the day: the baker can't open the bakery
  until the spoon is found, and the spoon can only be found by day.
  *Done (1.34.0, tutorial 128): the baker keeps hours, her shop opens
  once the spoon's back; a bed, and the days counted.*
- [ ] Choosing the hero at the start: Majd the Boy or Majd the Girl, and
  the rival is the one not chosen.
  - [ ] *Both have far too many triangles (the girl 25,000, the boy
    82,500): you're decimating them in Blender (to about 3,000 each)
    first.*
- [x] Townspeople from your models: the bearded man (walk, run, idle,
  fight, death), the low-poly dude, male and female (walking).
  *Done (1.39.0, tutorial 132): the baker (the casual woman), the farmer
  (the bearded man), the traveller (the low-poly man). The low-poly
  woman's sportswear didn't suit the baker; the dude isn't used yet.*
- [x] Buildings and things from your models: the barn, the old building,
  the double bed (the hero's home), a fire (the bakery's oven, a camp
  fire); the yacht for the coast's boats; a few trees from the tree pack
  (split into a file each).
  - [x] *The double bed, the fire (the oven, campfires), the yacht, and
    the long table. Done (1.42.0, tutorial 135).*
  - [x] *The barn, the old building, and trees from the tree pack. Done
    (1.44.0, tutorial 137): a farm in the meadow, and Pinewood's store.*
  - [ ] *Make the big pictures smaller first: the bus station (5 MB), the
    old house (17 MB).*
    *(`tools/prepare-model.py --pictures 1024` does it now, 1.42.0.)*
- [x] A credits screen in the game, from `tutorials/models/CREDITS.md`.
  *Done (1.41.0, tutorial 134): on the title, rolling up over the town.*
- [ ] The elder walking: the old man's model has no walk animation (he
  stands idle as he moves). *Another model, or one made in Blender.*
- [x] A ticket, bought later in the game, to take the train to new areas
  without the long way round. *Done (1.40.0, tutorial 133): to the coast
  and back, once Rufus is beaten there; denarii, won in battles.*
- [x] A forest, entered by a way as the cave is, dimmer outside but not
  as dark as a cave, with lots of grass to go through, and a new town on
  the other side. *Done (1.43.0, tutorial 136): the Deep Forest, and
  Pinewood.*
- [x] Fly, with a bird in the party, but only to towns been to. *Done
  (1.45.0, tutorial 138): up and away, and a fade.*
- [ ] Flying to a place on a Cabbymon: not off the screen with a fade,
  but across the whole map, the world seen passing below.
- [ ] The 3D quest in a browser (using Cabby's own drawing).
- [ ] *Small ones: let the crow flap, not hop (its model has "flap" and
  "glide") (done, 1.41.0: as it follows); Ember running after the hero
  when they're far ahead.*

## A third kind of game: open world, and physics

- [ ] A tutorial series in an open-world style (as in Assassin's Creed or
  Pokémon Scarlet): the hero moves anywhere, on X and Z, with W, A, S and
  D, and jumps; the camera moves with the arrow keys. With 2D, and 3D on
  tiles, that's three kinds of game, each its own series, going into much
  more detail, every small step shown.
  - [ ] *Physics with models that act on the world: fluids, cloth, glass
    shattering, gravity's effects (building on 1.5.0's falling, landing,
    bouncing and stacking).*
  - [ ] *Your `play.cby` is the seed of it: Majd the Boy walking and
    running (Shift, from 1.31.0), a phoenix on his shoulder, coins to
    collect.*

## The language and its tools

- [x] C files of your own, seen by Cabby without making a library first:
  `use local mylib` with `mylib.c` beside `mylib.hby`.
  - [x] *The interpreter compiles it to a `.so` (kept, and made again
    only when the `.c` changes) and loads it; `cabby -c` compiles it into
    the program. Done (1.35.0): kept in `~/.cache/cabby` (or
    `$CABBY_CACHE`); flags in `mylib.link`; manual chapter 33.*
  - [ ] *Today: `libcabbymylib.so` (for the interpreter), `.a` and
    `mylib.link` (for `-c`) in a folder, and `CABBY_LIB` pointing at it.*
- [ ] Libraries written in Cabby, compiled once, used from the
  interpreter and the interactive session at compiled speed: a library
  for Project X, used again in Project Y while trying things out.
  - [ ] *`cabby -c --library`: a library built from Cabby code, loaded
    as a native library is; the interpreter's values and the compiled
    code agreeing on how data is laid out.*
- [ ] WebAssembly on the Mac: wasi-sdk (arm64-macos) into `~/tools`, then
  `xattr -dr com.apple.quarantine ~/tools/wasi-sdk-*`; `make wasm`.

## Further off

- [ ] Android.
- [ ] Microcontrollers.
- [ ] Cabby on Docker, reached through a web browser.
- [ ] *The name: cabby.readthedocs.io is EclecticIQ's Cabby, a Python
  client for TAXII (sharing cyber-security data), years old. A different
  field: no need to rename. A trademark search would settle it, if ever
  wanted.*
