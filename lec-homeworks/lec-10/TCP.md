Compile on Linux/macOS (POSIX):

gcc -Wall -Wextra -O2 server.c -o server -pthread
gcc -Wall -Wextra -O2 client.c -o client

Run:

# terminal 1

./server 5555

# terminal 2 (one or many)

./client 127.0.0.1 5555

# type lines, they’ll be echoed back; Ctrl+D to end input

---

server.c — concurrent echo server (IPv4/IPv6, reusable port)

// server.c
// A simple concurrent TCP echo server using pthreads.
// - Binds to the given port on all interfaces (IPv4/IPv6).
// - Spawns one detached thread per client.
// - Echoes back whatever each client sends.
// Build: gcc -Wall -Wextra -O2 server.c -o server -pthread

#define \_POSIX_C_SOURCE 200112L
#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

static volatile sig_atomic_t g_stop = 0;

static void on_sigint(int sig) {
(void)sig;
g_stop = 1; // tell the accept loop to stop
}

// Small helper to print peer address (IP:port)
static void addr_to_string(const struct sockaddr *sa, char *out, size_t outlen) {
char host[NI_MAXHOST], serv[NI_MAXSERV];
int rc = getnameinfo(sa, (sa->sa_family == AF_INET) ? sizeof(struct sockaddr_in)
: sizeof(struct sockaddr_in6),
host, sizeof(host), serv, sizeof(serv),
NI_NUMERICHOST | NI_NUMERICSERV);
if (rc == 0)
snprintf(out, outlen, "%s:%s", host, serv);
else
snprintf(out, outlen, "unknown");
}

struct client_ctx {
int fd;
struct sockaddr_storage addr;
socklen_t addrlen;
};

static void *client_thread(void *arg) {
struct client_ctx _ctx = (struct client_ctx _)arg;
int fd = ctx->fd;

    // Log connection
    char peer[128];
    addr_to_string((struct sockaddr *)&ctx->addr, peer, sizeof(peer));
    fprintf(stderr, "[+] client connected: %s\n", peer);

    free(ctx); // no longer needed in this thread

    // Echo loop
    char buf[4096];
    for (;;) {
        ssize_t n = recv(fd, buf, sizeof(buf), 0);
        if (n == 0) {
            // orderly shutdown from client
            break;
        } else if (n < 0) {
            if (errno == EINTR) continue;
            perror("recv");
            break;
        }
        // Echo back the same bytes
        ssize_t sent = 0;
        while (sent < n) {
            ssize_t m = send(fd, buf + sent, (size_t)(n - sent), 0);
            if (m < 0) {
                if (errno == EINTR) continue;
                perror("send");
                close(fd);
                pthread_exit(NULL);
            }
            sent += m;
        }
    }

    fprintf(stderr, "[-] client disconnected: %s\n", peer);
    close(fd);
    pthread_exit(NULL);

}

int main(int argc, char \**argv) {
if (argc != 2) {
fprintf(stderr, "usage: %s <port>\n", argv[0]);
return EXIT_FAILURE;
}
const char *port = argv[1];

    // Handle Ctrl+C gracefully
    struct sigaction sa = {0};
    sa.sa_handler = on_sigint;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    signal(SIGPIPE, SIG_IGN); // avoid crashes if a client disappears

    // Prepare passive address(es)
    struct addrinfo hints, *res = NULL, *rp = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC; // IPv4 or IPv6
    hints.ai_socktype = SOCK_STREAM; // TCP
    hints.ai_flags = AI_PASSIVE; // for binding

    int rc = getaddrinfo(NULL, port, &hints, &res);
    if (rc != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rc));
        return EXIT_FAILURE;
    }

    int listen_fd = -1;

    // Try each candidate until one binds
    for (rp = res; rp != NULL; rp = rp->ai_next) {
        listen_fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (listen_fd < 0) continue;

        int yes = 1;
        setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

#ifdef IPV6_V6ONLY
if (rp->ai_family == AF_INET6) {
int v6only = 0; // allow IPv4-mapped connections too
setsockopt(listen_fd, IPPROTO_IPV6, IPV6_V6ONLY, &v6only, sizeof(v6only));
}
#endif
if (bind(listen_fd, rp->ai_addr, rp->ai_addrlen) == 0) {
// success
break;
}
close(listen_fd);
listen_fd = -1;
}
freeaddrinfo(res);

    if (listen_fd < 0) {
        perror("bind");
        return EXIT_FAILURE;
    }

    if (listen(listen_fd, SOMAXCONN) != 0) {
        perror("listen");
        close(listen_fd);
        return EXIT_FAILURE;
    }

    fprintf(stderr, "[*] listening on port %s ... (Ctrl+C to stop)\n", port);

    // Accept loop
    while (!g_stop) {
        struct client_ctx *ctx = malloc(sizeof(*ctx));
        if (!ctx) {
            perror("malloc");
            break;
        }
        ctx->addrlen = sizeof(ctx->addr);
        ctx->fd = accept(listen_fd, (struct sockaddr *)&ctx->addr, &ctx->addrlen);
        if (ctx->fd < 0) {
            free(ctx);
            if (errno == EINTR) continue; // probably Ctrl+C
            perror("accept");
            break;
        }

        pthread_t tid;
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        int prc = pthread_create(&tid, &attr, client_thread, ctx);
        pthread_attr_destroy(&attr);
        if (prc != 0) {
            fprintf(stderr, "pthread_create: %s\n", strerror(prc));
            close(ctx->fd);
            free(ctx);
        }
    }

    fprintf(stderr, "[*] shutting down listener\n");
    close(listen_fd);
    return EXIT_SUCCESS;

}

