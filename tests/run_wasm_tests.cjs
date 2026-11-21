// tests/run_wasm_tests.cjs — the tutorials run by cabby.wasm in a browser
// (Chromium, through Playwright), each checked against the output at its
// end, as tests/run_tutorial_tests.sh checks them on a computer.
//
//     node tests/run_wasm_tests.cjs [--cases] [--compiled] [names...]
//     node tests/run_wasm_tests.cjs --play
//
// --cases runs the program tests (tests/run_cases.cby) instead; --compiled
// compiles each with `cabby -c --wasm` and runs what that makes, in place
// of cabby.wasm interpreting it. --play uses the page `make wasm-site`
// makes (build/wasm-site) as a person would: a window and a game, typed
// into, clicked and closed, interpreted and compiled.
//
// Those that need the computer itself (a window, a server, the desktop)
// are listed in WASM_SKIP below, with why: in a browser they say what's
// missing instead.
const fs = require('fs');
const os = require('os');
const { execFileSync } = require('child_process');
const path = require('path');
const { chromium } = require('playwright');

const ROOT = path.dirname(__dirname);
const TUT = path.join(ROOT, 'tutorials');

// what each needs that a browser doesn't have
const WASM_SKIP = {
	'027-c-functions': 'C functions beyond the libraries\' (printf)', '028-addresses': 'callbacks (qsort)',
	'034-processes': 'other programs', '041-downloads': 'the network',
	'042-web-addresses': 'the network', '049-archives': 'zlib', '050-databases': 'SQLite', '062-terminal-window': 'a terminal',
	'063-workers': 'threads', '064-done-later': 'threads (a worker)', '065-progress': 'threads (a worker)',
	'066-painting': 'threads (workers)', '074-songs': 'sound', '076-keyboard': 'MIDI', '091-chat-server': 'the network',
	'092-web-server': 'the network', '093-websockets': 'the network', '094-encrypted': 'the network',
	'095-emails': 'the network', '096-the-computer': 'the computer', '097-power-and-volume': 'the computer',
	'098-users': 'the computer', '099-linux': 'the computer', '100-secrets': 'OpenSSL', '101-desktop': 'the desktop',
	'102-media': 'the desktop', '103-printing': 'printers', '104-speech': 'speech', '105-bluetooth': 'Bluetooth',
	'106-dbus': 'D-Bus', '107-screens': 'the desktop',
};

// what compiled WebAssembly has that the interpreter in a browser hasn't:
// C's functions themselves (printf), and C calling back (qsort)
const COMPILED_HAS = { '027-c-functions': 1, '028-addresses': 1 };

function walk(dir, out, base) {
	for (const e of fs.readdirSync(dir, { withFileTypes: true })) {
		const p = path.join(dir, e.name);
		if (e.isDirectory()) walk(p, out, base);
		else out.push(path.relative(base, p));
	}
	return out;
}

// a program's expectations, from its // lines
function expect(src) {
	const wantPanic = (src.match(/^\/\/ panic: (.*)$/m) || [])[1];
	return {
		want: src.split('\n').filter(l => l.startsWith('//>')).map(l => l.replace(/^\/\/> ?/, '')).join('\n'),
		wantPanic, wantExit: wantPanic ? 101 : parseInt((src.match(/^\/\/ exit: (\d+)$/m) || [0, 0])[1]),
		args: ((src.match(/^\/\/ args: (.*)$/m) || [])[1] || '').split(' ').filter(Boolean),
		script: (src.match(/^\/\/ script: (.*)$/m) || [])[1],   // (a game's, as run_tutorial_tests.sh gives it)
		// (as printf's %b reads it: \\n, \\r, \\t and \\\\)
		stdin: ((src.match(/^\/\/ (?:input|stdin): (.*)$/m) || [])[1] || '')
			.replace(/\\(n|r|t|\\)/g, (m, c) => ({ n: '\n', r: '\r', t: '\t', '\\': '\\' })[c]),
	};
}

// cases that need what a browser hasn't, though their libraries are there
const CASE_SKIP = {
	directory_held: "a held directory's path after it's moved (WASI can't say where an open folder is)",
	dates_zones_languages_loosely: 'the time zones database (/usr/share/zoneinfo)',
	callbacks_given_to_c: 'threads (pthread_create)',
};

