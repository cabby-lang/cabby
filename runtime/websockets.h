/* websockets.h — WebSockets (RFC 6455), for the networks library
 * (included by connections.c, after web.h): a connection that a web
 * page (or a program) and a server keep open, sending messages both ways
 * whenever they like, rather than a request and its answer.
 *
 * On a web server: a request asking to become a WebSocket ("Upgrade:
 * websocket") is told to on_request as any other; accept_socket answers
 * it "101 Switching Protocols", and from then on its connection carries
 * messages, each told to on_message. Connecting to one (networks.socket):
 * the same handshake, from the other side.
 *
 * What's sent goes in frames: a text or binary message (in one frame, or
 * in pieces), and the control frames: ping (answered with a pong), pong,
 * and close (answered with a close, then the connection closes). What a
 * client sends is masked, as the standard says; what a server sends
 * isn't. wss:// (and a web server's HTTPS) is the same, through TLS
 * (tls.h). */

#ifdef __APPLE__
#include <sys/random.h>
#endif

#define WS_MOST (16 * 1024 * 1024)   /* a message, at most */

void *rt_sha1_start(void);
void rt_sha1_add(void *h, const void *p, size_t n);
void rt_sha1_end(void *h, unsigned char out[20]);

static char *ws_base64(const unsigned char *s, size_t n)
{
	static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	char *out = malloc(4 * ((n + 2) / 3) + 1), *o = out;
	for (size_t i = 0; i < n; i += 3) {
		uint32_t v = (uint32_t)s[i] << 16 | (i + 1 < n ? (uint32_t)s[i + 1] << 8 : 0) | (i + 2 < n ? s[i + 2] : 0);
		*o++ = b64[v >> 18 & 63];
		*o++ = b64[v >> 12 & 63];
		*o++ = i + 1 < n ? b64[v >> 6 & 63] : '=';
		*o++ = i + 2 < n ? b64[v & 63] : '=';
	}
	*o = 0;
	return out;
}

