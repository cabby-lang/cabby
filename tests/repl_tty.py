#!/usr/bin/env python3
"""Session tests that need a real terminal (the line editor, prompts).

    repl_tty.py path/to/cabby

Prints one line per failed check and exits 1 if any failed. Each check types
lines into the session, as a person would, and looks at what the terminal
shows. The terminal's answer to the cursor-position question is given, as a
real terminal gives it."""
import os, pty, re, select, sys, time

CLI = sys.argv[1] if len(sys.argv) > 1 else './cabby'
pid, fd = pty.fork()
if pid == 0:
    os.environ['TERM'] = 'xterm-256color'
    os.execv(CLI, [CLI])

def pump(t):
    out = b''
    end = time.time() + t
    while time.time() < end:
        if select.select([fd], [], [], 0.02)[0]:
            try:
                d = os.read(fd, 65536)
            except OSError:
                break
            out += d
            for _ in range(d.count(b'\x1b[6n')):
                os.write(fd, b'\x1b[2;1R')
    return out

def screen(lines):
    """Types the lines; gives back what each terminal line ends up showing."""
    out = b''
    for l in lines:
        os.write(fd, l.encode() + b'\r')
        out += pump(0.25)
    text = re.sub(rb'\x1b\[[0-9;?]*[A-Za-z]', b'', out).decode(errors='replace')
    shown = []
    for seg in text.split('\n'):
        parts = [p for p in seg.split('\r') if p.strip()]
        if parts:
            shown.append(parts[-1].strip())
    return shown

failed = 0
def check(name, ok, shown):
    global failed
    if not ok:
        failed += 1
        print('FAIL tty_%s: %s' % (name, ' | '.join(shown)))

pump(1.0)
screen(['int x = 3'])
s = screen(['if x > 5 {', 'println "big"', '} else {', 'println "small"', '}'])
check('plain_else_runs', s[-2:] == ['small', '>>>'], s)
s = screen(['if x > 5 {', 'println "big"', '}'])
check('if_waits_for_else', s[-1] == '...', s)
s = screen(['else if x > 1 {', 'println "medium"', '}'])
check('else_if_waits', s[-1] == '...' and 'medium' not in s, s)
s = screen(['else {', 'println "tiny"', '}'])
check('last_else_runs', s[-2:] == ['medium', '>>>'], s)
s = screen(['if x > 5 {', 'println "big"', '}', ''])
check('blank_line_ends_if', s[-1] == '>>>', s)

def keys(k):
    """Presses keys without Enter; gives back what the screen shows, then clears the line."""
    os.write(fd, k.encode())
    out = pump(0.4)
    os.write(fd, b'\x15')   # Ctrl-U
    pump(0.2)
    text = re.sub(rb'\x1b\[[0-9;?]*[A-Za-z]', b'', out).decode(errors='replace')
    return [[p for p in seg.split('\r') if p.strip()][-1].strip() for seg in text.split('\n') if seg.strip()]

# Tab completion
screen(['use files', 'object pet {', 'str Name', 'void speak {', 'println Name', '}', '}', 'pet Rex',
        'list str Names = {"a"}'])
s = keys('files.re\t')
check('complete_lists', any(l.split() == ['read', 'rename'] for l in s), s)
s = keys('files.rea\t')
check('complete_one', s[-1].endswith('files.read'), s)
s = keys('Rex.\t')
check('complete_object', any(l.split() == ['Name', 'speak'] for l in s), s)
s = keys('Rex.Name.sta\t')
check('complete_through_field', s[-1].endswith('Rex.Name.starts_with'), s)
s = keys('Names.ta\t')
check('complete_list_member', s[-1].endswith('Names.take_last'), s)
s = keys('\tx')
check('tab_indents', s[-1] == '>>>     x' or s[-1].endswith('    x'), s)
s = keys('pet Bo = Rex\t// x')
check('tab_after_whole_name', s[-1].endswith('Rex    // x'), s)
s = keys('int n = 5\t// x')
check('tab_after_number', s[-1].endswith('5   // x') or s[-1].endswith('5    // x'), s)
screen(['str Name = "a"'])
s = keys('Name\t\t// x')
check('tab_tab_after_list', any(l.split() == ['Name', 'Names'] for l in s) and s[-1].endswith('Name    // x'), s)
s = keys('files.r\t\t')
check('tab_after_fill_lists', s[-1].endswith('files.re') and any(l.split() == ['read', 'rename'] for l in s), s)
s = keys('use di\t')
check('complete_use', s[-1].endswith('use directories'), s)
screen(['use shell'])
s = keys('shell.u\t')
check('complete_shell', s[-1].endswith('shell.unset'), s)
s = keys('use \t')
words = ' '.join(s).split()
check('complete_use_lists', 'standard' in words and 'local' in words and 'shell' in words, s)
s = keys('println \t')
check('tab_after_space_in_code', s[-1].rstrip() == '>>> println' and 'standard' not in ' '.join(s), s)
s = keys('cabby.lic\t')
check('complete_session_license', s[-1].endswith('cabby.license'), s)
s = keys('cabby.v\t')
check('complete_session_version', s[-1].endswith('cabby.version'), s)
# a library not used yet still completes, from its header's text
s = keys('secrets.make_p\t')
check('complete_unused_library', s[-1].endswith('secrets.make_private_key'), s)
s = keys('bluetooth.paired\t')
check('complete_unused_library_after_its_type', s[-1].endswith('bluetooth.paired_devices'), s)

os.kill(pid, 9)
print('tty session tests: %d failed' % failed)
sys.exit(1 if failed else 0)
