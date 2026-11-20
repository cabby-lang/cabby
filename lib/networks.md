# networks — the internet, web services, and programs talking to each other

```cabby
use networks
```

| Write | What it does |
|---|---|
| `networks.download Url` | what's at the address, as a `str` (a web page, JSON, or any bytes) |
| `networks.download_bytes Url` | what's at the address, as [bytes](bytes.md): for pictures, sounds, zip files |
| `networks.download_to Url, Path` | what's at the address, saved as a file |
| `networks.can_reach Url` | `true` if something is there and answering |
| `networks.status Url` | the HTTP status it answers with: `200` fine, `404` not found...; `0` if nothing answered |
| `networks.post Url, Body, Kind` | text sent to the address, and what it answers ([more](#sending-to-a-web-service)) |
| `networks.post_json Url, Data` | JSON sent, and the answer read as JSON |
| `networks.request_to Url` | a request to set up: any method, headers, progress ([more](#requests)) |
| `networks.connect Host, Port` | a connection to another program ([more](#programs-talking-to-each-other)) |
| `networks.server` | a server, for other programs to connect to |
| `networks.web_server` | a web server, for browsers to ask for pages ([more](#a-web-server)), and for WebSockets ([more](#websockets)) |
| `networks.open_socket Url` | a WebSocket to a server: messages both ways ([more](#websockets)) |
| `networks.connect_encrypted Host, Port` | an encrypted connection; and a server's `set_certificate` for HTTPS ([more](#encrypted-https-wss-and-tls)) |

```cabby
use networks
use json

int main {
	// a web page, as text
	str Page = networks.download "https://example.com"
	println Page.length, "characters"

	// a web service's answer: Sydney's weather now, from Open-Meteo (free,
	// no sign-up), as JSON
	str Url = "https://api.open-meteo.com/v1/forecast?latitude=-33.87&longitude=151.21&current_weather=true"
	json Weather = json.read networks.download Url
	println "Sydney:", Weather["current_weather"]["temperature"].double, "degrees"

	// saved as a file
	networks.download_to "https://example.com", "example.html"
}
```

These are real addresses, so the program runs as it is.

`https://` and `http://` addresses work, as do redirects (a page that has
moved is followed to where it went), proxies (as the system sets them),
and `file://` addresses, which read a file on this computer: handy for
trying a program out without the internet.

## Web addresses (URLs)

`networks.read_url` reads an address into its parts, and a
`networks.url`'s `text` puts it back together, with whatever needs it
encoded. Change a part in between, or make one from nothing:

<!-- run -->
```cabby
use networks

int main {
	networks.url Page = networks.read_url "https://example.com/search?q=fish+%26+chips&page=2#top"
	println Page.Host, Page.Path, Page.Query["q"], Page.Fragment
	Page.Query["page"] = "3"
	println Page.text

	networks.url Weather
	Weather.Scheme = "https"
	Weather.Host = "api.open-meteo.com"
	Weather.Path = "/v1/forecast"
	Weather.Query["latitude"] = "-33.87"
	Weather.Query["longitude"] = "151.21"
	println Weather.text
}
```

```text
example.com /search fish & chips top
https://example.com/search?q=fish%20%26%20chips&page=3#top
https://api.open-meteo.com/v1/forecast?latitude=-33.87&longitude=151.21
```

### networks.url

`https://ada@example.com:8080/search?q=cabby#top`, in its parts:

| Field | Is |
|---|---|
| `str Scheme` | `"https"` (in small letters) |
| `str User` | `"ada"`: who to log in as (`""` for none, as nearly always) |
| `str Host` | `"example.com"` (in small letters) |
| `int Port` | `8080`; 0 for the scheme's own (443 for https), which isn't written |
| `str Path` | `"/search"`: `""` for none |
| `lookup str str Query` | `{"q": "cabby"}`: the names and values after `?`, in the order they came |
| `str Fragment` | `"top"`: the part of the page to show |

Each is decoded: `%20` in an address is a space in the part, and `+` in
the query is a space too (as forms send it). `str text` is the whole
address again, encoded.

### Functions

| Function | What it does |
|---|---|
| `networks.url networks.read_url const str &Address` | The address in its parts. One without its kind first (`https://`) is a panic. |
| `str networks.encode const str &Text` | Text made safe to put in an address: every letter but A to Z, a to z, 0 to 9 and `- _ . ~` written as `%` and two hexadecimal digits, a byte at a time (`"é"` is `%C3%A9`). |
| `str networks.decode const str &Text` | The other way: `%20` back to a space. |
| `lookup str str networks.read_query const str &Text` | `"q=fish+%26+chips&page=2"` as its names and values, decoded. |
| `str networks.query_text const lookup str str &Query` | Names and values as an address's query, encoded: `"q=fish%20%26%20chips&page=2"`. |

The address part of networks is written in Cabby itself (it's all
text), in `networks.hby`.

## Sending to a web service

`networks.post` sends text to an address (an HTTP `POST`) and gives back
the answer; `post_json` sends JSON and reads the answer as JSON, and
`post_form` sends a form's fields, as a web page's form does:

<!-- check -->
```cabby
use networks
use json

int main {
	str Said = networks.post "https://httpbin.org/post", "Hello", "text/plain"

	json Item = json.read "{\"name\": \"Ada\", \"age\": 36}"
	json Made = networks.post_json "https://httpbin.org/post", Item
	println Made["json"]["name"].str_

	lookup str str Fields
	Fields["name"] = "Ada"
	Fields["town"] = "Sydney"
	str Answer = networks.post_form "https://httpbin.org/post", Fields
}
```

As with download, no answer, or an error's (a status of 400 and over), is a
panic that says why. For anything else, a request.

## Requests

`networks.request_to` makes a request to set up and send yourself: any
method (`PUT`, `PATCH`, `DELETE`...), headers, a time limit, and then a
good look at the answer. Here, a status of 404 or 500 isn't a panic: look
at `status`.

<!-- check -->
```cabby
use networks

int main {
	networks.request R = networks.request_to "https://api.example.com/items/7"
	R.set_method "DELETE"
	R.set_header "Authorization", "Bearer abc123"
	R.set_timeout 10
	int Status = R.send
	if Status == 0 {
		println "no answer:", R.error
	} else if !R.worked {
		println "it said", Status.text + ":", R.answer
	}
	return 0
}
```

| Method | What it does |
|---|---|
| `set_method Method` | `GET` (until a body's set, then `POST`), `POST`, `PUT`, `PATCH`, `DELETE`, `HEAD`... |
| `set_header Name, Value` | a header to send: `"Accept", "application/json"` |
| `set_body Text, Kind` | text to send, and its kind (`"text/plain"`, `"text/csv"`...) |
| `set_json Data` | JSON to send (`application/json`) |
| `set_form Fields` | a form's fields to send (`application/x-www-form-urlencoded`) |
| `set_bytes Data, Kind` | [bytes](bytes.md) to send: a picture, `"image/png"` |
| `set_timeout Seconds` | how long it may take in all before it gives up (0: as long as it takes) |
| `on_progress h` | calls `h` as a big transfer goes ([below](#progress-for-big-transfers)) |
| `int send` | sends it, and waits for the answer: its status (`0`: no answer) |
| `int save_to Path` | sends it, saving what comes back to a file as it comes: its status |

And the answer:

| Method | Gives |
|---|---|
| `int status` | `200` fine, `201` made, `204` done, `404` not found, `500` a problem at the site... (`0`: no answer) |
| `bool worked` | `true` for a status from 200 to 299 |
| `str answer` | what came back, as text |
| `json answer_json` | what came back, read as JSON |
| `bytes answer_bytes` | what came back, as bytes |
| `str answer_header Name` | one of the answer's headers (`"Content-Type"`), or `""` |
| `str error` | why there was no answer (`""` if there was one) |

`save_to` leaves nothing behind when there's no answer or an error's: the
file is only made once the whole of a good answer has come.

## Progress for big transfers

`on_progress` calls a method as a request goes, ten times a second at
most, and once more at the end; in it, `received`, `total` and `fraction`
say how far it's got. The handler is a method of the object the request
is part of, as a button's handler is:

<!-- check -->
```cabby
use networks

object fetcher {
	networks.request R

	void get const str &Url, const str &Path {
		R = networks.request_to Url
		R.on_progress went
		if R.save_to(Path) != 200 {
			println "\nit didn't work:", R.status, R.error
		}
	}

	void went {
		print "\r" + ((int)(R.fraction * 100.0)).text + "%  "
	}
}

int main {
	fetcher F
	F.get "https://example.com/big.zip", "big.zip"
	return 0
}
```

| Method | Says |
|---|---|
| `long received` | bytes that have come so far |
| `long total` | how many there'll be in all (`0` while the site hasn't said) |
| `double fraction` | how far it's got, from `0.0` to `1.0` (`0.0` while the total isn't known) |
| `long sent`, `long to_send` | the same for what's being sent |

## Programs talking to each other

A server listens on a port, and other programs connect to it: on this
computer, or any that can reach it. Text goes both ways along a
connection, most easily a line at a time. Here a server and a program
connecting to it are in the one program, for the example; usually
they're two:

<!-- run -->
```cabby
use networks
use events

object shouter {
	networks.server S
	networks.connection C

	void begin {
		S.on_line heard
		S.listen_here 0                 // 0: any free port
		C = networks.connect "localhost", S.port
		C.on_line answered
		C.send_line "hello"
		C.send_line "goodbye"
		events.run
	}

	// the server's handler, for each line a connection sends
	void heard {
		S.reply S.line.upper
	}

	// the connection's, for each line that comes back
	void answered {
		println "back came", C.line
		if C.line == "GOODBYE" {
			S.close
			C.close
		}
	}
}

int main {
	shouter Sh
	Sh.begin
	return 0
}
```

```text
back came HELLO
back came GOODBYE
```

Handlers are called from the event loop: `events.run`, a window's `run`,
or `events.step` in a loop of the program's own. `events.run` carries on
while a server is listening, or a connection has handlers; closing them
ends it.

### Connections

`networks.connect "localhost", 5000` connects to a program listening on a
port: `"localhost"` for this computer, or a computer's name or address
(`"192.168.1.20"`). Nothing listening there is a panic that says so;
`networks.can_connect Host, Port` looks first.

A program that just takes turns (send something, wait for the answer)
needs no handlers at all: `read_line` waits for the next line.

<!-- check -->
```cabby
use networks

int main {
	networks.connection C = networks.connect "localhost", 5000
	C.send_line "total"
	if C.read_line {
		println "it said", C.line
	}
	C.close
	return 0
}
```

| Method | What it does |
|---|---|
| `bool send_line Text` | sends a line: the text and a line end (`false` if the other end has gone) |
| `bool send Text` | sends text as it is |
| `bool read_line` | waits for the next line, for `line` to give: `false` once the other end has closed it and every line's been read |
| `str line` | the line `read_line` waited for, or the one `on_line`'s handler is told of |
| `on_line h` | calls `h` for each line as it comes |
| `on_close h` | calls `h` when the other end closes the connection |
| `bool is_open` | `true` until either end closes it |
| `str address` | where it's connected to |
| `close` | closes it (`on_close` is for the other end's closing, so it isn't called) |

A line from a program that ends its lines `\r\n` (as many network
programs do) comes without the `\r`, and a last line without a line end
is still a line.

### Servers

| Method | What it does |
|---|---|
| `listen Port` | listens on a port, for any computer that can reach this one; `0`: any free port |
| `listen_here Port` | listens for programs on this computer alone (at both its addresses: IPv4's `127.0.0.1` and IPv6's `::1`, either of which `"localhost"` may be) |
| `long port` | the port it's listening on |
| `on_connect h` | calls `h` when a program connects |
| `on_line h` | calls `h` for each line a connection sends |
| `on_close h` | calls `h` when a connection closes |
| `long from` | in a handler: the connection it's about, by number |
| `str line` | in `on_line`'s handler: the line |
| `bool reply Text` | in a handler: a line back to the connection it's about |
| `bool send_line_to Who, Text` | a line to a connection, by number (`send_to`: text as it is) |
| `send_line_to_all Text` | a line to every connection |
| `list long connections` | the connections open, by number |
| `str address_of Who` | where a connection's from: `"192.168.1.20"` |
| `close_connection Who` | closes one |
| `close` | stops listening, and closes every connection |

A connection's number stays the same while it's open, so it can be a key
in a lookup: who it is, what it's been doing.

<!-- check -->
```cabby
use networks
use events

object chat_room {
	networks.server S
	lookup long str Names

	void begin {
		S.on_connect joined
		S.on_line said
		S.on_close left
		S.listen 5000
		println "listening on port 5000"
		events.run
	}

	void joined {
		Names[S.from] = "someone"
		S.reply "welcome! say 'name' and yours"
	}

	void said {
		if S.line.starts_with "name " {
			Names[S.from] = S.line[5 to]
		} else {
			S.send_line_to_all Names[S.from] + ": " + S.line
		}
	}

	void left {
		long Who = S.from
		S.send_line_to_all Names[Who] + " has gone"
		Names.remove Who
	}
}

int main {
	chat_room Room
	Room.begin
	return 0
}
```

With it running, `nc localhost 5000` (or `telnet`) in a terminal or two
is in the room. A port below 1024 needs the administrator, and one in use
by another program is a panic that says so. Listening with `listen` lets
other computers on the network in, as a firewall allows: `listen_here`
when they needn't be.

## A web server

`networks.web_server` answers browsers: each request (a page asked for, a
form sent, a program asking for JSON) is told to the handler given to
`on_request`, which answers it. A request it doesn't answer is answered
"404 Not Found".

<!-- check -->
```cabby
use networks
use events

object site {
	networks.web_server Web
	int Visits

	void begin {
		Web.on_request answer
		Web.listen 8080
		println "open http://localhost:8080"
		events.run
	}

	void answer {
		Visits++
		if Web.path == "/" {
			Web.send_page "<h1>Hello!</h1><p>Visitor number " + Visits.text + ".</p>"
		} else if Web.path == "/hello" {
			Web.send_page "<p>Hello, " + Web.query_value("name") + "</p>"
		} else if Web.path == "/visits" {
			json Count = json.lookup
			Count["visits"] = json.read Visits.text
			Web.send_json Count
		} else {
			Web.send_from_folder "public"   // pictures, CSS, other pages
		}
	}
}

int main {
	site S
	S.begin
}
```

With it running, `http://localhost:8080` in a browser is the page, and
`http://localhost:8080/hello?name=Ada` greets Ada. Anyone on the network
can visit (as a firewall allows): `listen_here` for this computer alone.

**The request** (in the handler):

| | |
|---|---|
| `str method` | `"GET"` (a page asked for), `"POST"` (a form sent), `"PUT"`, `"DELETE"` ... |
| `str path` | what's asked for, decoded: `"/"`, `"/about"`, `"/pictures/cat.png"` |
| `str query` | the part after `?`, as it was sent: `"q=cabby&page=2"` |
| `str query_value Name`, `bool has_query_value Name` | one value from it, decoded: `query_value "q"` is `"cabby"` (`""` if it isn't there) |
| `str form_value Name`, `bool has_form_value Name` | one value from a form a page sent (`<form method="post">`) |
| `str body`, `json body_json` | what was sent with it, as it came, or read as JSON |
| `str header Name` | one of its headers: `header "User-Agent"` (`""` if not there) |
| `str cookie Name` | one of the browser's cookies |
| `str address` | where it's from: `"192.168.1.20"` |

**The answer** (one to each request):

| | |
|---|---|
| `send_page Html` | a page |
| `send_text Text` | plain text |
| `send_json Value` | JSON, for a program or a page's script |
| `send Status, Type, Content` | anything: `send 201, "text/csv", Rows` |
| `send_status Status, Why` | a short page saying so: `send_status 403, "Not for you."` |
| `send_not_found` | "404 Not Found" |
| `redirect Where` | sends the browser somewhere else: `redirect "/login"` |
| `bool send_file Path` | a file, its type from its name (`.png`: a picture); `false` if it isn't there, and the request still waits for an answer |
| `send_from_folder Folder` | the file in that folder that the path names (`/css/site.css` is `public/css/site.css`; a folder's `index.html` for a folder), or "404 Not Found"; nothing outside the folder is ever sent |
| `send_from_folder_at Folder, Prefix` | the same, for paths beginning with Prefix: `send_from_folder_at "public", "/static"` |
| `add_header Name, Value` | a header of the answer's own, before it's sent: `add_header "Cache-Control", "no-store"` |
| `set_cookie Name, Value` | a cookie for the browser to keep, and send back |
| `bool answered` | whether this request's been answered yet |

`listen`, `listen_here`, `port` and `close` are a server's. Requests are
answered one at a time, in the order they come, as everything in the event
loop is: a handler that takes a long time keeps the others waiting (a
worker can do the long part). A browser's connection stays open for its
next request. It's HTTP, not HTTPS: for a site on the internet, put a web
server that does HTTPS (Caddy, nginx) in front of it.

## WebSockets

A page asks for a page, and is answered; that's all, until it asks again.
A WebSocket is a connection a page (or a program) and the server keep
open, sending each other messages whenever they like: a chat, a game,
numbers that change as you watch. In the page's script, it's `new
WebSocket(...)`; on the server, the request asking for one is told to
`on_request` as any other, and `accept_socket` takes it. From then on, its
messages go to `on_message`.

<!-- check -->
```cabby
use networks
use events

// the page: a list of what's said, a box to say more, and its script
str page {
	str Html = "<!DOCTYPE html><ul id=said></ul><input id=say autofocus><script>\n"
	Html += "const Live = new WebSocket('ws://' + location.host + '/chat')\n"
	Html += "Live.onmessage = e => said.innerHTML += '<li>' + e.data\n"
	Html += "say.onkeydown = e => { if (e.key == 'Enter') { Live.send(say.value); say.value = '' } }\n"
	return Html + "</script>"
}

object chat {
	networks.web_server Web

	void begin {
		Web.on_request answer
		Web.on_message heard
		Web.on_socket_close gone
		Web.listen 8080
		println "open http://localhost:8080, in two windows"
		events.run
	}

	void answer {
		if Web.path == "/chat" && Web.wants_socket {
			long Who = Web.accept_socket
			Web.send_message Who, "Welcome! " + Web.sockets.length.text + " here."
		} else {
			Web.send_page page
		}
	}

	// what one says, everyone hears
	void heard {
		Web.send_message_to_all Web.address + ": " + Web.message
	}

	void gone {
		Web.send_message_to_all "Someone left."
	}
}

int main {
	chat C
	C.begin
}
```

Open it in two browser windows, and what's typed in one appears in both.

**On the web server:**

| | |
|---|---|
| `bool wants_socket` | in `on_request`: the request asks to become a WebSocket |
| `long accept_socket` | in `on_request`: takes it, and gives its number (0 if it wasn't asking: it's answered "400 Bad Request") |
| `on_message Handler` | `Handler` runs for each message a WebSocket sends |
| `on_socket_close Handler` | and when one closes (not for one this end closes) |
| `long from` | in those: the WebSocket it's about |
| `str message`, `bool message_is_binary` | in `on_message`: the message, and whether it was sent as bytes (a page's `Blob` or `ArrayBuffer`) rather than text |
| `bool reply Text` | in `on_message`: a message back |
| `bool send_message Who, Text` | a message to one; `false` if it's gone |
| `send_message_to_all Text` | to every one open |
| `list long sockets` | the ones open, by number |
| `close_socket Who` | closes one, saying so to the other end |

**Connecting to one** (a program talking to a WebSocket server, Cabby's
or anyone's): `networks.open_socket "ws://localhost:8080/chat"`, a panic
saying why if it can't be made.

| `networks.socket` | |
|---|---|
| `bool send Text` | a message; `false` if the server's gone |
| `bool read_message` | waits for the next message; `false` once it's closed |
| `str message`, `bool message_is_binary` | the message `read_message` waited for (or `on_message` is told of) |
| `on_message Handler`, `on_close Handler` | handlers instead, from the event loop |
| `bool is_open`, `close` | |

<!-- check -->
```cabby
use networks

int main {
	networks.socket Live = networks.open_socket "ws://localhost:8080/chat"
	Live.read_message
	println Live.message            // "Welcome! 1 here."
	Live.send "Hello from Cabby"
	Live.read_message
	println Live.message
	Live.close
	return 0
}
```

It's WebSockets as RFC 6455 says, as every browser speaks them: messages
in one piece or several, pings answered with pongs, and a close answered
with a close. Tested with Chromium, Node's own WebSockets and its `ws`
library. A web server with a certificate takes `wss://` (encrypted), as
it takes HTTPS, and `open_socket` connects to `wss://` addresses:
[below](#encrypted-https-wss-and-tls).

## Encrypted: HTTPS, wss and TLS

A server with a certificate is encrypted: a web server's pages are HTTPS
and its WebSockets `wss://`, and a server's connections are TLS, as
HTTPS's are. Nothing sent can be read, or changed, on the way.

<!-- check -->
```cabby
use networks
use events

object site {
	networks.web_server Web

	void begin {
		networks.make_certificate "localhost", "cert.pem", "key.pem"
		Web.set_certificate "cert.pem", "key.pem"
		Web.on_request answer
		Web.listen_here 8443
		println "open https://localhost:8443"
		events.run
	}

	void answer {
		Web.send_page "<h1>Hello, securely!</h1>"
	}
}

int main {
	site S
	S.begin
}
```

**A certificate** says which site it is, signed by someone a browser
trusts. A site on the internet gets one free from Let's Encrypt (with
`certbot`, which writes `fullchain.pem` and `privkey.pem`), and uses them
as they are:

```cabby
Web.set_certificate "/etc/letsencrypt/live/example.org/fullchain.pem", "/etc/letsencrypt/live/example.org/privkey.pem"
```

For trying it on this computer, or a home network, `make_certificate`
makes one of its own (self-signed). A browser warns that it isn't
trusted: go on past the warning, or tell the browser (or the system) to
trust it. A Cabby program trusts it with `networks.trust_certificate`.

| | |
|---|---|
| `networks.make_certificate Name, Certificate, Key` | a certificate of its own for a computer's name (and for `localhost`), and its key: two files, `.pem`; the key readable by its owner alone. It lasts 825 days |
| `set_certificate Certificate, Key` | a web server's, or a server's: encrypted from then on (said before `listen`, or after) |
| `bool encrypted` | a server's, or a connection's: whether it is |
| `networks.trust_certificate Path` | trusted as well as the system's: by downloads, WebSockets and connections from then on |
| `networks.connect_encrypted Host, Port` | an encrypted connection to a server with a certificate, checked as a browser checks a site's |
| `bool networks.can_connect_encrypted Host, Port` | whether it would connect (and its certificate is trusted) |

`networks.open_socket "wss://example.org/chat"` is a WebSocket,
encrypted. A certificate that isn't trusted is a panic that says so:

```text
panic at chat.cby:4:26: can't connect to localhost securely: its certificate isn't trusted (self-signed certificate)
```

It's TLS 1.2 and 1.3, by OpenSSL, which every Linux system has (`libssl3`
on Debian, `openssl` on Arch), loaded when it's first needed. On a Mac
it's Homebrew's (`brew install openssl`: 3 or later). Ports below 1024 (HTTPS's
own, 443) need the administrator; a server that listens on 8443 can be
reached on 443 by the system's firewall forwarding it, or Caddy or nginx
can stand in front.

## This computer, on the network

<!-- check -->
```cabby
use networks

int main {
	if !networks.connected {
		println "not on a network"
		return 1
	}
	println "my address:", networks.local_address
	str Wifi = networks.wifi_name
	if Wifi != "" {
		println "on Wi-Fi:", Wifi
	}
	return 0
}
```

| Function | What it says |
|---|---|
| `str networks.local_address` | This computer's address on its network: `"192.168.1.20"` (an IPv6 one when it has no IPv4; `""` when it isn't on a network). |
| `bool networks.connected` | `true` when it's on a network, by Wi-Fi or a cable. Whether the internet answers is `networks.can_reach`'s to say. |
| `str networks.wifi_name` | The Wi-Fi network's name (`""` when it isn't on Wi-Fi). |

They're the same on Linux and on a Mac. For the Wi-Fi network's name,
Linux asks `iwgetid` or NetworkManager's `nmcli`; a Mac asks
`networksetup` and `ipconfig`. macOS 14 and later only tells a program
that's been allowed Location Services (Terminal, say, in System Settings,
Privacy & Security, Location Services), and `""` otherwise.

### Who's there

Which computers on the network have something listening on a port, all
asked at once, and pings:

<!-- check -->
```cabby
use networks
use time

int main {
	list str Near = networks.nearby_addresses     // 192.168.1.1 to .254, but this one
	for Host in networks.answering Near, 22, time.milliseconds(500) {
		println Host, "has ssh"
	}
	if networks.pings "192.168.1.1", time.seconds(1) {
		println "the router answers, in", networks.ping_time("192.168.1.1", time.seconds(1)).milliseconds, "ms"
	}
}
```

| Function | What it gives |
|---|---|
| `list str networks.nearby_addresses` | The other addresses on this computer's network when it's a home or office one with room for 254: `192.168.1.1` to `192.168.1.254`, without its own. Empty when it isn't on an IPv4 network. |
| `list str networks.answering Hosts, Port, Wait` | Those of `Hosts` with a program listening on `Port`. Every one is asked at once, so 254 addresses take `Wait`, not 254 times it. One that hasn't answered within `Wait` isn't counted. |
| `bool networks.pings Host, Wait` | `true` if `Host` answers a ping within `Wait`. |
| `duration networks.ping_time Host, Wait` | How long its answer took to come back; a panic if there's none within `Wait` (`pings` asks first, or `else` catches it). |

A ping is an ICMP echo, as the `ping` command sends. Cabby sends it
itself where the system lets any program (a Mac does, and Linux does when
`net.ipv4.ping_group_range` takes in the program's group, as systemd sets
it on Debian and Arch), and asks the `ping` command otherwise. Many
computers are set not to answer pings (Windows, and Macs in stealth
mode), so no answer doesn't mean nobody's there; `answering` on a port
they serve finds them anyway.

Ask only the computers you're allowed to. On a network that isn't yours,
asking every computer what it's listening on is what an attacker does
first, and it's noticed.

## When it goes wrong

A download that fails is a panic that says why, at the line that asked:

```text
panic at prog.cby:5:16: can't download 'https://example.com/nope': 404 Not Found: nothing is at that address
panic at prog.cby:5:16: can't download 'https://nope.invalid': Could not resolve host: nope.invalid
```

When it might not work (the internet may be down, or a person typed the
address), check first with `networks.can_reach`, or look at the reason
with `networks.status`:

| Status | Means |
|---|---|
| 200 to 299 | it's there |
| 300 to 399 | it moved (downloads follow it) |
| 404 | nothing is at that address |
| 403, 401 | the site refuses, or wants a login |
| 500 to 599 | a problem at the site |
| 0 | nothing answered: no connection, or no such site |

`networks.download_to` writes into `Path.download` first, and only gives it
its name once the whole file has arrived, so a failed download never
leaves half a file behind.

## How it works

Downloads and requests use libcurl, the library behind the `curl` command,
which every Linux system and every Mac has. It is loaded the first time a
program downloads something, so a program that never does doesn't need
it. If it isn't installed, the panic says to install the `curl` package.

Connections and servers use the system's own sockets (TCP), the same on
Linux and on a Mac, so a Cabby program on one talks to a Cabby program on
the other, or to any program that sends lines: `nc`, a Python script, a
chat program. `runtime/connections.c` has them; `runtime/tls.h` encrypts
them, with OpenSSL.

## Still to come

Messages other than lines (sizes and bytes), sending binary WebSocket
messages (they're received already), certificates from Let's Encrypt
fetched by the program itself (ACME), and HTTP/2.
