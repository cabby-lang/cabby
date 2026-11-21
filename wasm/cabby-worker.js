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

onmessage = async e => {
	const { source, program, stdin, args, files } = e.data;
	const io = {
		env: { CABBY_CWD: '/home' },
		stdin: stdin || '',
		stdout: text => text && postMessage({ out: text }),
		stderr: text => text && postMessage({ err: text }),
	};
	let res;
	try {
		if (program !== undefined) {
			const name = typeof program === 'string' ? program.split('/').pop().replace(/\.wasm$/, '') : 'program';
			res = await runCabby({ wasm: await getProgram(program), name, files: files || {}, args: args || [], ...io });
		} else {
			const [wasm, lib] = await getInterpreter();
			res = await runCabby({ wasm, files: { ...lib, ...(files || {}), '/home/prog.cby': source },
				args: ['/home/prog.cby', ...(args || [])], ...io });
		}
	} catch (err) {
		// (a program that couldn't be fetched or compiled)
		postMessage({ err: `couldn't start it: ${err.message}\n` });
		res = { exit: 127 };
	}
	postMessage({ done: res.exit });
};
