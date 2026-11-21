// tests/run_wasm_tests.cjs — the tutorials run by cabby.wasm in a browser
// (Chromium, through Playwright), each checked against the output at its
// end, as tests/run_tutorial_tests.sh checks them on a computer.
//
//     node tests/run_wasm_tests.cjs [names...]
//
// Those that need the computer itself (a window, a server, the desktop)
// are listed in WASM_SKIP below, with why: in a browser they say what's
// missing instead.
const fs = require('fs');
const path = require('path');
const { chromium } = require('playwright');

const ROOT = path.dirname(__dirname);
const TUT = path.join(ROOT, 'tutorials');

// what each needs that a browser doesn't have
const WASM_SKIP = {
	'27-c-functions': 'C functions beyond the libraries\' (printf)', '28-addresses': 'callbacks (qsort)',
	'34-processes': 'other programs', '41-downloads': 'the network',
	'42-web-addresses': 'the network', '49-archives': 'zlib', '50-databases': 'SQLite', '62-terminal-window': 'a terminal',
	'63-workers': 'threads', '74-songs': 'sound', '76-keyboard': 'MIDI', '91-chat-server': 'the network',
	'92-web-server': 'the network', '93-websockets': 'the network', '94-encrypted': 'the network',
	'95-emails': 'the network', '96-the-computer': 'the computer', '97-power-and-volume': 'the computer',
	'98-users': 'the computer', '99-linux': 'the computer', '100-secrets': 'OpenSSL', '101-desktop': 'the desktop',
	'102-media': 'the desktop', '103-printing': 'printers', '104-speech': 'speech', '105-bluetooth': 'Bluetooth',
	'106-dbus': 'D-Bus', '107-screens': 'the desktop',
};

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
		// (as printf's %b reads it: \\n, \\r, \\t and \\\\)
		stdin: ((src.match(/^\/\/ (?:input|stdin): (.*)$/m) || [])[1] || '')
			.replace(/\\(n|r|t|\\)/g, (m, c) => ({ n: '\n', r: '\r', t: '\t', '\\': '\\' })[c]),
	};
}

// cases that need what a browser hasn't, though their libraries are there
const CASE_SKIP = {
	directory_held: "a held directory's path after it's moved (WASI can't say where an open folder is)",
	dates_zones_languages_loosely: 'the time zones database (/usr/share/zoneinfo)',
};

// what a program uses that a browser hasn't
const NEEDS = /^use (shell|gui|gtk|game|world|tui|tgame|videos|sound|threads|processes|networks|dbus|bluetooth|desktop|os|linux|users|system|printing|speech|secrets|emails|databases|archives|power|volume|media|brightness|keyboard|mouse|screens)$/m;

(async () => {
	const cases = process.argv.includes('--cases');
	const only = process.argv.slice(2).filter(a => !a.startsWith('--'));
	const jobs = [];
	let skip = 0;
	if (cases) {
		// tests/run_cases.cby, as run_program_tests.sh splits it
		const all = fs.readFileSync(path.join(ROOT, 'tests', 'run_cases.cby'), 'utf8').split(/^### /m).slice(1);
		for (const c of all) {
			const name = c.slice(0, c.indexOf('\n')).trim(), src = c.slice(c.indexOf('\n') + 1);
			if (only.length && !only.includes(name)) continue;
			if (CASE_SKIP[name] || /^\/\/ only: compile$/m.test(src) || NEEDS.test(src) || /^extern |callback /m.test(src)) { skip++; continue; }
			jobs.push({ name, file: '/cases/' + name + '.cby', src, cwd: '/', ...expect(src) });
		}
	} else {
		const names = fs.readdirSync(TUT).filter(f => /^\d+-.*\.cby$/.test(f)).map(f => f.slice(0, -4))
			.filter(n => !only.length || only.includes(n)).sort((a, b) => parseInt(a) - parseInt(b));
		for (const name of names) {
			const src = fs.readFileSync(path.join(TUT, name + '.cby'), 'utf8');
			if (WASM_SKIP[name] || /^use (gui|gtk|game|world|tui|tgame|videos|sound)$/m.test(src)) { skip++; continue; }
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
	await page.evaluate(async files => {
		window.cabbyFiles = {};
		for (const [k, v] of Object.entries(files)) window.cabbyFiles[k] = Uint8Array.from(atob(v), c => c.charCodeAt(0));
		window.cabbyWasm = await WebAssembly.compileStreaming(fetch('http://cabby.test/cabby.wasm'));
	}, files);
	let pass = 0, fail = 0;
	for (const j of jobs) {
		const r = await page.evaluate(async j => {
			const { runCabby } = await import('http://cabby.test/wasm/cabby.js');
			let out = '', err = '';
			const res = await runCabby({ wasm: window.cabbyWasm, files: { ...window.cabbyFiles, [j.file]: j.src },
				args: [j.file, ...j.args], env: { CABBY_CWD: j.cwd }, stdin: j.stdin,
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
	console.log(`${cases ? 'program tests' : 'tutorials'} (in a browser): ${pass} passed, ${fail} failed, ` +
		`${skip} that need the computer itself`);
	await browser.close();
	process.exit(fail ? 1 : 0);
})();
