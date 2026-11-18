/* connections.c — programs talking to each other over the network, for the
 * networks library (networks.connect, networks.server): the same on Linux
 * and macOS, through the system's own sockets.
 *
 * A connection carries text both ways, most easily a line at a time. A
 * server listens on a port, and each program that connects to it is a
 * connection of its own, known by its number. What arrives is told to the
 * program's handlers from the event loop (events.run, or a window's), as
 * processes' output and D-Bus signals are: a line from a connection, a new
 * connection, one closed. A program can also just wait for the next line
 * (read_line), with no handlers at all. */
#include "rt.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

extern int cby_events_watch(int fd, void (*fn)(void *data), void *data);
extern void cby_events_unwatch(int id);

typedef struct {
	void *obj, *fn;
} Hook;

typedef struct {
	int fd;                /* -1: closed */
	int watch;             /* in the event loop (0: not) */
	char *in;              /* what's come and not yet been given as a line */
	size_t in_len, in_cap;
	char *line;            /* the line being told */
	int64_t server;        /* the server it came to (0: one the program made) */
	char peer[64];         /* where it's from: "192.168.1.20" */
	Hook on_line, on_close;
	bool told_closed;
	/* a WebSocket's (websockets.h): it is one; this end connected to it
	 * (and masks what it sends); a close has been sent; the message in
	 * line is binary, and its length; a message coming in pieces */
	int ws, ws_client, ws_closing, ws_binary;
	size_t line_len;
	char *ws_frag;
	size_t ws_frag_len;
	/* encrypted (tls.h): its TLS (an SSL), and whether its handshake is
	 * still going on (a server's, as what comes allows) */
	void *tls;
	int tls_handshaking;
} Conn;

typedef struct {
	int fd, fd2;           /* (fd2: listen_here's IPv6 one, beside IPv4's; -1: none) */
	int watch, watch2;
	int port;
	Hook on_connect, on_line, on_close;
	int64_t from;          /* the connection being told of */
	int64_t *conns;        /* its connections, open */
	int nconns;
	/* a web server's (web_server: HTTP rather than lines) */
	int web;
	Hook on_request;
	struct Request *req;   /* the request being answered */
	Hook on_message;       /* its WebSockets' messages (on_close: one closed) */
	void *tls;             /* its certificate (an SSL_CTX: tls.h), for HTTPS; NULL: none */
} Server;

/* The connections and servers, by number. A worker's thread opens
 * connections while the main thread's event loop accepts them, so the
 * tables never move (one growing with realloc could be freed under the
 * other's feet), and a slot is claimed with `slots` held. */
#define CONN_MOST 65536
#define SERVER_MOST 1024
static Conn *conns[CONN_MOST];
static _Atomic int64_t nconns;
static Server *servers[SERVER_MOST];
static _Atomic int64_t nservers;
static pthread_mutex_t slots = PTHREAD_MUTEX_INITIALIZER;

static Conn *conn(int64_t id)
{
	if (id < 1 || id > nconns || !conns[id - 1])
		rt_panic(NULL, 0, 0, "there's no connection %lld (it was closed, and dropped)", (long long)id);
	return conns[id - 1];
}

static Server *server(int64_t id)
{
	if (id < 1 || id > nservers || !servers[id - 1])
		rt_panic(NULL, 0, 0, "that server has been closed");
	return servers[id - 1];
}

/* (tls.h's, below) */
static int tls_fill(Conn *c, bool wait);
static bool tls_send(Conn *c, const char *data, int64_t len);
static void tls_end(Conn *c);
static void tls_accept_start(Server *s, Conn *c);
static int tls_accept_more(Conn *c);
static void tls_forget(void);
static void tls_server_free(Server *s);
static bool tls_connect(int64_t id, const char *host);

static int64_t new_conn(int fd, int64_t server_id)
{
	Conn *c = calloc(1, sizeof *c);
	c->fd = fd;
	c->server = server_id;
	pthread_mutex_lock(&slots);
	int64_t i = 0;
	while (i < nconns && conns[i])
		i++;
	if (i == CONN_MOST) {
		pthread_mutex_unlock(&slots);
		rt_panic(NULL, 0, 0, "too many connections open at once (%d)", CONN_MOST);
	}
	conns[i] = c;
	if (i == nconns)
		nconns++;
	pthread_mutex_unlock(&slots);
	/* (no SIGPIPE when the other end has gone: a write just fails) */
#ifdef SO_NOSIGPIPE
	int one = 1;
	setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#endif
	int yes = 1;
	setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof yes);   /* (a line goes as it's sent) */
	return i + 1;
}

