// client.c
// TCP client that uses fork(): child copies stdin->socket; parent copies socket->stdout.
// Usage: ./client <server_ip> <port>
// Example: ./client 127.0.0.1 5000
// Type lines and press Enter; server will echo them back in uppercase.
// Ctrl+D (EOF) to close the write side; client exits when server closes.

#define _POSIX_C_SOURCE 200809L
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define BUFSZ 4096

static void die(const char *msg)
{
    perror(msg);
    exit(EXIT_FAILURE);
}

static void copy_stream(int in_fd, int out_fd)
{
    char buf[BUFSZ];
    for (;;)
    {
        ssize_t n = read(in_fd, buf, sizeof(buf));
        if (n == 0)
            break; // EOF
        if (n < 0)
        {
            if (errno == EINTR)
                continue;
            die("read");
        }
        size_t off = 0;
        while (off < (size_t)n)
        {
            ssize_t m = write(out_fd, buf + off, (size_t)n - off);
            if (m < 0)
            {
                if (errno == EINTR)
                    continue;
                die("write");
            }
            off += (size_t)m;
        }
    }
}

int main(int argc, char **argv)
{
    if (argc != 3)
    {
        fprintf(stderr, "Usage: %s <server_ip> <port>\n", argv[0]);
        return EXIT_FAILURE;
    }
    const char *ip = argv[1];
    int port = atoi(argv[2]);
    if (port <= 0 || port > 65535)
    {
        fprintf(stderr, "Invalid port.\n");
        return EXIT_FAILURE;
    }

    // Avoid being killed by SIGPIPE if server closes early.
    signal(SIGPIPE, SIG_IGN);

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0)
        die("socket");

    struct sockaddr_in srv;
    memset(&srv, 0, sizeof(srv));
    srv.sin_family = AF_INET;
    srv.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, ip, &srv.sin_addr) != 1)
    {
        fprintf(stderr, "Invalid IP: %s\n", ip);
        return EXIT_FAILURE;
    }

    if (connect(sock, (struct sockaddr *)&srv, sizeof(srv)) < 0)
        die("connect");

    fprintf(stderr, "Connected to %s:%d\n", ip, port);

    pid_t pid = fork();
    if (pid < 0)
        die("fork");

    if (pid == 0)
    {
        // Child: stdin -> socket. After EOF on stdin, half-close the socket for writing.
        copy_stream(STDIN_FILENO, sock);
        shutdown(sock, SHUT_WR); // signal EOF to server (keep reading replies)
        _exit(0);
    }
    else
    {
        // Parent: socket -> stdout. Exit when server closes.
        copy_stream(sock, STDOUT_FILENO);
        // If we get here, server closed. Kill child if still running.
        // (Not strictly necessary; child likely already exited after shutdown.)
        kill(pid, SIGTERM);
        close(sock);
    }

    return 0;
}
