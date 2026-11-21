// cabby.js — Cabby's interpreter (cabby.wasm) run in a browser, or anywhere
// JavaScript runs WebAssembly, with its files kept in memory.
//
//     import { runCabby } from './cabby.js';
//     const result = await runCabby({
//         wasm: await WebAssembly.compileStreaming(fetch('cabby.wasm')),
//         files: { '/lib/files.hby': '...', '/prog.cby': 'int main {\n\tprintln "Hi"\n}\n' },
//         args: ['/prog.cby'],
//         stdin: 'what input() and read_line read',
//         stdout: text => ..., stderr: text => ...,
//     });
//     result.exit        // the program's exit status (101 after a panic)
//     result.files       // every file, as it was left: what the program wrote too
//
// This is WASI (preview 1), the part cabby.wasm uses: the program's
// arguments and environment, its clock and random numbers, and files and
// folders, all in `files`, under one folder, "/". Sleeping waits by looking
// at the clock, so a program is best run in a Web Worker (cabby-worker.js),
// where waiting doesn't hold up the page, and which can be stopped.

const E = { SUCCESS: 0, BADF: 8, EXIST: 20, INVAL: 28, ISDIR: 31, NOENT: 44, NOSYS: 52, NOTDIR: 54, NOTEMPTY: 55 };
const FT = { CHAR: 2, DIR: 3, FILE: 4 };
const ROOT_FD = 3;

class Exit extends Error {
	constructor(code) { super('exit ' + code); this.code = code; }
}

// ---- the files: a tree of folders, each a Map of names to a folder or a file
function newDir(parent) { const d = { dir: new Map() }; d.parent = parent || d; return d; }
function newFile(bytes) { return { data: bytes || new Uint8Array(0), size: bytes ? bytes.length : 0 }; }

function splitPath(p) {
	const out = [];
	for (const part of p.split('/')) {
		if (!part || part === '.') continue;
		if (part === '..') out.pop();
		else out.push(part);
	}
	return out;
}