/* text back to Cabby: its length, then copied by cby_conn_take */
static _Thread_local const char *giving;
static _Thread_local size_t giving_len;
static char *error_text;   /* (set with the lock held: one for the program) */

static int64_t give(const char *p, size_t n)
{
	giving = p;
	giving_len = n;
	return (int64_t)n;
}

void cby_conn_take(char *dst)
{
	if (giving_len)
		memcpy(dst, giving, giving_len);
}

static void say_error(const char *fmt, const char *a, const char *b)
{
	char buf[512];
	snprintf(buf, sizeof buf, fmt, a, b);
	free(error_text);
	error_text = strdup(buf);
}

/* ------------------------------------------------------------ connecting */

/* a connection to a program listening at host and port: its number, or 0
 * (cby_conn_error says why) */
int64_t cby_conn_open(const char *host, int64_t port)
{
	char ports[16];
	snprintf(ports, sizeof ports, "%lld", (long long)port);
	struct addrinfo hints = { 0 }, *found = NULL;
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	bool was = rt_gil_drop();
	int got = getaddrinfo(host, ports, &hints, &found);
	rt_gil_take(was);
	if (got != 0) {
		say_error("there's no computer called '%s' (%s)", host, gai_strerror(got));
		return 0;
	}
	int fd = -1;
	char why[200] = "nothing answered";
	for (struct addrinfo *a = found; a && fd < 0; a = a->ai_next) {
		fd = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
		if (fd < 0)
			continue;
		fcntl(fd, F_SETFD, FD_CLOEXEC);
		was = rt_gil_drop();
		int r = connect(fd, a->ai_addr, a->ai_addrlen);
		rt_gil_take(was);
		if (r != 0) {
			snprintf(why, sizeof why, "%s", errno == ECONNREFUSED ? "nothing is listening on that port" : strerror(errno));
			close(fd);
			fd = -1;
		}
	}
	freeaddrinfo(found);
	if (fd < 0) {
		char at[300];
		snprintf(at, sizeof at, "%s:%lld", host, (long long)port);
		say_error("can't connect to %s: %s", at, why);
		return 0;
	}
	int64_t id = new_conn(fd, 0);
	snprintf(conn(id)->peer, sizeof conn(id)->peer, "%s", host);
	return id;
}

int64_t cby_conn_error(void)
{
	return error_text ? give(error_text, strlen(error_text)) : give("", 0);
}

void cby_conn_panic(void)
{
	rt_panic(NULL, 0, 0, "%s", error_text ? error_text : "it couldn't connect");
}

/* ------------------------------------------------------------ sending */

/* all of it, or false (the other end has gone) */
int cby_conn_send(int64_t id, const char *data, int64_t len)
{
	Conn *c = conn(id);
	if (c->fd < 0)
		return 0;
	if (c->tls) {
		bool was = rt_gil_drop();
		bool ok = tls_send(c, data, len);
		rt_gil_take(was);
		return ok;
	}
	bool was = rt_gil_drop();
	int64_t done = 0;
	while (done < len) {
#ifdef MSG_NOSIGNAL
		ssize_t w = send(c->fd, data + done, (size_t)(len - done), MSG_NOSIGNAL);
#else
		ssize_t w = send(c->fd, data + done, (size_t)(len - done), 0);
#endif
		if (w < 0 && errno == EINTR)
			continue;
		if (w <= 0)
			break;
		done += w;
	}
	rt_gil_take(was);
	return done == len;
}

/* ------------------------------------------------------------ receiving */

static void conn_closed(int64_t id);
static void web_readable(int64_t id, bool open);
static void ws_readable(int64_t id, bool open);
static void web_forget(Server *s);

/* whatever has come (once: read waits for it) onto the connection's
 * buffer: false when the other end has closed it. An encrypted one's,
 * through TLS: what it has decrypted, waiting for some if wait (the event
 * loop's doesn't: it may have none yet) */