---

client.c — interactive client (stdin/socket via select())

// client.c
// A simple interactive TCP client.
// - Connects to host:port (IPv4/IPv6).
// - Uses select() to forward stdin to the socket and print server replies.
// - On EOF (Ctrl+D), half-closes (shutdown write) to signal end of input,
// but keeps reading echoes until the server closes.
// Build: gcc -Wall -Wextra -O2 client.c -o client

#define \_POSIX_C_SOURCE 200112L
#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

static int connect_to(const char *host, const char *port) {
struct addrinfo hints, *res = NULL, *rp = NULL;
memset(&hints, 0, sizeof(hints));
hints.ai_family = AF_UNSPEC; // IPv4 or IPv6
hints.ai_socktype = SOCK_STREAM; // TCP

    int rc = getaddrinfo(host, port, &hints, &res);
    if (rc != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rc));
        return -1;
    }

    int fd = -1;
    for (rp = res; rp != NULL; rp = rp->ai_next) {
        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd < 0) continue;
        if (connect(fd, rp->ai_addr, rp->ai_addrlen) == 0) {
            break; // success
        }
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    return fd; // -1 if all attempts failed

}

int main(int argc, char \**argv) {
if (argc != 3) {
fprintf(stderr, "usage: %s <host> <port>\n", argv[0]);
return EXIT_FAILURE;
}
const char *host = argv[1];
const char \*port = argv[2];

    int fd = connect_to(host, port);
    if (fd < 0) {
        perror("connect");
        return EXIT_FAILURE;
    }

    fprintf(stderr, "[*] connected to %s:%s\n", host, port);
    // Make stdout line-buffered for nicer interactivity
    setvbuf(stdout, NULL, _IOLBF, 0);

    bool stdin_open = true;
    char inbuf[4096];
    char netbuf[4096];

    for (;;) {
        fd_set rfds;
        FD_ZERO(&rfds);
        int maxfd = fd;
        FD_SET(fd, &rfds);
        if (stdin_open) {
            FD_SET(STDIN_FILENO, &rfds);
            if (STDIN_FILENO > maxfd) maxfd = STDIN_FILENO;
        }

        int rc = select(maxfd + 1, &rfds, NULL, NULL, NULL);
        if (rc < 0) {
            if (errno == EINTR) continue;
            perror("select");
            break;
        }

        // Data from server?
        if (FD_ISSET(fd, &rfds)) {
            ssize_t n = recv(fd, netbuf, sizeof(netbuf), 0);
            if (n == 0) {
                fprintf(stderr, "[-] server closed connection\n");
                break;
            } else if (n < 0) {
                if (errno == EINTR) continue;
                perror("recv");
                break;
            } else {
                // Write exactly what we received to stdout
                size_t off = 0;
                while (off < (size_t)n) {
                    ssize_t m = write(STDOUT_FILENO, netbuf + off, n - off);
                    if (m < 0) {
                        if (errno == EINTR) continue;
                        perror("write");
                        goto done;
                    }
                    off += (size_t)m;
                }
            }
        }

        // Data from stdin?
        if (stdin_open && FD_ISSET(STDIN_FILENO, &rfds)) {
            ssize_t n = read(STDIN_FILENO, inbuf, sizeof(inbuf));
            if (n == 0) {
                // EOF from user: half-close write side, keep reading server
                shutdown(fd, SHUT_WR);
                stdin_open = false;
            } else if (n < 0) {
                if (errno == EINTR) continue;
                perror("read");
                break;
            } else {
                // Send to server
                size_t off = 0;
                while (off < (size_t)n) {
                    ssize_t m = send(fd, inbuf + off, n - off, 0);
                    if (m < 0) {
                        if (errno == EINTR) continue;
                        perror("send");
                        goto done;
                    }
                    off += (size_t)m;
                }
            }
        }
    }

done:
close(fd);
return EXIT_SUCCESS;
}

---

Notes & tips

The server sets SO_REUSEADDR and permits IPv6 + IPv4-mapped traffic when available.

The server ignores SIGPIPE so a vanished client won’t crash it; send() errors are handled instead.

The client half-closes (via shutdown(fd, SHUT_WR)) after stdin EOF so the server sees end-of-input but can still send remaining data.

You can easily swap the echo logic for your own protocol inside client_thread().