export async function runCabby(options) {
	const enc = new TextEncoder();
	const root = newDir();
	// put the given files in place, folders and all
	for (const [p, content] of Object.entries(options.files || {})) {
		const parts = splitPath(p);
		let d = root;
		for (const part of parts.slice(0, -1)) {
			if (!d.dir.has(part)) d.dir.set(part, newDir(d));
			d = d.dir.get(part);
		}
		const bytes = typeof content === 'string' ? enc.encode(content) : new Uint8Array(content);
		d.dir.set(parts[parts.length - 1], newFile(bytes));
	}
	const args = ['cabby', ...(options.args || [])];
	const env = Object.entries({ CABBY_LIB: '/lib', HOME: '/home', CABBY_CWD: '/', ...(options.env || {}) })
		.map(([k, v]) => `${k}=${v}`);
	const stdin = typeof options.stdin === 'string' ? enc.encode(options.stdin) : (options.stdin || new Uint8Array(0));
	let stdinAt = 0;
	const outDec = new TextDecoder(), errDec = new TextDecoder();
	const stdout = options.stdout || (() => {}), stderr = options.stderr || (() => {});

	// open files: fd -> { node, pos, append, path }
	const fds = new Map();
	fds.set(ROOT_FD, { node: root, pos: 0 });
	let nextFd = 4;

	let memory = null;
	const view = () => new DataView(memory.buffer);
	const bytes = () => new Uint8Array(memory.buffer);
	const str = (ptr, len) => new TextDecoder().decode(bytes().subarray(ptr, ptr + len));

	// the folder that holds path (from fd's folder: the folder itself, so it
	// is the same however it's moved), and its last name
	function lookup(fd, path) {
		const at = fds.get(fd);
		if (!at || !at.node.dir) return { err: E.BADF };
		const parts = path.split('/').filter(p => p && p !== '.');
		let d = at.node;
		for (const part of parts.slice(0, -1)) {
			if (part === '..') { d = d.parent; continue; }
			const n = d.dir.get(part);
			if (!n) return { err: E.NOENT };
			if (!n.dir) return { err: E.NOTDIR };
			d = n;
		}
		let name = parts[parts.length - 1];
		if (name === '..') { d = d.parent; name = undefined; }
		if (name === undefined) return { parent: d.parent, name: null, node: d };
		return { parent: d, name, node: d.dir.get(name) };
	}

	function fileStat(ptr, node, fdNum) {
		const v = view();
		for (let i = 0; i < 64; i += 8) v.setBigUint64(ptr + i, 0n, true);
		v.setBigUint64(ptr + 8, BigInt(fdNum || 1), true);
		v.setUint8(ptr + 16, node === 'char' ? FT.CHAR : node.dir ? FT.DIR : FT.FILE);
		v.setBigUint64(ptr + 24, 1n, true);
		v.setBigUint64(ptr + 32, BigInt(node.dir || node === 'char' ? 0 : node.size), true);
		const now = BigInt(Date.now()) * 1000000n;
		v.setBigUint64(ptr + 40, now, true);
		v.setBigUint64(ptr + 48, now, true);
		v.setBigUint64(ptr + 56, now, true);
	}

	function grow(f, need) {
		if (need <= f.data.length) return;
		const n = new Uint8Array(Math.max(need, f.data.length * 2, 256));
		n.set(f.data.subarray(0, f.size));
		f.data = n;
	}

	const iovs = (ptr, n) => {
		const v = view(), out = [];
		for (let i = 0; i < n; i++) out.push([v.getUint32(ptr + i * 8, true), v.getUint32(ptr + i * 8 + 4, true)]);
		return out;
	};

	const wasi = {
		args_sizes_get(argc, size) {
			view().setUint32(argc, args.length, true);
			view().setUint32(size, args.reduce((n, a) => n + enc.encode(a).length + 1, 0), true);
			return E.SUCCESS;
		},
		args_get(argv, buf) {
			let at = buf;
			args.forEach((a, i) => {
				view().setUint32(argv + i * 4, at, true);
				const b = enc.encode(a);
				bytes().set(b, at);
				bytes()[at + b.length] = 0;
				at += b.length + 1;
			});
			return E.SUCCESS;
		},
		environ_sizes_get(n, size) {
			view().setUint32(n, env.length, true);
			view().setUint32(size, env.reduce((k, a) => k + enc.encode(a).length + 1, 0), true);
			return E.SUCCESS;
		},
		environ_get(envp, buf) {
			let at = buf;
			env.forEach((a, i) => {
				view().setUint32(envp + i * 4, at, true);
				const b = enc.encode(a);
				bytes().set(b, at);
				bytes()[at + b.length] = 0;
				at += b.length + 1;
			});
			return E.SUCCESS;
		},
		clock_time_get(id, precision, out) {
			const ns = id === 0 ? BigInt(Date.now()) * 1000000n : BigInt(Math.round(performance.now() * 1e6));
			view().setBigUint64(out, ns, true);
			return E.SUCCESS;
		},
		random_get(buf, len) {
			for (let i = 0; i < len; i += 65536)
				crypto.getRandomValues(bytes().subarray(buf + i, buf + Math.min(len, i + 65536)));
			return E.SUCCESS;
		},
		proc_exit(code) { throw new Exit(code); },
		poll_oneoff(inp, out, nsubs, nevents) {
			// clock subscriptions (time.sleep): wait for the earliest
			const v = view();
			let wait = 0;
			for (let i = 0; i < nsubs; i++) {
				const s = inp + i * 48;
				if (v.getUint8(s + 8) === 0) {   // a clock
					const t = Number(v.getBigUint64(s + 24, true)) / 1e6;
					const abs = v.getUint16(s + 40, true) & 1;
					wait = Math.max(wait, abs ? t - performance.now() : t);
				}
			}
			const until = performance.now() + wait;
			while (performance.now() < until) { /* (a worker's own time) */ }
			for (let i = 0; i < nsubs; i++) {
				const e = out + i * 32;
				v.setBigUint64(e, v.getBigUint64(inp + i * 48, true), true);
				v.setUint16(e + 8, 0, true);
				v.setUint8(e + 10, v.getUint8(inp + i * 48 + 8));
			}
			v.setUint32(nevents, nsubs, true);
			return E.SUCCESS;
		},

		fd_write(fd, iov, n, written) {
			let total = 0;
			if (fd === 1 || fd === 2) {
				for (const [p, len] of iovs(iov, n)) {
					const chunk = bytes().slice(p, p + len);
					(fd === 1 ? stdout : stderr)((fd === 1 ? outDec : errDec).decode(chunk, { stream: true }));
					total += len;
				}
			} else {
				const f = fds.get(fd);
				if (!f || f.node.dir) return E.BADF;
				for (const [p, len] of iovs(iov, n)) {
					if (f.append) f.pos = f.node.size;
					grow(f.node, f.pos + len);
					f.node.data.set(bytes().subarray(p, p + len), f.pos);
					f.pos += len;
					f.node.size = Math.max(f.node.size, f.pos);
					total += len;
				}
			}
			view().setUint32(written, total, true);
			return E.SUCCESS;
		},
		fd_read(fd, iov, n, nread) {
			let total = 0;
			for (const [p, len] of iovs(iov, n)) {
				let chunk;
				if (fd === 0) {
					chunk = stdin.subarray(stdinAt, stdinAt + len);
					stdinAt += chunk.length;
				} else {
					const f = fds.get(fd);
					if (!f || f.node.dir) return E.BADF;
					chunk = f.node.data.subarray(f.pos, Math.min(f.node.size, f.pos + len));
					f.pos += chunk.length;
				}
				bytes().set(chunk, p);
				total += chunk.length;
				if (chunk.length < len) break;
			}
			view().setUint32(nread, total, true);
			return E.SUCCESS;
		},
		fd_seek(fd, offset, whence, out) {
			const f = fds.get(fd);
			if (!f || f.node.dir) return fd <= 2 ? E.INVAL : E.BADF;
			const base = whence === 0 ? 0 : whence === 1 ? f.pos : f.node.size;
			const to = base + Number(offset);
			if (to < 0) return E.INVAL;
			f.pos = to;
			view().setBigUint64(out, BigInt(to), true);
			return E.SUCCESS;
		},
		fd_close(fd) { if (fd > ROOT_FD) fds.delete(fd); return E.SUCCESS; },
		fd_fdstat_get(fd, ptr) {
			const v = view();
			const f = fds.get(fd);
			if (fd > 2 && !f) return E.BADF;
			v.setUint8(ptr, fd <= 2 ? FT.CHAR : f.node.dir ? FT.DIR : FT.FILE);
			v.setUint16(ptr + 2, f && f.append ? 1 : 0, true);
			v.setBigUint64(ptr + 8, 0xFFFFFFFFFFFFFFFFn, true);
			v.setBigUint64(ptr + 16, 0xFFFFFFFFFFFFFFFFn, true);
			return E.SUCCESS;
		},
		fd_fdstat_set_flags() { return E.SUCCESS; },
		fd_filestat_get(fd, ptr) {
			if (fd <= 2) { fileStat(ptr, 'char', fd); return E.SUCCESS; }
			const f = fds.get(fd);
			if (!f) return E.BADF;
			fileStat(ptr, f.node, fd);
			return E.SUCCESS;
		},
		fd_filestat_set_size(fd, size) {
			const f = fds.get(fd);
			if (!f || f.node.dir) return E.BADF;
			grow(f.node, Number(size));
			f.node.data.fill(0, Math.min(f.node.size, Number(size)), Number(size));
			f.node.size = Number(size);
			return E.SUCCESS;
		},
		fd_prestat_get(fd, ptr) {
			if (fd !== ROOT_FD) return E.BADF;
			view().setUint8(ptr, 0);
			view().setUint32(ptr + 4, 1, true);
			return E.SUCCESS;
		},
		fd_prestat_dir_name(fd, ptr, len) {
			if (fd !== ROOT_FD) return E.BADF;
			bytes()[ptr] = 47;   // "/"
			return E.SUCCESS;
		},
		fd_readdir(fd, buf, len, cookie, used) {
			const f = fds.get(fd);
			if (!f || !f.node.dir) return E.NOTDIR;
			const names = ['.', '..', ...f.node.dir.keys()];
			let at = 0;
			for (let i = Number(cookie); i < names.length; i++) {
				const name = enc.encode(names[i]);
				const node = i < 2 ? f.node : f.node.dir.get(names[i]);
				const head = new Uint8Array(24);
				const hv = new DataView(head.buffer);
				hv.setBigUint64(0, BigInt(i + 1), true);
				hv.setBigUint64(8, BigInt(i + 1), true);
				hv.setUint32(16, name.length, true);
				hv.setUint8(20, node.dir ? FT.DIR : FT.FILE);
				const entry = new Uint8Array(24 + name.length);
				entry.set(head);
				entry.set(name, 24);
				const room = Math.min(entry.length, len - at);
				bytes().set(entry.subarray(0, room), buf + at);
				at += room;
				if (at >= len) break;
			}
			view().setUint32(used, at, true);
			return E.SUCCESS;
		},
		path_open(fd, dirflags, p, plen, oflags, rights, inherit, fdflags, out) {
			const r = lookup(fd, str(p, plen));
			if (r.err) return r.err;
			let node = r.node;
			if (!node) {
				if (!(oflags & 1)) return E.NOENT;
				if (!r.name) return E.INVAL;
				node = newFile();
				r.parent.dir.set(r.name, node);
			} else if (oflags & 4) {
				return E.EXIST;
			}
			if ((oflags & 2) && !node.dir) return E.NOTDIR;
			if (oflags & 8) {
				if (node.dir) return E.ISDIR;
				node.size = 0;
			}
			const n = nextFd++;
			fds.set(n, { node, pos: 0, append: (fdflags & 1) !== 0 });
			view().setUint32(out, n, true);
			return E.SUCCESS;
		},
		path_filestat_get(fd, flags, p, plen, ptr) {
			const r = lookup(fd, str(p, plen));
			if (r.err) return r.err;
			if (!r.node) return E.NOENT;
			fileStat(ptr, r.node);
			return E.SUCCESS;
		},
		path_create_directory(fd, p, plen) {
			const r = lookup(fd, str(p, plen));
			if (r.err) return r.err;
			if (r.node) return E.EXIST;
			r.parent.dir.set(r.name, newDir(r.parent));
			return E.SUCCESS;
		},
		path_remove_directory(fd, p, plen) {
			const r = lookup(fd, str(p, plen));
			if (r.err) return r.err;
			if (!r.node) return E.NOENT;
			if (!r.node.dir) return E.NOTDIR;
			if (r.node.dir.size) return E.NOTEMPTY;
			r.parent.dir.delete(r.name);
			return E.SUCCESS;
		},
		path_unlink_file(fd, p, plen) {
			const r = lookup(fd, str(p, plen));
			if (r.err) return r.err;
			if (!r.node) return E.NOENT;
			if (r.node.dir) return E.ISDIR;
			r.parent.dir.delete(r.name);
			return E.SUCCESS;
		},
		path_rename(fd, p, plen, fd2, q, qlen) {
			const a = lookup(fd, str(p, plen)), b = lookup(fd2, str(q, qlen));
			if (a.err) return a.err;
			if (b.err) return b.err;
			if (!a.node) return E.NOENT;
			if (!a.name || !b.name) return E.INVAL;
			a.parent.dir.delete(a.name);
			b.parent.dir.set(b.name, a.node);
			if (a.node.dir) a.node.parent = b.parent;
			return E.SUCCESS;
		},
		path_readlink() { return E.INVAL; },
	};

	const instance = await WebAssembly.instantiate(options.wasm, { wasi_snapshot_preview1: wasi });
	memory = instance.exports.memory;
	let exit = 0;
	try {
		instance.exports._start();
	} catch (e) {
		if (e instanceof Exit) exit = e.code;
		else {
			stderr(outDec.decode() + `cabby.wasm stopped: ${e.message}\n`);
			exit = 134;
		}
	}
	stdout(outDec.decode());
	stderr(errDec.decode());
	// the files as they were left
	const files = {};
	(function walk(d, at) {
		for (const [name, n] of d.dir) {
			if (n.dir) walk(n, at + name + '/');
			else files[at + name] = n.data.slice(0, n.size);
		}
	})(root, '/');
	return { exit, files };
}
