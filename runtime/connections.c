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
#include <ctype.h>
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
#include <sys/wait.h>
#include <spawn.h>
#include <time.h>
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

/* ------------------------------------------------------------ asking who's there
 *
 * Which of many computers have something listening on a port, all asked at
 * once (a connection started to each, and the time limit for them all,
 * rather than each in turn); and a ping, an ICMP echo, as the ping command
 * sends: by a socket of the program's own where the system lets anyone
 * (a Mac does, and Linux when net.ipv4.ping_group_range takes in the
 * program's group, as systemd sets it), or else by the ping command. */

static _Thread_local char *answered_text;

/* (at a thread's end, and the program's) */
static void answered_end(void)
{
	free(answered_text);
	answered_text = NULL;
}
__attribute__((constructor)) static void answered_threads(void) { rt_on_thread_end(answered_end); }

static int64_t clock_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* hosts, each after a "\x1f": those that answered, one a line */
int64_t cby_net_answering(const char *hosts, int64_t port, int64_t ms)
{
	char ports[16];
	snprintf(ports, sizeof ports, "%lld", (long long)port);
	int cap = 16, n = 0;
	struct pollfd *p = malloc(sizeof *p * (size_t)cap);
	char **who = malloc(sizeof *who * (size_t)cap);
	char *list = strdup(hosts);
	bool was = rt_gil_drop();
	for (char *h = list; h && *h;) {
		char *sep = strchr(h, '\x1f');
		if (sep)
			*sep = 0;
		struct addrinfo hints = { 0 }, *found = NULL;
		hints.ai_family = AF_UNSPEC;
		hints.ai_socktype = SOCK_STREAM;
		hints.ai_flags = AI_NUMERICSERV;
		if (getaddrinfo(h, ports, &hints, &found) == 0 && found) {
			int fd = socket(found->ai_family, found->ai_socktype, found->ai_protocol);
			if (fd >= 0) {
				fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
				fcntl(fd, F_SETFD, FD_CLOEXEC);
				int r = connect(fd, found->ai_addr, found->ai_addrlen);
				if (r == 0 || errno == EINPROGRESS) {
					if (n == cap) {
						cap *= 2;
						p = realloc(p, sizeof *p * (size_t)cap);
						who = realloc(who, sizeof *who * (size_t)cap);
					}
					p[n] = (struct pollfd){ fd, POLLOUT, 0 };
					who[n++] = h;
				} else {
					close(fd);
				}
			}
			freeaddrinfo(found);
		}
		h = sep ? sep + 1 : NULL;
	}
	/* each answers, or is refused, or the time's up */
	bool *yes = calloc((size_t)(n ? n : 1), 1);
	int64_t end_at = clock_ms() + (ms > 0 ? ms : 0);
	int left = n;
	while (left > 0) {
		int64_t wait = end_at - clock_ms();
		if (wait <= 0)
			break;
		int r = poll(p, (nfds_t)n, (int)wait);
		if (r < 0 && errno == EINTR)
			continue;
		if (r <= 0)
			break;
		for (int i = 0; i < n; i++)
			if (p[i].fd >= 0 && p[i].revents) {
				int err = 0;
				socklen_t el = sizeof err;
				getsockopt(p[i].fd, SOL_SOCKET, SO_ERROR, &err, &el);
				yes[i] = err == 0 && (p[i].revents & POLLOUT);
				close(p[i].fd);
				p[i].fd = -1;   /* (poll leaves it be) */
				left--;
			}
	}
	size_t len = 0;
	for (int i = 0; i < n; i++)
		len += strlen(who[i]) + 1;
	free(answered_text);
	answered_text = malloc(len + 1);
	size_t at = 0;
	for (int i = 0; i < n; i++) {
		if (p[i].fd >= 0)
			close(p[i].fd);
		if (yes[i]) {
			size_t l = strlen(who[i]);
			memcpy(answered_text + at, who[i], l);
			at += l;
			answered_text[at++] = '\n';
		}
	}
	answered_text[at] = 0;
	rt_gil_take(was);
	free(yes);
	free(p);
	free(who);
	free(list);
	return give(answered_text, at);
}

static uint16_t icmp_sum(const unsigned char *b, size_t n)
{
	uint32_t sum = 0;
	for (size_t i = 0; i + 1 < n; i += 2)
		sum += (uint32_t)(b[i] << 8 | b[i + 1]);
	if (n & 1)
		sum += (uint32_t)(b[n - 1] << 8);
	while (sum >> 16)
		sum = (sum & 0xFFFF) + (sum >> 16);
	return (uint16_t)~sum;
}

/* by a socket of the program's own: the round trip in microseconds, -1 no
 * answer in time, -2 the system doesn't let the program ping this way */
