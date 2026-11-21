# Cabby in a browser

Cabby runs in a web page, with nothing installed and nothing sent
anywhere, two ways:

- **Interpreted**: Cabby's interpreter, built for WebAssembly
  (`cabby.wasm`), runs a program from its source, so a page can take a
  program typed into it. It's the same interpreter as `cabby prog.cby`:
  the same checks, the same messages, the same results.
- **Compiled**: `cabby -c prog.cby --wasm` makes `prog.wasm`, which a page
  runs as fast as a native program (2 to 30 times faster than
  interpreted). For programs a page offers ready-made: a demo, a game.

Either way, windows and games (`use gui`, `use game`) show on the page,
on a canvas, and are played there with the keyboard and the mouse.

## Building it

It needs **wasi-sdk** 33 or newer (a clang for WebAssembly, with its C library), from
<https://github.com/WebAssembly/wasi-sdk/releases>: unpack the one for your
computer (`x86_64-linux`, `arm64-linux`, `arm64-macos`) into `~/tools`, where
the Makefile looks for it, or say where it is:

    make wasm-site                          # (finds ~/tools/wasi-sdk-*)
    make wasm-site WASI_SDK=/opt/wasi-sdk

`make wasm` makes `cabby.wasm`, and the runtime that compiled programs
need (`lib/wasm/libcabbyrt.a`, which `make install` installs too).

For windows and games it needs SDL 3 built for WebAssembly too, once:

    tools/build-sdl-wasm.sh      # (git and cmake; fetches SDL from GitHub)

That puts SDL in `build/wasm-sdl` (with two small changes,
`tools/sdl-wasi.patch`: no threads, as Emscripten's SDL has none either,
and nothing loaded as it runs). From then on `make wasm` builds the gui and
game libraries for WebAssembly as well (`lib/wasm/libcabbygui.a`,
`libcabbygame.a`), and puts them in `cabby.wasm`.
`make wasm-site` puts in `build/wasm-site` everything a page needs:

| File | What it is |
|---|---|
| `cabby.wasm` | the interpreter (about 1.7 MB, under 600 KB compressed; with gui and game, 2.9 MB and 900 KB) |
| `cabby.js` | runs it: `runCabby`, with the program's files kept in memory |
| `cabby-worker.js` | runs it in a Web Worker, so the page doesn't wait on it, and it can be stopped |
| `cabby-lib.json` | the libraries' headers (`lib/*.hby`), all in one file |
| `index.html` | a page to try it: an editor, Run and Stop, what the program reads, and what it prints, with a window or a game shown under the editor |
| `fonts/` | DejaVu Sans, the font windows and games write with (fetched only when one opens; its license is beside it) |

To try it on your computer:

    python3 -m http.server -d build/wasm-site    # then open http://localhost:8000

(A page loaded straight from a file, `file://`, can't start a worker.)

## Compiling for it

    cabby -c prog.cby --wasm               # prog.wasm
    cabby -c prog.cby --wasm -o game.wasm

It needs wasi-sdk too: `cabby` looks in `$CABBY_WASI_SDK`, then where it
was when Cabby was built, then `~/tools/wasi-sdk-*` and `/opt/wasi-sdk`.

And it needs the runtime that `make wasm` builds, which it looks for
where it finds the libraries' headers. The cabby folder's own `./cabby`
finds it in that folder's `lib/wasm`; an installed Cabby (`/usr/bin/cabby`,
from a package) finds it once it's installed too: `sudo make install`
after `make wasm`, or a package built after `make wasm` (the .deb takes
it in then). Or point any Cabby at the folder it was built in:

    CABBY_LIB=~/cabby/lib cabby -c prog.cby --wasm
A program is small: `hello` is 16 KB (7 KB compressed), and one reading
and writing JSON about 100 KB (40 KB); a game, with SDL in it, about
900 KB (250 KB). It needs nothing else: not
`cabby.wasm`, nor the headers.

A program that uses a library a browser hasn't (a window, the network)
still builds, and says so with a panic when it gets there.

## Using it from a web page

The simplest way is the worker. Send it a program and it sends back what
the program prints, a piece at a time, and then how it ended:

```js
let worker = new Worker('cabby-worker.js', { type: 'module' });
worker.onmessage = e => {
	if (e.data.out !== undefined) show(e.data.out);         // what it printed
	if (e.data.err !== undefined) showError(e.data.err);    // a panic's message, and the like
	if (e.data.done !== undefined) finished(e.data.done);   // its exit status: 0, or 101 after a panic
};
worker.postMessage({
	source: 'int main {\n\tprintln "Hi"\n}\n',
	stdin: 'Ada\n',                       // what input and read_line read (optional)
	args: [],                             // its arguments (optional)
	files: { '/home/names.txt': 'Ada\nAlan\n' },   // files it can read (optional)
});
```