// what a program uses that a browser hasn't
const NEEDS = /^use (shell|gui|gtk|game|world|tui|tgame|videos|sound|threads|processes|networks|dbus|bluetooth|desktop|os|linux|users|system|printing|speech|secrets|emails|databases|archives|power|volume|media|brightness|keyboard|mouse|screens)$/m;
// (and once gui and game are built for WebAssembly)
const NEEDS_THEN = /^use (shell|gtk|world|tui|tgame|videos|sound|threads|processes|networks|dbus|bluetooth|desktop|os|linux|users|system|printing|speech|secrets|emails|databases|archives|power|volume|media|brightness|keyboard|mouse|screens)$/m;

// ---- --play: the page itself, with a window and a game in it
async function play() {
	const SITE = path.join(ROOT, 'build', 'wasm-site');
	if (!fs.existsSync(path.join(SITE, 'index.html'))) {
		console.log('play: no build/wasm-site (make wasm-site first)');
		process.exit(1);
	}
	const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'cabby-play-'));
	execFileSync(path.join(ROOT, 'cabby'), ['-c', path.join(TUT, '051-first-window.cby'), '--wasm', '-o', path.join(tmp, 'greeter.wasm')]);
	const browser = await chromium.launch();
	const page = await browser.newPage({ viewport: { width: 1000, height: 1000 } });
	page.on('pageerror', e => console.log('  page error: ' + e.message));
	await page.route('http://cabby.test/**', route => {
		const u = decodeURIComponent(new URL(route.request().url()).pathname);
		const p = u.startsWith('/tmp/') ? path.join(tmp, u.slice(5)) : path.join(SITE, u === '/' ? 'index.html' : u);
		if (!fs.existsSync(p)) return route.fulfill({ status: 404 });
		const type = p.endsWith('.wasm') ? 'application/wasm' : p.endsWith('.js') ? 'text/javascript'
			: p.endsWith('.html') ? 'text/html' : 'application/octet-stream';
		route.fulfill({ status: 200, contentType: type, body: fs.readFileSync(p) });
	});
	await page.goto('http://cabby.test/');
	await page.waitForFunction(() => document.getElementById('status').textContent === 'Ready');
	let pass = 0, fail = 0;
	const check = (what, ok, got) => {
		if (ok) pass++;
		else { fail++; console.log(`FAIL ${what}\n  got ${JSON.stringify(got)}`); }
	};
	const shown = () => page.waitForFunction(() => !document.getElementById('stage').hidden, null, { timeout: 30000 });
	const ended = () => page.waitForFunction(() => !document.getElementById('run').disabled, null, { timeout: 30000 });
	const output = () => page.textContent('#output');
	const at = async (fx, fy) => {   // (a place in the window, as fractions of its size)
		const b = await page.locator('#canvas-here canvas').boundingBox();
		return [b.x + b.width * fx, b.y + b.height * fy];
	};

	// a window, interpreted: typed into, Enter, then closed (and main carries on)
	await page.selectOption('#example', 'A window');
	await page.click('#run');
	await shown();
	await page.waitForTimeout(300);
	await page.mouse.click(...await at(0.5, 60 / 180));
	await page.keyboard.type('Ada');
	await page.keyboard.press('Enter');
	await page.waitForFunction(() => document.getElementById('output').textContent.includes('greeted'), null, { timeout: 10000 }).catch(() => {});
	// (and as a phone types: through the hidden box)
	await page.mouse.click(...await at(0.5, 60 / 180));
	await page.keyboard.press('End');
	await page.focus('#typing');
	await page.keyboard.type('!');
	await page.keyboard.press('Enter');
	await page.waitForTimeout(500);
	await page.click('#close-window');
	await ended();
	check('a window, typed into and closed', (await output()) === 'greeted Ada\ngreeted Ada!\ngoodbye\n', await output());

	// a game, interpreted: a key held, then closed
	await page.selectOption('#example', 'A game');
	await page.click('#run');
	await shown();
	await page.focus('#canvas-here canvas');
	await page.keyboard.down('ArrowLeft');
	await page.waitForTimeout(600);
	await page.keyboard.up('ArrowLeft');
	await page.click('#close-window');
	await ended();
	check('a game, played and closed', /^You caught \d+ and missed \d+\n$/.test(await output()), await output());

	// a window compiled with -c --wasm, through the worker (as a page with its own programs has it)
	const r = await page.evaluate(() => new Promise(res => {
		const w = new Worker('cabby-worker.js', { type: 'module' });
		const canvas = document.createElement('canvas').transferControlToOffscreen();
		let out = '';
		w.onmessage = e => {
			if (e.data.out) out += e.data.out;
			if (e.data.err) out += '[err] ' + e.data.err;
			if (e.data.window) {
				const send = input => w.postMessage({ input });
				setTimeout(() => {
					send({ kind: 'click', x: 180, y: 60 });
					send({ kind: 'release', x: 180, y: 60 });
					for (const ch of 'Grace') send({ kind: 'text', key: ch });
					send({ kind: 'down', key: 'Return' });
					send({ kind: 'up', key: 'Return' });
					setTimeout(() => send({ kind: 'close' }), 500);
				}, 300);
			}
			if (e.data.done !== undefined) { w.terminate(); res(out); }
		};
		w.postMessage({ program: '/tmp/greeter.wasm', canvas }, [canvas]);
	}));
	check('a compiled window, typed into and closed', r === 'greeted Grace\ngoodbye\n', r);

	console.log(`played (in a browser): ${pass} passed, ${fail} failed`);
	await browser.close();
	process.exit(fail ? 1 : 0);
}

