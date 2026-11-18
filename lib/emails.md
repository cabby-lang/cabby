# emails — sending and reading email

```cabby
use emails
```

A message (who it's from and to, a subject, what it says, files attached
to it), sent through an email account's server, as a mail program sends
it: SMTP. And a mailbox read, as a mail program reads it: IMAP. The same on Linux and macOS. It's sent with libcurl, which
comes with the `curl` package that every Linux system and every Mac has.

* [Sending one](#sending-one)
* [emails.message](#emailsmessage)
* [emails.account](#emailsaccount)
* [Which server](#which-server)
* [When it doesn't go](#when-it-doesnt-go)
* [How it's sent](#how-its-sent)
* [Reading a mailbox](#reading-a-mailbox)
* [emails.received](#emailsreceived)
* [Not yet](#not-yet)

## Sending one

<!-- check -->
```cabby
use emails
use shell

int main {
	emails.message M
	M.set_from "Thomas Whalan <tom@example.com>"
	M.add_to "ana@example.com"
	M.set_subject "Lesson on Friday"
	M.set_text "Hi Ana,\n\nThe lesson is at four this week. The scales are attached.\n\nThomas"
	M.attach "scales.pdf"

	str Password = shell.get "MAIL_PASSWORD"   // (not written in the program)
	emails.account Mail = emails.account_at "smtp.example.com", "tom@example.com", Password
	if Mail.send M {
		println "sent"
	} else {
		println "it didn't go:", Mail.error
	}
}
```

An address is written as it is, `"ana@example.com"`, or with a name:
`"Ana Rossi <ana@example.com>"`. Names, subjects and text can be in any
language.

## emails.message

| | |
|---|---|
| `set_from Address` | who it's from: the account's address, usually |
| `add_to Address` | who it's to (any number) |
| `add_cc Address` | copied in, and shown to everyone as copied |
| `add_bcc Address` | sent a copy, without the others knowing |
| `set_reply_to Address` | where answers go, if not to whom it's from |
| `set_subject Subject` | |
| `set_text Text` | what it says (`"\n"` between lines) |
| `set_html Html` | what it says as HTML too (bold, links): mail programs show this, and the text where they can't |
| `attach Path` | a file sent with it (a PDF, a picture, a recording) |
| `str text` | the whole message as it's sent: its headers, then what it says |
| `save Path` | saved as a file: `.eml`, which a mail program opens |

## emails.account

`emails.account_at Server, Login, Password` is an account's server, for
sending, with its login (an address, usually) and password.

| | |
|---|---|
| `bool send Message` | sends it: `true` if the server took it |
| `str error` | why the last send didn't work (`""` if it did) |
| `set_port Port` | 465 to begin with (encrypted from the start); 587 for those that use STARTTLS |
| `set_unencrypted` | for a server on this computer alone (or a test's): anything sent so can be read on the way |

A message is always sent encrypted (TLS) unless `set_unencrypted` says
not to, and the server's certificate is checked, as a browser checks a
site's.

## Which server

Each provider says which server sends its mail, in its help pages:

| Provider | Server | Port | Password |
|---|---|---|---|
| Gmail | `smtp.gmail.com` | 465 | an app password (Google Account > Security > App passwords; it needs 2-Step Verification on) |
| iCloud Mail | `smtp.mail.me.com` | 587 | an app-specific password (appleid.apple.com) |
| Fastmail | `smtp.fastmail.com` | 465 | an app password (Settings > Privacy & Security) |
| Proton Mail | `127.0.0.1` | 1025 | Proton Mail Bridge's (it runs on the computer) |

Many providers no longer take the account's own password from a program:
an **app password** is a separate one, made for it in the account's
settings. Outlook.com only takes sign-ins through Microsoft's own login
page (OAuth), which this library doesn't do yet.

Keep a password out of the program itself: read it from an environment
variable (`shell.get "MAIL_PASSWORD"`) or a file only you can read.

## When it doesn't go

`send` gives `false`, and `error` says why:

| `error` | usually means |
|---|---|
| `Login denied` | the login or password is wrong, or the provider wants an app password |
| `Couldn't connect to server` | the server's name or port is wrong, or the network is down |
| `Couldn't resolve host name` | no server by that name (or no network) |
| `SSL connect error` | the port's wrong for TLS: try 587 (or 465) |
| `Failed sending data to the peer` | the server turned it down: too big, or a recipient it won't take |

## How it's sent

The message is made by Cabby (`runtime/emails.c`) as RFC 5322 and MIME
say: text that's plain ASCII in short lines goes as it is, anything else
quoted-printable (still readable in the raw message), attachments in
base64, and names and subjects that aren't ASCII as RFC 2047 says. With
HTML, the text and the HTML go as alternatives; with attachments, the
message is "mixed". A Bcc goes to the server, not into the message.
Then libcurl sends it by SMTP: logging in, then who it's from and to,
then the message.

## Reading a mailbox

<!-- check -->
```cabby
use emails
use shell

int main {
	str Password = shell.get "MAIL_PASSWORD"
	emails.mailbox Mail = emails.mailbox_at "imap.gmail.com", "tom@example.com", Password
	println Mail.unread_count, "unread of", Mail.count
	for M in Mail.unread {
		println M.from_name, "-", M.subject
		println M.text
		for I in M.attachments {
			M.save_attachment I, M.attachment_name(I)
		}
		Mail.mark_read M.number
	}
	return 0
}
```

| `emails.mailbox` | |
|---|---|
| `emails.mailbox_at Server, Login, Password` | an account's mailbox (the password, for many, an app password: [below](#which-server-for-reading)) |
| `list str folders` | its folders: `["INBOX", "Sent", "Archive"]` |
| `open_folder Name`, `str folder` | the folder read from now on (`"INBOX"` to begin with) |
| `long count`, `long unread_count` | how many messages there are in it, and how many unread |
| `list emails.received unread` | those not yet read, oldest first |
| `list emails.received latest Count` | the newest so many, oldest first |
| `list emails.received all` | every one in the folder |
| `from Who`, `with_subject Words`, `containing Words` | those from someone (part of a name or address), with words in the subject, or anywhere |
| `emails.received message Number` | one, by its number |
| `bool mark_read Number`, `mark_unread`, `flag`, `unflag` | |
| `bool move Number, Folder`, `bool delete Number` | moved to another folder; deleted, for good |
| `bool save Message, Folder`, `bool save_unread Message, Folder` | an `emails.message` put into a folder without sending it: a copy in `"Sent"`, a draft in `"Drafts"` (marked read, or not) |
| `bool save_text Raw, Folder` | the same for a message as text: a `.eml` file read, or a received one's `whole` |
| `bool can_connect`, `str error` | whether it can log in, and why not (otherwise the first thing asked of it is a panic that says why) |
| `set_port Port` | 993 to begin with (encrypted from the start); 143, for STARTTLS |
| `set_unencrypted` | for a server on this computer alone (or a test's) |

Sending a message doesn't put a copy in the account's Sent folder: Gmail
does that by itself, and for others `save` it there.

Reading a message doesn't mark it read: `mark_read` does, as a mail
program does when it's opened. A message's `number` is the mailbox's own
for it (its UID), the same each time it's asked for.

### Which server, for reading

| Provider | Server |
|---|---|
| Gmail | `imap.gmail.com` (IMAP turned on in Gmail's settings; an app password) |
| iCloud Mail | `imap.mail.me.com` (an app-specific password) |
| Fastmail | `imap.fastmail.com` (an app password) |
| Proton Mail | `127.0.0.1`, port 1143, with `set_unencrypted` (Proton Mail Bridge's, on the computer) |

## emails.received

A message as it came: from a mailbox, or a file (`emails.read_message
"saved.eml"`).

| | |
|---|---|
| `str from`, `str from_address`, `str from_name` | `"Ana Rossi <ana@example.com>"`, `"ana@example.com"`, `"Ana Rossi"` |
| `str sent_to`, `str cc` | who it's to, and copied in |
| `str subject`, `str date` | |
| `str header Name` | any header: `"Reply-To"`, `"Message-ID"` (`""` if it hasn't one) |
| `str text`, `str html` | what it says, as text, and as HTML if it came with that (`""` if not) |
| `int attachments` | how many files are attached |
| `attachment_name I`, `attachment_type I`, `attachment_size I` | each's name, kind (`"application/pdf"`) and size, from 0 |
| `bool save_attachment I, Path` | saved as a file |
| `long number`, `bool read`, `bool flagged`, `long size` | in a mailbox: its number, whether it's been read, flagged (starred), and its size |
| `str whole` | the whole message as it came, to save as a `.eml` |

Its headers and text are decoded, however they came: names and subjects
in other languages (RFC 2047's encoded words), text in base64 or
quoted-printable, and character sets other than UTF-8 (Latin-1, Windows'
1252, and the rest by the system's iconv), all given as UTF-8. Its parts
are found however they're nested (mixed, alternative, related).

## Not yet

* Signing in with OAuth (Outlook.com, and Gmail without an app password)
* Pictures inside an HTML message (rather than linked from the web)
* Waiting for new mail to come (IMAP's IDLE)
