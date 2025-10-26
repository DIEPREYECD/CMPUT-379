// server.c
// Concurrent TCP echo server using forked child processes (no threads).
// Usage: ./server <port>
// Example: ./server 5000

#define _POSIX_C_SOURCE 200809L
#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define BACKLOG 128
#define BUFSZ 4096

static void die(const char *msg)
{
    perror(msg);
    exit(EXIT_FAILURE);
}

// Reap all dead children to avoid zombies.
static void sigchld_handler(int signo)
{
    (void)signo;
    // Use non-blocking waitpid in a loop.
    while (waitpid(-1, NULL, WNOHANG) > 0)
    { /* reap */
    }
}

// Read a line (ending in '\n') from fd into buf (up to bufsz-1 chars).
// Returns number of bytes in buf (>=0), 0 on EOF, or -1 on error.
static ssize_t readline(int fd, char *buf, size_t bufsz)
{
    size_t i = 0;
    while (i + 1 < bufsz)
    {
        char c;
        ssize_t n = read(fd, &c, 1);
        if (n == 0)
        { // EOF
            if (i == 0)
                return 0; // no data read
            break;        // return partial line
        }
        if (n < 0)
        {
            if (errno == EINTR)
                continue;
            return -1;
        }
        buf[i++] = c;
        if (c == '\n')
            break;
    }
    buf[i] = '\0';
    return (ssize_t)i;
}

static void to_upper(char *s)
{
    for (; *s; ++s)
        *s = (char)toupper((unsigned char)*s);
}

static void handle_client(int connfd, struct sockaddr_in *peer)
{
    char addr[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &peer->sin_addr, addr, sizeof(addr));
    int p = ntohs(peer->sin_port);
    fprintf(stderr, "[child %ld] connected: %s:%d\n", (long)getpid(), addr, p);

    char line[BUFSZ];
    while (1)
    {
        ssize_t n = readline(connfd, line, sizeof(line));
        if (n == 0)
        { // client closed
            break;
        }
        else if (n < 0)
        {
            if (errno == EINTR)
                continue;
            perror("readline");
            break;
        }
        // Transform to uppercase and echo back.
        to_upper(line);
        size_t to_write = strlen(line);
        size_t off = 0;
        while (off < to_write)
        {
            ssize_t m = write(connfd, line + off, to_write - off);
            if (m < 0)
            {
                if (errno == EINTR)
                    continue;
                perror("write");
                goto done;
            }
            off += (size_t)m;
        }
    }

done:
    fprintf(stderr, "[child %ld] disconnected: %s:%d\n", (long)getpid(), addr, p);
    close(connfd);
}

int main(int argc, char **argv)
{
    if (argc != 2)
    {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        return EXIT_FAILURE;
    }
    int port = atoi(argv[1]);
    if (port <= 0 || port > 65535)
    {
        fprintf(stderr, "Invalid port.\n");
        return EXIT_FAILURE;
    }

    // Ignore SIGPIPE so unexpected client closes don't kill us.
    signal(SIGPIPE, SIG_IGN);

    // Reap children.
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigchld_handler;
    sa.sa_flags = SA_RESTART; // restart accept() after handler
    if (sigaction(SIGCHLD, &sa, NULL) == -1)
        die("sigaction");

    int listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenfd < 0)
        die("socket");

    // Allow fast restarts.
    int yes = 1;
    if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) < 0)
        die("setsockopt(SO_REUSEADDR)");

    struct sockaddr_in srv;
    memset(&srv, 0, sizeof(srv));
    srv.sin_family = AF_INET;
    srv.sin_addr.s_addr = htonl(INADDR_ANY);
    srv.sin_port = htons((uint16_t)port);

    if (bind(listenfd, (struct sockaddr *)&srv, sizeof(srv)) < 0)
        die("bind");
    if (listen(listenfd, BACKLOG) < 0)
        die("listen");

    fprintf(stderr, "Server listening on port %d ...\n", port);

    // Accept loop: fork a child per connection.
    for (;;)
    {
        struct sockaddr_in peer;
        socklen_t plen = sizeof(peer);
        int connfd = accept(listenfd, (struct sockaddr *)&peer, &plen);
        if (connfd < 0)
        {
            if (errno == EINTR)
                continue; // interrupted by signal
            die("accept");
        }

        pid_t pid = fork();
        if (pid < 0)
        {
            perror("fork");
            close(connfd);
            continue;
        }
        else if (pid == 0)
        {
            // Child process: close listener, handle the client.
            close(listenfd);
            handle_client(connfd, &peer);
            _exit(0);
        }
        else
        {
            // Parent: close connected socket, go back to accept().
            close(connfd);
        }
    }
}
