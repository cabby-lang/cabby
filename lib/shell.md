# shell — running commands, and environment variables

```cabby
use shell
```

Run other programs as you would in a terminal, and read or change the
program's environment variables. The current directory, which commands run
in, is in [directories](directories.md) (`directories.current`,
`directories.change_to`).

| Call | What it does |
|---|---|
| `shell.run Command` | Runs the command and gives what it printed, as a `str`, without the line ends at the very end. A panic if the command fails. |
| `shell.status Command` | Runs the command and gives its exit status, as an `int`: 0 if it worked. What it prints goes straight to the screen. Never a panic, whatever the command does. |
| `shell.get Name` | The environment variable's value, as a `str`. A panic if it isn't set. |
| `shell.has Name` | `true` if the environment variable is set (even to `""`). |
| `shell.set Name, Value` | Sets the environment variable, for this program and the commands it runs afterwards. |
| `shell.unset Name` | Removes the environment variable (nothing happens if it isn't set). |
| `shell.run_with Command, Input` | `shell.run`, giving the command `Input` (what it reads, its stdin). |
| `shell.run_program Arguments` | Runs a program with its arguments as a `list str`, with no shell in between: `{"ls", "-l", Folder}`. What it printed, as `shell.run` gives it; a panic if it fails. |
| `shell.run_program_with Arguments, Input` | The same, giving it `Input`. |
| `shell.program_status Arguments` | The program's exit status, as `shell.status` gives it (127 if there's no such program). What it prints goes straight to the screen. |

Every command, name and value is a `str`.

## How a command runs

A command is given to the system's shell, `/bin/sh -c`, so it is written
as you'd type it in a terminal: pipes (`|`), redirections (`>`, `2>`),
`&&`, quotes and `$NAME` all work. The command runs in the program's
current directory and sees its environment variables, and the program
waits for it to finish.

<!-- run -->
```cabby
use shell

int main {
	str Count = shell.run "printf 'b\na\nc\n' | sort | head -n 2"
	println Count.split "\n"
	shell.set "GREETING", "hello"
	println shell.run "echo $GREETING, world"
}
```

```text
[a, b]
hello, world
```

What the program printed before a command runs always comes before the
command's own output, and files the program wrote into are complete when
the command reads them.

`shell.run` collects what the command prints to stdout. What it prints to
stderr, such as error messages, still goes to the screen; add `2>&1` to
the command to collect that too, or `2>/dev/null` to hide it.

## When a command fails

An exit status other than 0 means a command failed. `shell.run` then
panics, saying how it ended:

```text
ls: cannot access 'photos': No such file or directory
panic at prog.cby:4:12: the command 'ls photos' failed: exit status 2 (shell.status gives the status instead)
```

Exit status 127 means the shell couldn't find the command, and the panic
says so. A command stopped by a signal (killed, or Ctrl-C) gives 128 plus
the signal's number from `shell.status`, as a terminal's shell does.

When a command is allowed to fail, or failing is the answer you want,
use `shell.status`:

<!-- run -->
```cabby
use shell

int main {
	if shell.status("command -v sh > /dev/null") == 0 {
		println "sh is installed"
	}
	int Status = shell.status "exit 3"
	println "status:", Status
}
```

```text
sh is installed
status: 3
```

`grep -q`, `test` and `cmp -s` are made for this: they print nothing and
answer with their status.

## Input, and arguments that need no quoting

`shell.run_with` gives a command something to read, as `|` would:

<!-- run -->
```cabby
use shell

int main {
	println shell.run_with("sort", "pear\napple\nfig\n").split "\n"
	println shell.run_with("wc -w", "one two three").trim
}
```

```text
[apple, fig, pear]
3
```

A command is shell language, so a file's name put into one needs quoting
(a space, a `'` or a `$` in it would change what the command means).
`shell.run_program` needs none: the program is the list's first, and
each argument after it is given to it as it is, with no shell in between:

<!-- run -->
```cabby
use shell

int main {
	str Odd = "it's a \"file\" $NAME"
	println shell.run_program {"printf", "[%s]", Odd}
	println shell.program_status {"grep", "-q", "x", "/dev/null"}
}
```

```text
[it's a "file" $NAME]
1
```

The program is looked for as the shell would, in `$PATH` (unless it's a
path itself: `"./build.sh"`). There being no such program is a panic for
`run_program`, and status 127 for `program_status`.

## Environment variables

`shell.get` panics when the variable isn't set:

```text
panic at prog.cby:4:15: the environment variable 'EDITOR' isn't set (check first with shell.has "EDITOR")
```

so check first when it might not be:

<!-- run -->
```cabby
use shell

int main {
	str Editor = "nano"
	if shell.has "CABBY_EXAMPLE_EDITOR" {
		Editor = shell.get "CABBY_EXAMPLE_EDITOR"
	}
	println "editing with", Editor
}
```

```text
editing with nano
```

A name can't be empty or have `=` in it; either is a panic.

## Compared with C's system

`system` (in [standard](standard.md)) gives C's wait status, which is the
exit status times 256 on Linux, and can't collect the output.
`shell.status` gives the exit status itself, and `shell.run` the output.

## See also

The manual's chapter 32, "Files, directories and commands", and chapter 31
for exit statuses.
