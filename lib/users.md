# users — who's running a program, the computer's other users, and making them

```cabby
use users
```

The same on Linux and on a Mac.

<!-- check -->
```cabby
use users

int main {
	str Name = users.full_name
	if Name is "" {
		Name = users.name
	}
	println "Hello,", Name
	println "home:", users.home, "shell:", users.shell
	if users.is_root {
		println "(running as root)"
	}
	println "groups:", users.groups
	return 0
}
```

Whoever is running the program:

| Call | What it says |
|---|---|
| `users.name` | the login name: `"ada"` |
| `users.full_name` | the full name: `"Ada Lovelace"` (`""` if none was given) |
| `users.home` | the home folder: `"/home/ada"`, `"/Users/ada"` |
| `users.shell` | the shell: `"/bin/bash"`, `"/bin/zsh"` |
| `users.id` | the user's number (uid); 0 is root |
| `users.is_root` | `true` for root, the administrator (as under `sudo`) |
| `users.groups` | the groups they're in, by name: a `list str` |
| `users.in_group Group` | `true` if they're in `Group` |

Anyone, by their login name:

| Call | What it says |
|---|---|
| `users.exists Name` | `true` if the computer has a user called `Name` |
| `users.full_name_of Name` | their full name (`""` with no such user) |
| `users.home_of Name` | their home folder (`""` with no such user) |
| `users.id_of Name` | their number (-1 with no such user) |
| `users.groups_of Name` | their groups (none with no such user) |

`users.home` is the home folder the system has for the user, which is
almost always `$HOME`; `directories.home` is `$HOME` itself.

Groups worth knowing: on Linux, `wheel` (Arch) or `sudo` (Debian) can use
`sudo`, and `audio`, `video` and `input` reach those devices directly; on
a Mac, `admin` can. A user's own group, of the same name, is in the list
too.

## Making users and groups

<!-- check -->
```cabby
use users

int main {
	users.make "ada", "Ada Lovelace", "analytical engine"
	users.make_group "band"
	users.add_to_group "ada", "band"
	println users.groups_of("ada")       // [ada, band]
}
```

Making users and groups, and changing them, needs the administrator:
run the program with `sudo` (`sudo cabby accounts.cby`, or `sudo
./accounts` compiled). Without it, each of these is a panic that says so.

| Call | What it does |
|---|---|
| `users.make Name, FullName, Password` | A new user, who can log in, with a home folder of their own. The name is small letters, digits, `_` and `-` (`"ada"`, `"j-smith"`). |
| `users.make_admin Name, FullName, Password` | The same, as an administrator: someone who can use `sudo` (in the `sudo` group on Debian, `wheel` on Arch; an admin on a Mac). |
| `users.set_password Name, Password` | A new password for them. |
| `users.remove Name` | Removes the user, and their home folder with them. |
| `users.remove_keeping_home Name` | Removes the user, keeping their home folder (to look through, or save). |
| `users.make_group Group` | A new group, with no one in it. |
| `users.remove_group Group` | Removes a group (its members stay, outside it). |
| `users.add_to_group Name, Group` | Puts someone in a group. They see it the next time they log in. |
| `users.remove_from_group Name, Group` | Takes someone out of a group. |
| `users.all` | Everyone who can log in, by login name: not the system's own users (on Linux, from number 1000; on a Mac, from 501). |
| `users.all_groups` | Every group, by name. |

A name that's taken already, or isn't a name a user can have, is a panic,
as is anything the system won't do: its own words are in the panic.
Cabby won't remove the administrator (root), or whoever's running the
program. A Mac's directory service keeps what it knows for a
while, so on a Mac Cabby clears it, and waits (a moment, up to ten
seconds) until a user or group just made or removed shows as such: the
next line of the program sees the change.

Each system does it its own way, and Cabby uses its own programs: on
Linux, `useradd`, `chpasswd`, `userdel`, `groupadd`, `groupdel` and
`gpasswd` (the same on Debian and Arch); on a Mac, `sysadminctl` and
`dseditgroup`, as System Settings does. On Linux the password is given
to `chpasswd` to read, so it's never where other programs can see it; a
Mac's `sysadminctl` takes it on its command line, where the computer's
other users could see it while it runs (a moment), so on a Mac shared
with people you don't trust, make users in System Settings instead.

Cabby's tests try these with stand-ins for the programs, which say what
they were asked (`tests/desktop/tools/admin-tool`), so they make no one
on the computer that runs them.