static bool fill(Conn *c, bool wait)
{
	if (c->tls)
		return tls_fill(c, wait) >= 0;
	if (c->in_cap - c->in_len < 4096) {
		c->in_cap = c->in_cap ? c->in_cap * 2 : 8192;
		c->in = realloc(c->in, c->in_cap);
	}
	ssize_t n;
	do
		n = recv(c->fd, c->in + c->in_len, c->in_cap - c->in_len - 1, 0);
	while (n < 0 && errno == EINTR);
	if (n <= 0)
		return false;
	c->in_len += (size_t)n;
	return true;
}

/* the next whole line in the buffer (without its line end), taken out of
 * it into c->line: false if there isn't one yet */
static bool next_line(Conn *c)
{
	char *nl = c->in_len ? memchr(c->in, '\n', c->in_len) : NULL;
	if (!nl)
		return false;
	size_t n = (size_t)(nl - c->in);
	free(c->line);
	c->line = malloc(n + 1);
	memcpy(c->line, c->in, n);
	if (n && c->line[n - 1] == '\r')
		n--;   /* (a line from a program that ends them \r\n) */
	c->line[n] = 0;
	size_t used = (size_t)(nl - c->in) + 1;
	memmove(c->in, c->in + used, c->in_len - used);
	c->in_len -= used;
	return true;
}

/* waits for the next line: its length, or -1 when the connection's closed
 * and nothing's left (a last line with no line end is given as a line) */
int64_t cby_conn_read_line(int64_t id)
{
	Conn *c = conn(id);
	for (;;) {
		if (next_line(c))
			return give(c->line, strlen(c->line));
		if (c->fd < 0)
			break;
		bool was = rt_gil_drop();
		bool more = fill(c, true);
		rt_gil_take(was);
		if (!more) {
			close(c->fd);
			c->fd = -1;
			break;
		}
	}
	if (c->in_len) {
		/* the end, without a line end: the last line all the same */
		free(c->line);
		c->line = strndup(c->in, c->in_len);
		c->in_len = 0;
		return give(c->line, strlen(c->line));
	}
	return -1;
}

/* the line being told to on_line's handler */
int64_t cby_conn_line(int64_t id)
{
	Conn *c = conn(id);
	return c->line ? give(c->line, strlen(c->line)) : give("", 0);
}

int cby_conn_is_open(int64_t id) { return id >= 1 && id <= nconns && conns[id - 1] && conns[id - 1]->fd >= 0; }

int64_t cby_conn_peer(int64_t id)
{
	Conn *c = conn(id);
	return give(c->peer, strlen(c->peer));
}

/* the event loop says there's something to read: each whole line to the
 * handler (the connection's own, or its server's) */
static void readable(void *data)
{
	int64_t id = (int64_t)(intptr_t)data;
	if (id < 1 || id > nconns || !conns[id - 1] || conns[id - 1]->fd < 0)
		return;
	Conn *c = conns[id - 1];
	if (c->tls_handshaking) {
		int done = tls_accept_more(c);
		if (done < 0) {
			c->told_closed = true;   /* (it never was connected, to tell of) */
			conn_closed(id);
			return;
		}
		if (done == 0)
			return;
		/* shaken hands: connected now, as far as the program's concerned */
		Server *s = c->server ? servers[c->server - 1] : NULL;
		if (s && s->on_connect.fn) {
			s->from = id;
			rt_invoke_hook(s->on_connect.obj, s->on_connect.fn);
		}
		if (id > nconns || !conns[id - 1] || conns[id - 1]->fd < 0)
			return;
		c = conns[id - 1];
	}
	bool open = fill(c, false);
	if (c->ws) {
		ws_readable(id, open);
		return;
	}
	if (c->server && servers[c->server - 1] && servers[c->server - 1]->web) {
		web_readable(id, open);
		return;
	}
	while (id <= nconns && conns[id - 1] && next_line(conns[id - 1])) {
		c = conns[id - 1];
		if (c->server) {
			Server *s = servers[c->server - 1];
			if (s && s->on_line.fn) {
				s->from = id;
				rt_invoke_hook(s->on_line.obj, s->on_line.fn);
			}
		} else if (c->on_line.fn) {
			rt_invoke_hook(c->on_line.obj, c->on_line.fn);
		}
	}
	if (!open)
		conn_closed(id);
}