The program is `/home/prog.cby`, run from `/home`, so `files` given as
`/home/x` are just `x` to it. To stop one, `worker.terminate()` and make a
new worker. A worker runs one program at a time.

A compiled program is sent the same way, as `program`: its address (from
where `cabby-worker.js` is, or a full one), or its bytes. It's fetched and
compiled once, however often it runs; `cabby.wasm` isn't fetched at all
for it.

```js
worker.postMessage({ program: 'programs/quest.wasm', args: ['--easy'], stdin: '' });
```

Without a worker, `runCabby` in `cabby.js` runs one directly, and gives back
every file as it was left (what the program wrote too). Its comments say
how. It works anywhere JavaScript runs WebAssembly with exceptions (Chrome
and Edge 137+, Firefox 131+, Safari 18.4+, on phones too), though a program that sleeps
holds up whatever it's running in, so in a page it's best in a worker.

## Windows and games

A program's window is drawn by SDL, in software, as Cabby's tests draw
one off screen, and the page shows each frame on a canvas. What the
person does there comes back as SDL's own events, so a window or a game
works as it does on a computer: keys held and pressed, clicks, the mouse
moving, the wheel, typing into text boxes, and closing (after which
`main` carries on).

The page gives the worker a canvas with the program, and sends it what
happens there:

```js
const canvas = document.querySelector('canvas').transferControlToOffscreen();
worker.postMessage({ program: 'programs/quest.wasm', canvas }, [canvas]);   // (or source)
worker.onmessage = e => {
	if (e.data.window) showIt(e.data.window);   // { title, width, height }: the window opened
	if (e.data.closed) hideIt();                // it closed
};
// as they happen: keys by SDL's names, places in the canvas's own pixels
worker.postMessage({ input: { kind: 'down', key: 'Left' } });          // and 'up'; with ctrl, shift, alt
worker.postMessage({ input: { kind: 'text', key: 'a' } });             // what a key types, for text boxes
worker.postMessage({ input: { kind: 'click', x: 120, y: 40 } });       // and 'release', 'move'
worker.postMessage({ input: { kind: 'wheel', x: 0, y: -1 } });
worker.postMessage({ input: { kind: 'close' } });
```

`index.html` does all of this, and is the example to copy: keys while the
canvas has the focus, the mouse scaled to the canvas's pixels, a
"Close the window" button, and for a phone, a row of arrow keys and a
keyboard button (a hidden text box, which brings up the phone's keyboard).

It needs JSPI, WebAssembly's promise integration (Chrome and Edge 137+,
Firefox 153+, Safari 27+, on phones too): it lets the program pause while
it waits for the next frame, so the worker hears the page meanwhile.
Without it, a window is shown but can't be played. Frames come 60 a
second, interpreted or compiled.

The page shows one window at a time (the one drawn last); there's no
sound yet, no files chosen from the computer (`gui`'s file choosers), and
no 3D (`world`).

## What a browser hasn't

A browser keeps a page away from the computer itself, so the libraries
that need it say so when they're used, with a panic such as *'rt_tcp_connect'
isn't here (in a browser)*:

- 3D (`world`), GTK windows (`gtk`), and the terminal's (`tui`, `tgame`)
- sound, video, speech, MIDI, printers, Bluetooth, D-Bus and the desktop
- the network, other programs (`shell`, `processes`) and threads
- SQLite, zlib, OpenSSL, and the time zones' database
- interpreted, C functions beyond the libraries' own (`extern`): there's
  no libffi, so each C function is called through a wrapper made for it
  beforehand (`wasm/calls.c`, made by `tools/make-wasm-calls.py` from
  every `extern` line in the libraries, tutorials and tests); variadic
  ones (`printf`) and callbacks (`qsort`) can't be. Compiled, they all
  work, as on a computer.
- the interactive session, and compiling (`-c`) in the page itself

Files are the program's own: those it's given and those it writes, kept in
memory until it ends. Interpreted, calls can be 1,000 deep (a browser's
stack is small), where a computer allows 25,000 or more; compiled, many
thousands, until the browser's stack is full (a panic: *stack overflow*).
The address checks the interpreter makes (reading past the end, an address
kept too long) aren't made by compiled programs, in a browser or out.

## Testing it

The tutorials and the program tests, run by `cabby.wasm` in Chromium (through
Playwright, which `npm install playwright` gives) and checked as `make test`
checks them; those needing what a browser hasn't are counted, not run:

    node tests/run_wasm_tests.cjs                      # the tutorials
    node tests/run_wasm_tests.cjs --cases              # the program tests
    node tests/run_wasm_tests.cjs --compiled [--cases] # each compiled with -c --wasm
    node tests/run_wasm_tests.cjs --play               # the page, played: a window and a game

(Node can run `cabby.wasm` too, with `wasm/run-node.mjs`, but Node 22's V8
can crash collecting garbage around WebAssembly's exceptions, so the
browser is what's tested.)
