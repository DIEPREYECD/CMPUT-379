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

static int connect_to(const char *host, const char *port)
{
    struct addrinfo hints, *res = NULL, *rp = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;     // IPv4 or IPv6
    hints.ai_socktype = SOCK_STREAM; // TCP

    int rc = getaddrinfo(host, port, &hints, &res);
    if (rc != 0)
    {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rc));
        return -1;
    }

    int fd = -1;
    for (rp = res; rp != NULL; rp = rp->ai_next)
    {
        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd < 0)
            continue;
        if (connect(fd, rp->ai_addr, rp->ai_addrlen) == 0)
        {
            break; // success
        }
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    return fd; // -1 if all attempts failed
}

int main(int argc, char \**argv)
{
    if (argc != 3)
    {
        fprintf(stderr, "usage: %s <host> <port>\n", argv[0]);
        return EXIT_FAILURE;
    }
    const char *host = argv[1];
    const char \*port = argv[2];

    int fd = connect_to(host, port);
    if (fd < 0)
    {
        perror("connect");
        return EXIT_FAILURE;
    }

    fprintf(stderr, "[*] connected to %s:%s\n", host, port);
    // Make stdout line-buffered for nicer interactivity
    setvbuf(stdout, NULL, _IOLBF, 0);

    bool stdin_open = true;
    char inbuf[4096];
    char netbuf[4096];

    for (;;)
    {
        fd_set rfds;
        FD_ZERO(&rfds);
        int maxfd = fd;
        FD_SET(fd, &rfds);
        if (stdin_open)
        {
            FD_SET(STDIN_FILENO, &rfds);
            if (STDIN_FILENO > maxfd)
                maxfd = STDIN_FILENO;
        }

        int rc = select(maxfd + 1, &rfds, NULL, NULL, NULL);
        if (rc < 0)
        {
            if (errno == EINTR)
                continue;
            perror("select");
            break;
        }

        // Data from server?
        if (FD_ISSET(fd, &rfds))
        {
            ssize_t n = recv(fd, netbuf, sizeof(netbuf), 0);
            if (n == 0)
            {
                fprintf(stderr, "[-] server closed connection\n");
                break;
            }
            else if (n < 0)
            {
                if (errno == EINTR)
                    continue;
                perror("recv");
                break;
            }
            else
            {
                // Write exactly what we received to stdout
                size_t off = 0;
                while (off < (size_t)n)
                {
                    ssize_t m = write(STDOUT_FILENO, netbuf + off, n - off);
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
        }

        // Data from stdin?
        if (stdin_open && FD_ISSET(STDIN_FILENO, &rfds))
        {
            ssize_t n = read(STDIN_FILENO, inbuf, sizeof(inbuf));
            if (n == 0)
            {
                // EOF from user: half-close write side, keep reading server
                shutdown(fd, SHUT_WR);
                stdin_open = false;
            }
            else if (n < 0)
            {
                if (errno == EINTR)
                    continue;
                perror("read");
                break;
            }
            else
            {
                // Send to server
                size_t off = 0;
                while (off < (size_t)n)
                {
                    ssize_t m = send(fd, inbuf + off, n - off, 0);
                    if (m < 0)
                    {
                        if (errno == EINTR)
                            continue;
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