// cabby-worker.js — runs a Cabby program in a Web Worker, so a long one
// doesn't hold up the page, and can be stopped (the page ends the worker).
//
//     const w = new Worker('cabby-worker.js', { type: 'module' });
//     w.onmessage = e => { ... };   // { out: text }, { err: text }, { done: exitStatus }
//
//     // a program's source, run by the interpreter (cabby.wasm)
//     w.postMessage({ source: 'int main {\n\tprintln "Hi"\n}\n', stdin: '' });
//
//     // or a program compiled with `cabby -c prog.cby --wasm`: its address
//     // (from where this file is, or a full one), or its bytes
//     w.postMessage({ program: 'programs/prog.wasm', args: ['a', 'b'] });
//
// Either can have `stdin`, `args` and `files` ({ '/home/x.txt': 'text' }:
// a program runs in /home). The interpreter and the libraries' headers
// (cabby.wasm and cabby-lib.json, from `make wasm-site`) are looked for
// next to this file, and fetched only once source is sent.
//
// A game (`use game`) draws on a canvas of the page's: give it as
// `canvas`, made with canvas.transferControlToOffscreen(), with the
// program (w.postMessage({ program, canvas }, [canvas])). The worker says
// when a window opens, { window: { title, width, height } }, and closes,
// { closed: true }; the page sends what the person does there as it
// happens: { input: { kind: 'down', key: 'Left' } } ('down' and 'up' for
// keys, as SDL names them, with ctrl, shift, alt and repeat; 'click' and
// 'move' with x and y in the canvas's pixels; 'close'). Without a canvas,
// each frame comes to the page instead, as { frame: ImageData }.
import { runCabby } from './cabby.js';

const here = new URL('.', import.meta.url);
let interpreter = null;
function getInterpreter() {
	interpreter ??= Promise.all([
		WebAssembly.compileStreaming(fetch(new URL('cabby.wasm', here))),
		fetch(new URL('cabby-lib.json', here)).then(r => r.json()),
	]);
	return interpreter;
}
const compiled = new Map();   // (each program's address: compiled once)
function getProgram(program) {
	if (typeof program !== 'string')
		return WebAssembly.compile(program);
	const url = new URL(program, here).href;
	if (!compiled.has(url))
		compiled.set(url, WebAssembly.compileStreaming(fetch(url).then(r => {
			if (!r.ok)
				throw new Error(`${program} isn't there (${r.status})`);
			return r;
		}))).get(url).catch(() => compiled.delete(url));   // (not kept if it failed: try again next time)
	return compiled.get(url);
}

// the font a game writes with (fonts/, beside this file), fetched once a
// game first runs, and put where the game looks for it
const FONT_AT = '/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf';
let font = null;
function getFont() {
	font ??= fetch(new URL('fonts/DejaVuSans.ttf', here)).then(r => r.ok ? r.arrayBuffer() : null).catch(() => null);
	return font;
}
async function withFont(files, isGame) {
	const f = isGame ? await getFont() : null;
	return f ? { ...files, [FONT_AT]: f } : files;
}
const drawsWindows = mod => WebAssembly.Module.imports(mod).some(i => i.module === 'cabby');

// what the person has done on the page, waiting for the program to ask
let queue = [];
let canvas = null, ctx = null;
const display = {
	window(title, width, height) {
		if (canvas) {
			canvas.width = width;
			canvas.height = height;
		}
		postMessage({ window: { title, width, height } });
	},
	show(rgba, width, height) {
		const frame = new ImageData(new Uint8ClampedArray(rgba), width, height);   // (a copy: the program's memory moves on)
		if (canvas) {
			if (canvas.width !== width || canvas.height !== height) {
				canvas.width = width;
				canvas.height = height;
			}
			ctx ??= canvas.getContext('2d');
			ctx.putImageData(frame, 0, 0);
		} else
			postMessage({ frame }, [frame.data.buffer]);
	},
	closed() {
		queue = [];
		postMessage({ closed: true });
	},
};
const input = max => queue.splice(0, max);

let running = false;
onmessage = async e => {
	if (e.data.input) {
		if (running)
			queue.push(e.data.input);
		return;
	}
	const { source, program, stdin, args, files } = e.data;
	if (e.data.canvas) {
		canvas = e.data.canvas;
		ctx = null;
	}
	const io = {
		env: { CABBY_CWD: '/home' },
		stdin: stdin || '',
		stdout: text => text && postMessage({ out: text }),
		stderr: text => text && postMessage({ err: text }),
		display, input,
	};
	let res;
	running = true;
	queue = [];
	try {
		if (program !== undefined) {
			const name = typeof program === 'string' ? program.split('/').pop().replace(/\.wasm$/, '') : 'program';
			const wasm = await getProgram(program);
			res = await runCabby({ wasm, name, files: await withFont(files || {}, drawsWindows(wasm)), args: args || [], ...io });
		} else {
			const [wasm, lib] = await getInterpreter();
			res = await runCabby({ wasm, files: await withFont({ ...lib, ...(files || {}), '/home/prog.cby': source },
					/^use (game|gui)\b/m.test(source)),
				args: ['/home/prog.cby', ...(args || [])], ...io });
		}
	} catch (err) {
		// (a program that couldn't be fetched or compiled)
		postMessage({ err: `couldn't start it: ${err.message}\n` });
		res = { exit: 127 };
	}
	running = false;
	postMessage({ done: res.exit });
};