(async () => {
	if (process.argv.includes('--play'))
		return play();
	const cases = process.argv.includes('--cases');
	const compiled = process.argv.includes('--compiled');
	const gameBuilt = fs.existsSync(path.join(ROOT, 'lib', 'wasm', 'libcabbygame.a')) && fs.existsSync(path.join(ROOT, 'lib', 'wasm', 'libcabbygui.a'));
	const tmp = compiled ? fs.mkdtempSync(path.join(os.tmpdir(), 'cabby-wasm-')) : null;
	const only = process.argv.slice(2).filter(a => !a.startsWith('--'));
	const jobs = [];
	let skip = 0;
	if (cases) {
		// tests/run_cases.cby, as run_program_tests.sh splits it
		const all = fs.readFileSync(path.join(ROOT, 'tests', 'run_cases.cby'), 'utf8').split(/^### /m).slice(1);
		for (const c of all) {
			const name = c.slice(0, c.indexOf('\n')).trim(), src = c.slice(c.indexOf('\n') + 1);
			if (only.length && !only.includes(name)) continue;
			if (CASE_SKIP[name] || /^\/\/ only: compile$/m.test(src) || (compiled && /^\/\/ only: run$/m.test(src)) || (gameBuilt ? NEEDS_THEN : NEEDS).test(src) || (!compiled && /^extern |callback /m.test(src))) { skip++; continue; }
			jobs.push({ name, file: '/cases/' + name + '.cby', src, cwd: '/', ...expect(src) });
		}
	} else {
		const names = fs.readdirSync(TUT).filter(f => /^\d+-.*\.cby$/.test(f)).map(f => f.slice(0, -4))
			.filter(n => !only.length || only.includes(n)).sort((a, b) => parseInt(a) - parseInt(b));
		for (const name of names) {
			const src = fs.readFileSync(path.join(TUT, name + '.cby'), 'utf8');
			// (a game or a gui window can run, when their libraries are built for WebAssembly)
			const browserHasnt = gameBuilt ? /^use (gtk|world|tui|tgame|videos|sound)$/m
				: /^use (gui|gtk|game|world|tui|tgame|videos|sound)$/m;
			if ((WASM_SKIP[name] && !(compiled && COMPILED_HAS[name])) || browserHasnt.test(src)) { skip++; continue; }
			jobs.push({ name, file: '/tutorials/' + name + '.cby', src, cwd: '/tutorials', ...expect(src) });
		}
	}
	const browser = await chromium.launch();
	const page = await browser.newPage();
	await page.route('http://cabby.test/**', route => {
		const p = path.join(ROOT, decodeURIComponent(new URL(route.request().url()).pathname));
		if (!fs.existsSync(p)) return route.fulfill({ status: 404 });
		const type = p.endsWith('.wasm') ? 'application/wasm' : p.endsWith('.js') ? 'text/javascript' : 'text/plain';
		route.fulfill({ status: 200, contentType: type, body: fs.readFileSync(p) });
	});
	await page.goto('http://cabby.test/wasm/cabby.js');
	// the libraries' headers, and every file the tutorials keep beside them
	const files = {};
	for (const f of fs.readdirSync(path.join(ROOT, 'lib')).filter(f => f.endsWith('.hby')))
		files['/lib/' + f] = fs.readFileSync(path.join(ROOT, 'lib', f)).toString('base64');
	if (cases)   // (as run_program_tests.sh runs them: from the cabby folder, where one reads this)
		files['/tests/run_cases.cby'] = fs.readFileSync(path.join(ROOT, 'tests', 'run_cases.cby')).toString('base64');
	else
		for (const f of walk(TUT, [], TUT))
			if ((f.includes('/') || !f.endsWith('.cby')) && !f.endsWith('.md') && fs.statSync(path.join(TUT, f)).size < 4e6)
				files['/tutorials/' + f] = fs.readFileSync(path.join(TUT, f)).toString('base64');
	// the fonts a game writes with, where it looks for them (a page gives them the same way)
	const FONTS = '/usr/share/fonts/truetype/dejavu';
	for (const f of ['DejaVuSans.ttf', 'DejaVuSans-Bold.ttf', 'DejaVuSans-Oblique.ttf', 'DejaVuSansMono.ttf'])
		if (fs.existsSync(path.join(FONTS, f)))
			files[FONTS + '/' + f] = fs.readFileSync(path.join(FONTS, f)).toString('base64');
	await page.evaluate(async files => {
		window.cabbyFiles = {};
		for (const [k, v] of Object.entries(files)) window.cabbyFiles[k] = Uint8Array.from(atob(v), c => c.charCodeAt(0));
		window.cabbyWasm = await WebAssembly.compileStreaming(fetch('http://cabby.test/cabby.wasm'));
	}, files);
	let pass = 0, fail = 0;
	for (const j of jobs) {
		if (compiled) {
			// (compiled here, from a copy beside the files it uses)
			const here = cases ? null : path.join(TUT, path.basename(j.file));
			const src = here || path.join(tmp, path.basename(j.file));
			const out = path.join(tmp, path.basename(j.file).replace(/\.cby$/, '.wasm'));
			if (!here) fs.writeFileSync(src, j.src);
			try {
				execFileSync(path.join(ROOT, 'cabby'), ['-c', src, '--wasm', '-o', out], { stdio: 'pipe', cwd: ROOT });
			} catch (e) {
				fail++;
				console.log(`FAIL ${j.name} (didn't compile)\n  ` + String(e.stderr).split('\n').slice(0, 4).join('\n  '));
				continue;
			}
			j.wasm = fs.readFileSync(out).toString('base64');
		}
		const r = await page.evaluate(async j => {
			const { runCabby } = await import('http://cabby.test/wasm/cabby.js');
			let out = '', err = '';
			const wasm = j.wasm ? await WebAssembly.compile(Uint8Array.from(atob(j.wasm), c => c.charCodeAt(0)))
				: window.cabbyWasm;
			const res = await runCabby({ wasm, files: { ...window.cabbyFiles, [j.file]: j.src },
				name: j.wasm ? j.name : 'cabby', args: j.wasm ? j.args : [j.file, ...j.args], env: { CABBY_CWD: j.cwd, ...(j.script ? { CABBY_GUI_SCRIPT: j.script } : {}) }, stdin: j.stdin,
				stdout: s => { out += s; }, stderr: s => { err += s; } });
			return { out, err, exit: res.exit };
		}, j);
		const got = r.out.replace(/\n$/, '');
		const ok = got === j.want && r.exit === j.wantExit && (!j.wantPanic || r.err.includes(j.wantPanic));
		if (ok) pass++;
		else {
			fail++;
			console.log(`FAIL ${j.name} (exit ${r.exit}, want ${j.wantExit})`);
			const g = got.split('\n'), w = j.want.split('\n');
			for (let i = 0; i < Math.max(g.length, w.length); i++)
				if (g[i] !== w[i]) { console.log(`  line ${i + 1}: got  ${JSON.stringify(g[i])}\n          want ${JSON.stringify(w[i])}`); break; }
			if (r.err) console.log('  err| ' + r.err.split('\n').slice(0, 4).join('\n  err| '));
		}
	}
	console.log(`${cases ? 'program tests' : 'tutorials'} (${compiled ? 'compiled, ' : ''}in a browser): ${pass} passed, ${fail} failed, ` +
		`${skip} that need the computer itself`);
	await browser.close();
	process.exit(fail ? 1 : 0);
})();