static void stop_watching(Conn *c)
{
	if (c->watch) {
		cby_events_unwatch(c->watch);
		c->watch = 0;
	}
}

/* the other end has gone (or this end closed it): told once, to on_close */
static void conn_closed(int64_t id)
{
	if (id < 1 || id > nconns || !conns[id - 1])
		return;
	Conn *c = conns[id - 1];
	stop_watching(c);
	tls_end(c);
	if (c->fd >= 0) {
		close(c->fd);
		c->fd = -1;
	}
	if (c->told_closed)
		return;
	c->told_closed = true;
	if (c->in_len && !c->ws && !(c->server && servers[c->server - 1] && servers[c->server - 1]->web)) {
		/* (a last line without a line end, first) */
		free(c->line);
		c->line = strndup(c->in, c->in_len);
		c->in_len = 0;
		if (c->server && servers[c->server - 1] && servers[c->server - 1]->on_line.fn) {
			servers[c->server - 1]->from = id;
			rt_invoke_hook(servers[c->server - 1]->on_line.obj, servers[c->server - 1]->on_line.fn);
		} else if (!c->server && c->on_line.fn) {
			rt_invoke_hook(c->on_line.obj, c->on_line.fn);
		}
	}
	if (id > nconns || !conns[id - 1])
		return;
	c = conns[id - 1];
	if (c->server) {
		Server *s = c->server <= nservers ? servers[c->server - 1] : NULL;
		if (s) {
			for (int i = 0; i < s->nconns; i++)
				if (s->conns[i] == id) {
					s->conns[i] = s->conns[--s->nconns];
					break;
				}
			if (s->on_close.fn && (!s->web || c->ws)) {   /* (a web server's: its WebSockets') */
				s->from = id;
				rt_invoke_hook(s->on_close.obj, s->on_close.fn);
			}
		}
	} else if (c->on_close.fn) {
		rt_invoke_hook(c->on_close.obj, c->on_close.fn);
	}
}

/* the program's handlers, for a connection it made; the event loop
 * watches it from then on */
void cby_conn_on_line(int64_t id, void *obj, void *fn)
{
	Conn *c = conn(id);
	c->on_line = (Hook){ obj, fn };
	if (!c->watch && c->fd >= 0)
		c->watch = cby_events_watch(c->fd, readable, (void *)(intptr_t)id);
}

void cby_conn_on_close(int64_t id, void *obj, void *fn)
{
	Conn *c = conn(id);
	c->on_close = (Hook){ obj, fn };
	if (!c->watch && c->fd >= 0)
		c->watch = cby_events_watch(c->fd, readable, (void *)(intptr_t)id);
}

/* closed from this end (on_close isn't called for it) */
void cby_conn_close(int64_t id)
{
	if (id < 1 || id > nconns || !conns[id - 1])
		return;
	Conn *c = conns[id - 1];
	stop_watching(c);
	tls_end(c);
	if (c->fd >= 0) {
		close(c->fd);
		c->fd = -1;
	}
	c->told_closed = true;
	if (c->server && c->server <= nservers && servers[c->server - 1]) {
		Server *s = servers[c->server - 1];
		for (int i = 0; i < s->nconns; i++)
			if (s->conns[i] == id) {
				s->conns[i] = s->conns[--s->nconns];
				break;
			}
	}
}

/* closed and forgotten (a connection object dropped) */
void cby_conn_free(int64_t id)
{
	if (id < 1 || id > nconns || !conns[id - 1])
		return;
	cby_conn_close(id);
	tls_end(conns[id - 1]);
	free(conns[id - 1]->in);
	free(conns[id - 1]->line);
	free(conns[id - 1]->ws_frag);
	free(conns[id - 1]);
	conns[id - 1] = NULL;
}

/* ------------------------------------------------------------ listening */

/* the event loop says a program is connecting: to the server's first
 * socket, or (data odd) its second */
