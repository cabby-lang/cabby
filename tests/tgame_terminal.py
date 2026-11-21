#!/usr/bin/env python3
"""Checks tgame in a real (pretend) terminal: a game tutorial is run as
tgame in a pseudo-terminal, keys are pressed, and what it writes is played
on a small terminal of our own, so the test can look at the screen.

    tests/tgame_terminal.py ./cabby [picture.png]

It checks that the game fills the screen with its picture (half blocks),
writes its text as characters, takes keys (Ctrl+Q ends it), and gives the
terminal back as it was. With a picture's name, it also saves what the
screen showed (to look at; Pillow draws it)."""
import codecs, os, pty, re, select, subprocess, sys, tempfile, time, fcntl, termios, struct

COLS, ROWS = 100, 30

class Screen:
    """Just enough of a terminal: moving the cursor, colors, characters."""
    def __init__(self, cols, rows):
        self.cols, self.rows = cols, rows
        self.clear()
        self.r = self.c = 0
        self.fg, self.bg = None, None
        self.modes = set()
        self.pending = ""

    def clear(self):
        self.cells = [[(" ", None, None) for _ in range(self.cols)] for _ in range(self.rows)]

    def color(self, params, i):
        if params[i + 1] == 2:
            return (params[i + 2], params[i + 3], params[i + 4]), i + 5
        n = params[i + 2]          # 256 colors: the cube, or the grays
        if n >= 232:
            g = 8 + (n - 232) * 10
            return (g, g, g), i + 3
        n -= 16
        return (n // 36 * 51, n // 6 % 6 * 51, n % 6 * 51), i + 3

    def feed(self, text):
        text = self.pending + text
        self.pending = ""
        cut = text.rfind("\x1b")
        if cut >= 0 and not re.match(r"\x1b\[[?<>=]?[0-9;:]*[A-Za-z~]", text[cut:]):
            text, self.pending = text[:cut], text[cut:]     # (the rest is still coming)
        i = 0
        while i < len(text):
            ch = text[i]
            if ch == "\x1b":
                m = re.match(r"\x1b\[([?<>=]?)([0-9;:]*)([A-Za-z~])", text[i:])
                if not m:
                    i += 1
                    continue
                i += m.end()
                private, raw, final = m.groups()
                params = [int(p) if p else 0 for p in re.split("[;:]", raw)] if raw else []
                if private == "?" and final in "hl":
                    for p in params:
                        (self.modes.add if final == "h" else self.modes.discard)(p)
                elif private:
                    pass
                elif final == "H":
                    self.r = (params[0] if params else 1) - 1
                    self.c = (params[1] if len(params) > 1 else 1) - 1
                elif final == "J":
                    self.clear()
                elif final == "m":
                    if not params:
                        params = [0]
                    k = 0
                    while k < len(params):
                        p = params[k]
                        if p == 0:
                            self.fg = self.bg = None
                            k += 1
                        elif p in (38, 48):
                            c, k = self.color(params, k)
                            if p == 38:
                                self.fg = c
                            else:
                                self.bg = c
                        else:
                            k += 1
                continue
            if ch in "\r\n":
                i += 1
                continue
            if 0 <= self.r < self.rows and 0 <= self.c < self.cols:
                self.cells[self.r][self.c] = (ch, self.fg, self.bg)
            self.c += 1
            i += 1

    def text(self):
        return "\n".join("".join(c[0] for c in row) for row in self.cells)

    def save(self, path):
        from PIL import Image, ImageDraw, ImageFont
        W, H = 8, 16
        im = Image.new("RGB", (self.cols * W, self.rows * H), (0, 0, 0))
        d = ImageDraw.Draw(im)
        try:
            font = ImageFont.truetype("DejaVuSansMono.ttf", 13)
        except OSError:
            font = ImageFont.load_default()
        for r, row in enumerate(self.cells):
            for c, (ch, fg, bg) in enumerate(row):
                x, y = c * W, r * H
                d.rectangle([x, y, x + W - 1, y + H - 1], fill=bg or (0, 0, 0))
                if ch == "▀":
                    d.rectangle([x, y, x + W - 1, y + H // 2 - 1], fill=fg or (255, 255, 255))
                elif ch != " ":
                    d.text((x, y), ch, fill=fg or (255, 255, 255), font=font)
        im.save(path)

def main():
    cabby = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else "./cabby")
    picture = sys.argv[2] if len(sys.argv) > 2 else None
    here = os.path.dirname(os.path.abspath(__file__))
    tutorials = os.path.join(here, "..", "tutorials")
    which = os.environ.get("TGAME_TUTORIAL", "068-games.cby")
    with open(os.path.join(tutorials, which)) as f:
        src = f.read()
    src = re.sub(r"^use game$", "use tgame", src, flags=re.M)
    src = re.sub(r"(^|[^a-z_])game\.", r"\1tgame.", src)
    tmp = tempfile.mkdtemp()
    prog = os.path.join(tmp, "game.cby")
    with open(prog, "w") as f:
        f.write(src)

    pid, fd = pty.fork()
    if pid == 0:
        fcntl.ioctl(0, termios.TIOCSWINSZ, struct.pack("HHHH", ROWS, COLS, 0, 0))
        os.chdir(tutorials)
        env = dict(os.environ, TERM="xterm-256color", COLORTERM="truecolor")
        env.pop("CABBY_GUI_SCRIPT", None)
        os.execve(cabby, [cabby, prog], env)
    screen = Screen(COLS, ROWS)
    got = []
    decode = codecs.getincrementaldecoder("utf-8")("replace")

    def pump(seconds):
        end = time.time() + seconds
        while time.time() < end:
            r, _, _ = select.select([fd], [], [], 0.05)
            if r:
                try:
                    data = os.read(fd, 65536)
                except OSError:
                    return False
                if not data:
                    return False
                s = decode.decode(data)
                got.append(s)
                # (it asks about kitty's keys; we answer as a plain terminal)
                if "\x1b[c" in s:
                    os.write(fd, b"\x1b[?62;22c")
                screen.feed(s)
        return True

    def pump_until(ready, seconds):
        """until ready() (or that long: a busy computer may be slow to start)"""
        end = time.time() + seconds
        while time.time() < end and not ready():
            if not pump(0.1):
                return

    fails = []
    pump_until(lambda: "▀" in screen.text() and "Caught:" in screen.text(), 10)
    for _ in range(20):                      # the basket moves right
        os.write(fd, b"\x1b[C")
        pump(0.03)
    # an apple missed: the game prints so, which is kept until it ends
    pump_until(lambda: "missed one" in screen.text() or "Missed: 0" not in screen.text(), 10)
    pump(0.3)
    if picture:
        screen.save(picture)
    shown = screen.text()
    os.write(fd, b"\x11")                    # Ctrl+Q
    pump_until(lambda: "\x1b[?1049l" in "".join(got), 5)
    pump(0.3)
    _, status = os.waitpid(pid, 0)
    everything = "".join(got)
    if "missed one" in shown:
        fails.append("what the game printed went over the picture")
    if "▀" not in shown:
        fails.append("no picture (half blocks) on the screen")
    if "Caught:" not in shown:
        fails.append("the score isn't written as text:\n" + shown)
    if not (os.WIFEXITED(status) and os.WEXITSTATUS(status) == 0):
        fails.append("Ctrl+Q didn't end the game cleanly (status %r)" % status)
    if "\x1b[?1049l" not in everything or "\x1b[?25h" not in everything:
        fails.append("the terminal wasn't given back (other screen, cursor)")
    elif "missed one" not in everything.split("\x1b[?1049l")[-1]:
        fails.append("what the game printed wasn't shown after it ended")
    for f in fails:
        print("FAIL tgame terminal:", f)
    print("tgame terminal: %s" % ("failed" if fails else "passed"))
    sys.exit(1 if fails else 0)

main()
