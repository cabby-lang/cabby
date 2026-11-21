# Cabby in a browser

Cabby's interpreter, built for WebAssembly, runs a program in a web page,
with nothing installed and nothing sent anywhere. It's the same interpreter
as `cabby prog.cby`: the same checks, the same messages, the same results.

## Building it

It needs **wasi-sdk** (a clang for WebAssembly, with its C library), from
<https://github.com/WebAssembly/wasi-sdk/releases>: unpack the one for your
computer (`x86_64-linux`, `arm64-linux`, `arm64-macos`) into `~/tools`, where
the Makefile looks for it, or say where it is:

    make wasm-site                          # (finds ~/tools/wasi-sdk-*)
    make wasm-site WASI_SDK=/opt/wasi-sdk

`make wasm` makes just `cabby.wasm`. `make wasm-site` puts in
`build/wasm-site` everything a page needs:

| File | What it is |
|---|---|
| `cabby.wasm` | the interpreter (about 1.7 MB; under 600 KB compressed) |
| `cabby.js` | runs it: `runCabby`, with the program's files kept in memory |
| `cabby-worker.js` | runs it in a Web Worker, so the page doesn't wait on it, and it can be stopped |
| `cabby-lib.json` | the libraries' headers (`lib/*.hby`), all in one file |
| `index.html` | a page to try it: an editor, Run and Stop, what the program reads, and what it prints |

To try it on your computer:

    python3 -m http.server -d build/wasm-site    # then open http://localhost:8000

(A page loaded straight from a file, `file://`, can't start a worker.)

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

Without a worker, `runCabby` in `cabby.js` runs one directly, and gives back
every file as it was left (what the program wrote too). Its comments say
how. It works anywhere JavaScript runs WebAssembly with exceptions (Chrome
and Edge 137+, Firefox 131+, Safari 18.4+, on phones too), though a program that sleeps
holds up whatever it's running in, so in a page it's best in a worker.

## What a browser hasn't

A browser keeps a page away from the computer itself, so the libraries
that need it say so when they're used, with a panic such as *'rt_tcp_connect'
isn't here (in a browser)*:

- windows and games (`gui`, `game`, `world`), and the terminal's (`tui`, `tgame`)
- sound, video, speech, MIDI, printers, Bluetooth, D-Bus and the desktop
- the network, other programs (`shell`, `processes`) and threads
- SQLite, zlib, OpenSSL, and the time zones' database
- C functions beyond the libraries' own (`extern`): there's no libffi, so
  each C function is called through a wrapper made for it beforehand
  (`wasm/calls.c`, made by `tools/make-wasm-calls.py` from every `extern`
  line in the libraries, tutorials and tests); variadic ones (`printf`) and
  callbacks (`qsort`) can't be
- the interactive session, and compiling (`-c`)

Files are the program's own: those it's given and those it writes, kept in
memory until it ends. Calls can be 1,000 deep (a browser's stack is small),
where a computer allows 25,000 or more.

## Testing it

The tutorials and the program tests, run by `cabby.wasm` in Chromium (through
Playwright, which `npm install playwright` gives) and checked as `make test`
checks them; those needing what a browser hasn't are counted, not run:

    node tests/run_wasm_tests.cjs           # the tutorials
    node tests/run_wasm_tests.cjs --cases   # the program tests

(Node can run `cabby.wasm` too, with `wasm/run-node.mjs`, but Node 22's V8
can crash collecting garbage around WebAssembly's exceptions, so the
browser is what's tested.)