static int64_t ping_socket(const struct sockaddr_in *to, int64_t ms)
{
	int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_ICMP);
	if (fd < 0)
		return -2;
	fcntl(fd, F_SETFD, FD_CLOEXEC);
	static _Atomic unsigned short seq;
	unsigned short mine = (unsigned short)(++seq);
	unsigned char out[16] = { 8, 0, 0, 0, 0x43, 0x42, (unsigned char)(mine >> 8), (unsigned char)mine,
				  'c', 'a', 'b', 'b', 'y', 0, 0, 0 };
	uint16_t sum = icmp_sum(out, sizeof out);
	out[2] = (unsigned char)(sum >> 8);
	out[3] = (unsigned char)sum;
	struct timespec t0, t1;
	clock_gettime(CLOCK_MONOTONIC, &t0);
	if (sendto(fd, out, sizeof out, 0, (const struct sockaddr *)to, sizeof *to) < 0) {
		close(fd);
		return errno == EACCES || errno == EPERM ? -2 : -1;
	}
	int64_t end_at = clock_ms() + ms;
	int64_t got = -1;
	for (;;) {
		int64_t wait = end_at - clock_ms();
		if (wait <= 0)
			break;
		struct pollfd p = { fd, POLLIN, 0 };
		int r = poll(&p, 1, (int)wait);
		if (r < 0 && errno == EINTR)
			continue;
		if (r <= 0)
			break;
		unsigned char in[256];
		ssize_t k = recv(fd, in, sizeof in, 0);
		if (k <= 0)
			continue;
		size_t off = 0;
		if ((in[0] >> 4) == 4)             /* a Mac gives the IP header too */
			off = (size_t)(in[0] & 0x0F) * 4;
		if ((size_t)k < off + 8 || in[off] != 0)   /* 0: an echo reply */
			continue;
		/* (Linux sets the id itself; the sequence number is ours) */
		if (in[off + 6] != (unsigned char)(mine >> 8) || in[off + 7] != (unsigned char)mine)
			continue;
		clock_gettime(CLOCK_MONOTONIC, &t1);
		got = (int64_t)(t1.tv_sec - t0.tv_sec) * 1000000 + (t1.tv_nsec - t0.tv_nsec) / 1000;
		break;
	}
	close(fd);
	return got;
}

/* by the ping command: "time=0.045 ms" */
static int64_t ping_command(const char *host, int64_t ms)
{
	for (const char *c = host; *c; c++)
		if (!(isalnum((unsigned char)*c) || *c == '.' || *c == '-' || *c == ':'))
			return -1;   /* (a name, or an address, and nothing else) */
	char wait[24];
#ifdef __APPLE__
	snprintf(wait, sizeof wait, "%lld", (long long)(ms > 0 ? ms : 1));   /* -W: milliseconds */
#else
	snprintf(wait, sizeof wait, "%lld", (long long)((ms + 999) / 1000 > 0 ? (ms + 999) / 1000 : 1));
#endif
	const char *dir = getenv("CABBY_TOOLS");
	char prog[600];
	snprintf(prog, sizeof prog, "%s%sping", dir && *dir ? dir : "", dir && *dir ? "/" : "");
	const char *argv[] = { prog, "-n", "-c", "1", "-W", wait, host, NULL };
	int fds[2];
	if (pipe(fds) != 0)
		return -1;
	posix_spawn_file_actions_t acts;
	posix_spawn_file_actions_init(&acts);
	posix_spawn_file_actions_adddup2(&acts, fds[1], 1);
	posix_spawn_file_actions_addopen(&acts, 2, "/dev/null", O_WRONLY, 0);
	posix_spawn_file_actions_addclose(&acts, fds[0]);
	posix_spawn_file_actions_addclose(&acts, fds[1]);
	pid_t pid;
	extern char **environ;
	int ok = posix_spawnp(&pid, prog, &acts, NULL, (char *const *)argv, environ);
	posix_spawn_file_actions_destroy(&acts);
	close(fds[1]);
	char buf[2048];
	size_t len = 0;
	ssize_t k;
	while (len + 1 < sizeof buf && (k = read(fds[0], buf + len, sizeof buf - len - 1)) != 0) {
		if (k < 0) {
			if (errno == EINTR)
				continue;
			break;
		}
		len += (size_t)k;
	}
	buf[len] = 0;
	close(fds[0]);
	if (ok == 0)
		while (waitpid(pid, NULL, 0) < 0 && errno == EINTR) {
		}
	const char *t = strstr(buf, "time=");
	if (ok != 0 || !t)
		return -1;
	return (int64_t)(strtod(t + 5, NULL) * 1000 + 0.5);
}

/* (a header's own panic, at the program's line) */
void cby_net_panic(const char *why) { rt_panic(NULL, 0, 0, "%s", why); }

/* the round trip to host, in microseconds, or -1 if it didn't answer in ms */
int64_t cby_net_ping(const char *host, int64_t ms)
{
	bool was = rt_gil_drop();
	struct addrinfo hints = { 0 }, *found = NULL;
	hints.ai_family = AF_INET;
	int64_t got = -1;
	if (getaddrinfo(host, NULL, &hints, &found) == 0 && found) {
		/* (the tests ask a stand-in ping command, from CABBY_TOOLS) */
		got = getenv("CABBY_TOOLS") ? -2 : ping_socket((const struct sockaddr_in *)found->ai_addr, ms);
		if (got == -2)
			got = ping_command(host, ms);
		freeaddrinfo(found);
	}
	rt_gil_take(was);
	return got;
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
	answered_end();
	tls_forget();
	imaps_end();
}