static void accept_one(void *data)
{
	int64_t sid = (int64_t)(intptr_t)data / 2;
	int second = (int)((intptr_t)data % 2);
	if (sid < 1 || sid > nservers || !servers[sid - 1])
		return;
	Server *s = servers[sid - 1];
	struct sockaddr_storage from;
	socklen_t len = sizeof from;
	int fd = accept(second ? s->fd2 : s->fd, (struct sockaddr *)&from, &len);
	if (fd < 0)
		return;
	fcntl(fd, F_SETFD, FD_CLOEXEC);
	int64_t id = new_conn(fd, sid);
	Conn *c = conns[id - 1];
	if (from.ss_family == AF_INET)
		inet_ntop(AF_INET, &((struct sockaddr_in *)&from)->sin_addr, c->peer, sizeof c->peer);
	else if (from.ss_family == AF_INET6) {
		struct in6_addr *a6 = &((struct sockaddr_in6 *)&from)->sin6_addr;
		if (IN6_IS_ADDR_V4MAPPED(a6))
			inet_ntop(AF_INET, &a6->s6_addr[12], c->peer, sizeof c->peer);   /* ("::ffff:192.168.1.20": IPv4's, as it is) */
		else
			inet_ntop(AF_INET6, a6, c->peer, sizeof c->peer);
	}
	s = servers[sid - 1];
	s->conns = realloc(s->conns, sizeof *s->conns * (size_t)(s->nconns + 1));
	s->conns[s->nconns++] = id;
	c->watch = cby_events_watch(fd, readable, (void *)(intptr_t)id);
	if (s->tls) {
		/* (encrypted: on_connect once it's shaken hands) */
		tls_accept_start(s, c);
		return;
	}
	if (s->on_connect.fn) {
		s->from = id;
		rt_invoke_hook(s->on_connect.obj, s->on_connect.fn);
	}
}

/* a socket listening at an address and port: its fd, or -1 (errno says why) */
static int listen_at(int family, int here_only, int port)
{
	int fd = socket(family, SOCK_STREAM, 0);
	if (fd < 0)
		return -1;
	fcntl(fd, F_SETFD, FD_CLOEXEC);
	int yes = 1;
	setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);   /* (a server started again at once) */
	int r;
	if (family == AF_INET6) {
		/* listening anywhere: IPv4 too, on the one socket; on this computer
		 * alone, ::1 is IPv6's only (IPv4's 127.0.0.1 has a socket of its own) */
		int v6only = here_only ? 1 : 0;
		setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &v6only, sizeof v6only);
		struct sockaddr_in6 a = { 0 };
		a.sin6_family = AF_INET6;
		a.sin6_port = htons((uint16_t)port);
		a.sin6_addr = here_only ? in6addr_loopback : in6addr_any;
		r = bind(fd, (struct sockaddr *)&a, sizeof a);
	} else {
		struct sockaddr_in a = { 0 };
		a.sin_family = AF_INET;
		a.sin_port = htons((uint16_t)port);
		a.sin_addr.s_addr = htonl(here_only ? INADDR_LOOPBACK : INADDR_ANY);
		r = bind(fd, (struct sockaddr *)&a, sizeof a);
	}
	if (r != 0 || listen(fd, 64) != 0) {
		int e = errno;
		close(fd);
		errno = e;
		return -1;
	}
	return fd;
}

static int port_of(int fd)
{
	struct sockaddr_storage at;
	socklen_t len = sizeof at;
	getsockname(fd, (struct sockaddr *)&at, &len);
	return at.ss_family == AF_INET6 ? ntohs(((struct sockaddr_in6 *)&at)->sin6_port)
					: ntohs(((struct sockaddr_in *)&at)->sin_port);
}

/* A server listening on a port (0: any free one), for any computer that
 * can reach this one, or (here_only) for programs on this computer alone:
 * its number, or 0 (cby_conn_error says why). On this computer alone, it
 * listens at both of its addresses, IPv4's 127.0.0.1 and IPv6's ::1, since
 * "localhost" may be either. */
