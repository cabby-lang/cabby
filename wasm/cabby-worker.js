// cabby-worker.js — runs a Cabby program in a Web Worker, so a long one
// doesn't hold up the page, and can be stopped (the page ends the worker).
//
//     const w = new Worker('cabby-worker.js', { type: 'module' });
//     w.onmessage = e => { ... };   // { out: text }, { err: text }, { done: exitStatus }
//     w.postMessage({ source: 'int main {\n\tprintln "Hi"\n}\n', stdin: '' });
//
// It looks for cabby.wasm and cabby-lib.json (the libraries' headers, made
// by `make wasm-site`) next to itself.
import { runCabby } from './cabby.js';

const here = new URL('.', import.meta.url);
const ready = Promise.all([
	WebAssembly.compileStreaming(fetch(new URL('cabby.wasm', here))),
	fetch(new URL('cabby-lib.json', here)).then(r => r.json()),
]);

onmessage = async e => {
	const [wasm, lib] = await ready;
	const { source, stdin, args, files } = e.data;
	const res = await runCabby({
		wasm,
		files: { ...lib, ...(files || {}), '/home/prog.cby': source },
		args: ['/home/prog.cby', ...(args || [])],
		env: { CABBY_CWD: '/home' },
		stdin: stdin || '',
		stdout: text => text && postMessage({ out: text }),
		stderr: text => text && postMessage({ err: text }),
	});
	postMessage({ done: res.exit });
};
