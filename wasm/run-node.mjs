// wasm/run-node.mjs — runs cabby.wasm under Node's WASI, for testing it
// without a browser:   node wasm/run-node.mjs prog.cby [arguments...]
// The cabby folder is the WebAssembly program's "/" (so `use` finds lib/),
// and /tmp is /tmp; the folder it's run from is passed as CABBY_CWD.
import { readFile } from 'node:fs/promises';
import { WASI } from 'node:wasi';
import { argv, cwd, env, exit } from 'node:process';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const root = path.dirname(path.dirname(fileURLToPath(import.meta.url)));
// a path on this computer, as the WebAssembly program sees it
function inside(p) {
	const full = path.resolve(cwd(), p);
	if (full === '/tmp' || full.startsWith('/tmp/')) return full;
	const rel = path.relative(root, full);
	return rel.startsWith('..') ? null : '/' + rel;
}
const args = argv.slice(2).map(a => {
	if (a.startsWith('-')) return a;
	const p = inside(a);
	return p && (a.includes('/') || a.endsWith('.cby')) ? p : a;
});
const wasi = new WASI({
	version: 'preview1', args: ['cabby', ...args], returnOnExit: true,
	env: { CABBY_LIB: '/lib', HOME: '/tmp', CABBY_CWD: inside('.') || '/', ...(env.CABBY_TEST_ENV ? JSON.parse(env.CABBY_TEST_ENV) : {}) },
	preopens: { '/': root, '/tmp': '/tmp' },
});
const mod = await WebAssembly.compile(await readFile(path.join(root, 'cabby.wasm')));
const inst = await WebAssembly.instantiate(mod, wasi.getImportObject());
exit(wasi.start(inst));