/* the answer to a key: base64(SHA-1(key + the standard's own GUID)) */
static char *ws_accept_for(const char *key)
{
	static const char guid[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
	void *h = rt_sha1_start();
	rt_sha1_add(h, key, strlen(key));
	rt_sha1_add(h, guid, sizeof guid - 1);
	unsigned char d[20];
	rt_sha1_end(h, d);
	return ws_base64(d, 20);
}

static void ws_random(unsigned char *p, size_t n)
{
	if (getentropy(p, n) != 0)
		for (size_t i = 0; i < n; i++)
			p[i] = (unsigned char)rand();
}

/* a frame: its kind (1 text, 2 binary, 8 close, 9 ping, 10 pong), and
 * what's in it; masked from a client. False if it couldn't be sent. */
static bool ws_send_frame(int64_t id, int op, const char *data, size_t len)
{
	Conn *c = conn(id);
	if (c->fd < 0 || c->ws_closing)
		return false;
	unsigned char head[14];
	size_t h = 0;
	head[h++] = (unsigned char)(0x80 | op);
	unsigned char mask = c->ws_client ? 0x80 : 0;
	if (len < 126) {
		head[h++] = (unsigned char)(mask | len);
	} else if (len < 65536) {
		head[h++] = mask | 126;
		head[h++] = (unsigned char)(len >> 8);
		head[h++] = (unsigned char)len;
	} else {
		head[h++] = mask | 127;
		for (int i = 7; i >= 0; i--)
			head[h++] = (unsigned char)((uint64_t)len >> (8 * i));
	}
	unsigned char key[4] = { 0 };
	if (c->ws_client) {
		ws_random(key, 4);
		memcpy(head + h, key, 4);
		h += 4;
	}
	char *whole = malloc(h + len + 1);
	memcpy(whole, head, h);
	for (size_t i = 0; i < len; i++)
		whole[h + i] = c->ws_client ? (char)(data[i] ^ key[i & 3]) : data[i];
	bool ok = cby_conn_send(id, whole, (int64_t)(h + len));
	free(whole);
	if (op == 8)
		c->ws_closing = 1;
	return ok;
}

/* a close frame, with its code (1000: all's well) */
static void ws_send_close(int64_t id, int code)
{
	char b[2] = { (char)(code >> 8), (char)code };
	ws_send_frame(id, 8, b, 2);
}

/* The next frame in the connection's buffer, taken out of it: 1 a
 * message is whole (c->line), 2 a control frame (dealt with), 0 not
 * there yet, -1 the connection's to close (a close frame, or one that
 * breaks the rules). */
static int ws_frame(int64_t id)
{
	Conn *c = conn(id);
	if (c->in_len < 2)
		return 0;
	unsigned char *p = (unsigned char *)c->in;
	int fin = p[0] & 0x80, op = p[0] & 0x0f, masked = p[1] & 0x80;
	uint64_t len = p[1] & 0x7f;
	size_t h = 2;
	if (len == 126) {
		if (c->in_len < 4)
			return 0;
		len = (uint64_t)p[2] << 8 | p[3];
		h = 4;
	} else if (len == 127) {
		if (c->in_len < 10)
			return 0;
		len = 0;
		for (int i = 0; i < 8; i++)
			len = len << 8 | p[2 + i];
		h = 10;
	}
	if (len > WS_MOST || c->ws_frag_len + len > WS_MOST) {
		ws_send_close(id, 1009);   /* (too big) */
		return -1;
	}
	if ((bool)masked == (bool)c->ws_client) {
		/* (a client's frames are masked, a server's not: the standard says
		 * to close on one that isn't as it should be) */
		ws_send_close(id, 1002);
		return -1;
	}
	unsigned char key[4] = { 0 };
	if (masked) {
		if (c->in_len < h + 4)
			return 0;
		memcpy(key, p + h, 4);
		h += 4;
	}
	if (c->in_len < h + len)
		return 0;
	char *data = malloc(len + 1);
	for (uint64_t i = 0; i < len; i++)
		data[i] = (char)(p[h + i] ^ key[i & 3]);
	data[len] = 0;
	memmove(c->in, c->in + h + len, c->in_len - h - len);
	c->in_len -= h + len;
	int got = 2;
	if (op == 1 || op == 2 || op == 0) {
		if (op != 0 && c->ws_frag_len) {
			/* (a new message while one's in pieces) */
			free(data);
			ws_send_close(id, 1002);
			return -1;
		}
		c->ws_frag = realloc(c->ws_frag, c->ws_frag_len + len + 1);
		memcpy(c->ws_frag + c->ws_frag_len, data, len);
		c->ws_frag_len += len;
		c->ws_frag[c->ws_frag_len] = 0;
		if (op != 0)
			c->ws_binary = op == 2;
		if (fin) {
			free(c->line);
			c->line = c->ws_frag;
			c->line_len = c->ws_frag_len;
			c->ws_frag = NULL;
			c->ws_frag_len = 0;
			got = 1;
		} else {
			got = 2;
		}
	} else if (op == 9) {
		ws_send_frame(id, 10, data, len);   /* ping: pong */
	} else if (op == 8) {
		/* close: answered with a close (the same code), then closed */
		if (!c->ws_closing)
			ws_send_frame(id, 8, data, len >= 2 ? 2 : 0);
		got = -1;
	} else if (op != 10) {
		ws_send_close(id, 1002);   /* (a kind of frame there isn't) */
		got = -1;
	}
	free(data);
	return got;
}

/* what's come on a WebSocket: each message to its handler (the server's
 * on_message, or the connection's own) */
static void ws_readable(int64_t id, bool open)
{
	for (;;) {
		if (id > nconns || !conns[id - 1] || conns[id - 1]->fd < 0)
			return;
		int got = ws_frame(id);
		if (got == 0)
			break;
		if (got < 0) {
			conn_closed(id);
			return;
		}
		if (got == 1) {
			Conn *c = conns[id - 1];
			if (c->server) {
				Server *s = servers[c->server - 1];
				if (s && s->on_message.fn) {
					s->from = id;
					rt_invoke_hook(s->on_message.obj, s->on_message.fn);
				}
			} else if (c->on_line.fn) {
				rt_invoke_hook(c->on_line.obj, c->on_line.fn);
			}
		}
	}
	if (!open)
		conn_closed(id);
}

/* ---- on a web server */

/* whether the request asks to become a WebSocket */
int cby_web_wants_socket(int64_t sid)
{
	Request *r = web_request(sid);
	const char *up = header_of(r, "upgrade"), *how = header_of(r, "connection");
	return strcmp(r->method, "GET") == 0 && up && has_word(up, "websocket") && how && has_word(how, "upgrade") &&
	       header_of(r, "sec-websocket-key") != NULL;
}

/* accepts it: its connection is a WebSocket from now on; its number (0:
 * it wasn't asking, and it's been answered "400 Bad Request") */
int64_t cby_web_accept_socket(int64_t sid)
{
	Request *r = web_request(sid);
	if (r->answered)
		rt_panic(NULL, 0, 0, "%s", "this request has been answered already (one answer to each)");
	if (!cby_web_wants_socket(sid)) {
		web_simple(r, 400, "This isn't asking for a WebSocket.");
		return 0;
	}
	const char *version = header_of(r, "sec-websocket-version");
	if (!version || strcmp(version, "13") != 0) {
		cby_web_add_header(sid, "Sec-WebSocket-Version", "13");
		web_simple(r, 426, "Only WebSockets of version 13 are spoken here.");
		return 0;
	}
	char *accept = ws_accept_for(header_of(r, "sec-websocket-key"));
	char head[256];
	int n = snprintf(head, sizeof head,
			 "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
			 "Sec-WebSocket-Accept: %s\r\n\r\n", accept);
	free(accept);
	r->answered = 1;
	r->keep_alive = 1;
	if (r->conn < 1 || r->conn > nconns || !conns[r->conn - 1])
		return 0;
	cby_conn_send(r->conn, head, n);
	conns[r->conn - 1]->ws = 1;
	return r->conn;
}

/* 0 on_message, 1 on_socket_close */
void cby_web_on_socket(int64_t sid, int64_t which, void *obj, void *fn)
{
	Server *s = server(sid);
	if (which == 0)
		s->on_message = (Hook){ obj, fn };
	else
		s->on_close = (Hook){ obj, fn };
}

/* the WebSockets open, by number */
int64_t cby_web_socket_count(int64_t sid)
{
	Server *s = server(sid);
	int64_t n = 0;
	for (int i = 0; i < s->nconns; i++)
		if (conns[s->conns[i] - 1]->ws && conns[s->conns[i] - 1]->fd >= 0)
			n++;
	return n;
}

int64_t cby_web_socket(int64_t sid, int64_t which)
{
	Server *s = server(sid);
	int64_t n = 0;
	for (int i = 0; i < s->nconns; i++)
		if (conns[s->conns[i] - 1]->ws && conns[s->conns[i] - 1]->fd >= 0 && n++ == which)
			return s->conns[i];
	return 0;
}

/* ---- either end */

/* the message being told of (or read_message waited for) */
int64_t cby_ws_message(int64_t id)
{
	Conn *c = conn(id);
	return c->line ? give(c->line, c->line_len) : give("", 0);
}

int cby_ws_binary(int64_t id) { return conn(id)->ws_binary; }

/* a message: text, or (binary) bytes; false if it's gone */
int cby_ws_send(int64_t id, const char *data, int64_t len, int binary)
{
	if (id < 1 || id > nconns || !conns[id - 1] || !conns[id - 1]->ws)
		return 0;
	return ws_send_frame(id, binary ? 2 : 1, data, (size_t)len);
}

/* closed from this end: a close frame, then the connection */
void cby_ws_close(int64_t id)
{
	if (id < 1 || id > nconns || !conns[id - 1])
		return;
	if (conns[id - 1]->ws && conns[id - 1]->fd >= 0)
		ws_send_close(id, 1000);
	cby_conn_close(id);
}

/* ---- connecting to one */

/* ws://host[:port]/path: a WebSocket to it, its number, or 0
 * (cby_conn_error says why) */
int64_t cby_ws_open(const char *url)
{
	const char *rest;
	int secure = 0;
	if (strncmp(url, "ws://", 5) == 0) {
		rest = url + 5;
	} else if (strncmp(url, "wss://", 6) == 0) {
		rest = url + 6;   /* (encrypted: TLS, as HTTPS is) */
		secure = 1;
	} else {
		say_error("'%s' isn't a WebSocket's address: they begin ws:// (or wss://, encrypted)%s", url, "");
		return 0;
	}
	const char *slash = strchr(rest, '/');
	size_t hl = slash ? (size_t)(slash - rest) : strlen(rest);
	char host[256], hostport[300];
	if (hl >= sizeof host)
		hl = sizeof host - 1;
	memcpy(host, rest, hl);
	host[hl] = 0;
	snprintf(hostport, sizeof hostport, "%s", host);
	const char *path = slash ? slash : "/";
	long port = secure ? 443 : 80;
	char *colon = host[0] == '[' ? strstr(host, "]:") : strrchr(host, ':');
	if (colon) {
		if (host[0] == '[')
			colon++;
		port = strtol(colon + 1, NULL, 10);
		*colon = 0;
	}
	if (host[0] == '[') {
		memmove(host, host + 1, strlen(host));
		char *close_ = strchr(host, ']');
		if (close_)
			*close_ = 0;
	}
	int64_t id = cby_conn_open(host, port);
	if (!id)
		return 0;
	if (secure && !tls_connect(id, host)) {
		cby_conn_free(id);
		return 0;
	}
	unsigned char raw[16];
	ws_random(raw, 16);
	char *key = ws_base64(raw, 16);
	size_t n = strlen(path) + strlen(hostport) + strlen(key) + 200;
	char *ask = malloc(n);
	snprintf(ask, n,
		 "GET %s HTTP/1.1\r\nHost: %s\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
		 "Sec-WebSocket-Key: %s\r\nSec-WebSocket-Version: 13\r\n\r\n",
		 path, hostport, key);
	cby_conn_send(id, ask, (int64_t)strlen(ask));
	free(ask);
	/* the answer's headers: "101", and the key answered */
	Conn *c = conn(id);
	char *end = NULL;
	while (!end) {
		for (size_t i = 0; i + 3 < c->in_len && !end; i++)
			if (memcmp(c->in + i, "\r\n\r\n", 4) == 0)
				end = c->in + i;
		if (end)
			break;
		bool was = rt_gil_drop();
		bool more = c->in_len < WEB_MOST_HEAD && fill(c, true);
		rt_gil_take(was);
		if (!more) {
			say_error("%s%s", "the server closed the connection instead of answering", "");
			free(key);
			cby_conn_free(id);
			return 0;
		}
	}
	size_t head_len = (size_t)(end - c->in) + 4;
	char *head = strndup(c->in, head_len);
	memmove(c->in, c->in + head_len, c->in_len - head_len);
	c->in_len -= head_len;
	char *want = ws_accept_for(key);
	free(key);
	int ok = strncmp(head, "HTTP/1.1 101", 12) == 0 || strncmp(head, "HTTP/1.0 101", 12) == 0;
	int answered = 0;
	for (char *line = strstr(head, "\r\n"); ok && line; line = strstr(line + 2, "\r\n"))
		if (strncasecmp(line + 2, "sec-websocket-accept:", 21) == 0) {
			const char *v = line + 23;
			while (*v == ' ')
				v++;
			answered = strncmp(v, want, strlen(want)) == 0;
		}
	free(want);
	if (!ok || !answered) {
		char first[200];
		char *e = strstr(head, "\r\n");
		snprintf(first, sizeof first, "%.*s", e ? (int)(e - head) : 100, head);
		say_error("the server didn't take the WebSocket: it answered '%s'%s", first, "");
		free(head);
		cby_conn_free(id);
		return 0;
	}
	free(head);
	c->ws = 1;
	c->ws_client = 1;
	return id;
}

/* waits for the next message: its length, or -1 when the connection's
 * closed */
int64_t cby_ws_read(int64_t id)
{
	for (;;) {
		Conn *c = conn(id);
		if (c->fd >= 0 || c->in_len) {
			int got = ws_frame(id);
			if (got == 1)
				return cby_ws_message(id);
			if (got == 2)
				continue;
			if (got < 0) {
				c = conn(id);
				if (c->fd >= 0) {
					close(c->fd);
					c->fd = -1;
				}
				stop_watching(c);
				return -1;
			}
		}
		c = conn(id);
		if (c->fd < 0)
			return -1;
		bool was = rt_gil_drop();
		bool more = fill(c, true);
		rt_gil_take(was);
		if (!more) {
			close(c->fd);
			c->fd = -1;
			stop_watching(c);
			return -1;
		}
	}
}