int64_t cby_server_listen(int64_t port, int here_only)
{
	int fd, fd2 = -1;
	if (here_only) {
		fd = listen_at(AF_INET, 1, (int)port);
		if (fd >= 0)
			fd2 = listen_at(AF_INET6, 1, port_of(fd));   /* (none, without IPv6: fine) */
	} else {
		fd = listen_at(AF_INET6, 0, (int)port);
		if (fd < 0 && errno != EADDRINUSE && errno != EACCES)
			fd = listen_at(AF_INET, 0, (int)port);   /* (no IPv6 here) */
	}
	if (fd < 0) {
		char p[32];
		snprintf(p, sizeof p, "%lld", (long long)port);
		say_error("can't listen on port %s: %s", p,
			  errno == EADDRINUSE ? "another program is listening on it" :
			  errno == EACCES ? "ports below 1024 need the administrator (root)" : strerror(errno));
		return 0;
	}
	Server *s = calloc(1, sizeof *s);
	s->fd = fd;
	s->fd2 = fd2;
	s->port = port_of(fd);
	pthread_mutex_lock(&slots);
	int64_t i = 0;
	while (i < nservers && servers[i])
		i++;
	if (i == SERVER_MOST) {
		pthread_mutex_unlock(&slots);
		rt_panic(NULL, 0, 0, "too many servers at once (%d)", SERVER_MOST);
	}
	servers[i] = s;
	if (i == nservers)
		nservers++;
	pthread_mutex_unlock(&slots);
	s->watch = cby_events_watch(fd, accept_one, (void *)(intptr_t)((i + 1) * 2));
	if (fd2 >= 0)
		s->watch2 = cby_events_watch(fd2, accept_one, (void *)(intptr_t)((i + 1) * 2 + 1));
	return i + 1;
}

int64_t cby_server_port(int64_t id) { return server(id)->port; }
int64_t cby_server_from(int64_t id) { return server(id)->from; }
int64_t cby_server_count(int64_t id) { return server(id)->nconns; }
int64_t cby_server_connection(int64_t id, int64_t i)
{
	Server *s = server(id);
	return i >= 0 && i < s->nconns ? s->conns[i] : 0;
}

/* 0 on_connect, 1 on_line, 2 on_close */
void cby_server_on(int64_t id, int64_t which, void *obj, void *fn)
{
	Server *s = server(id);
	Hook h = { obj, fn };
	if (which == 0)
		s->on_connect = h;
	else if (which == 1)
		s->on_line = h;
	else
		s->on_close = h;
}

void cby_server_close(int64_t id)
{
	if (id < 1 || id > nservers || !servers[id - 1])
		return;
	Server *s = servers[id - 1];
	if (s->watch)
		cby_events_unwatch(s->watch);
	if (s->watch2)
		cby_events_unwatch(s->watch2);
	close(s->fd);
	if (s->fd2 >= 0)
		close(s->fd2);
	for (int i = 0; i < s->nconns; i++) {
		int64_t cid = s->conns[i];
		if (cid >= 1 && cid <= nconns && conns[cid - 1]) {
			conns[cid - 1]->server = 0;   /* (closed from here: nothing more to tell) */
			cby_conn_free(cid);
		}
	}
	free(s->conns);
	web_forget(s);
	tls_server_free(s);
	free(s);
	servers[id - 1] = NULL;
}

/* (text built up a piece at a time: imap.h's) */
typedef struct {
	char *d;
	size_t len, cap;
} Buf;

static void put_n(Buf *b, const char *s, size_t n)
{
	if (b->len + n + 1 > b->cap) {
		b->cap = (b->len + n + 1) * 2;
		b->d = realloc(b->d, b->cap);
	}
	memcpy(b->d + b->len, s, n);
	b->len += n;
	b->d[b->len] = 0;
}

static void put(Buf *b, const char *s) { put_n(b, s, strlen(s)); }

/* whether word is in s before end, whatever the letters' case */
static const char *strcasestr_portable(const char *s, const char *word, const char *end)
{
	size_t n = strlen(word);
	for (; *s && (!end || s < end); s++)
		if (strncasecmp(s, word, n) == 0)
			return s;
	return NULL;
}

#include "web.h"
#include "websockets.h"
#include "tls.h"
#include "imap.h"

__attribute__((destructor)) static void connections_end(void)
{
	for (int64_t i = 0; i < nservers; i++)
		cby_server_close(i + 1);
	nservers = 0;
	for (int64_t i = 0; i < nconns; i++)
		cby_conn_free(i + 1);
	nconns = 0;
	free(error_text);
	error_text = NULL;   /* (objects of the program's dropped after this find none) */
	tls_forget();
	imaps_end();
}
