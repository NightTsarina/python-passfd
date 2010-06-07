/*
 * vim:ts=4:sw=4:et:ai:sts=4
 */
#define _GNU_SOURCE

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <wait.h>

#include "fdpassing.c"

int test_fd_msg(int fd, int receivedlen, char *received, int expectedlen,
        char *expected) {
    char buf[4096];
    ssize_t n;

    if(fd < 0) {
        fprintf(stderr, "Error receiving file descriptor: %d.\n", fd);
        return 0;
    }
    if(receivedlen != expectedlen) {
        fprintf(stderr, "Message length mismatch. "
                "Expected: %d, received: %d\n", expectedlen, receivedlen);
        goto error;
    }
    if(memcmp(received, expected, expectedlen)) {
        char *cpy = strndup(received, receivedlen);
        fprintf(stderr, "Invalid message received. "
                "Expected: %s, received: %s\n", expected, cpy);
        free(cpy);
        goto error;
    }
    n = read(fd, buf, sizeof(buf));
    if(n < 0) {
        perror("read");
        goto error;
    }
    if(n == 0) {
        fprintf(stderr, "End of file reached on received fd.\n");
        goto error;
    }
    if(n != sizeof(buf)) {
        fprintf(stderr, "Short read (%ld) on received fd.\n", n);
        goto error;
    }
    for(n = 0; n < sizeof(buf) && !buf[n]; n++)
        ;
    if(n < sizeof(buf)) {
        fprintf(stderr, "Invalid fd received: non-null char read\n");
        goto error;
    }
    fprintf(stderr, "OK\n");
    close(fd);
    return 1;
error:
    close(fd);
    return 0;
}
int test_child(int sock) {
    int fd = open("/dev/zero", 0);
    if(fd < 0) {
        perror("open");
        return 0;
    }
    if(sendfd(sock, fd, 0, NULL) != 0) goto error; /* Will not be received */
    if(sendfd(sock, fd, 1, "a") != 1) goto error;
    if(sendfd(sock, fd, 1, "\0") != 1) goto error;
    if(sendfd(sock, fd, 10, "full write") != 10) goto error;
    if(sendfd(sock, fd, 10, "short write") != 10) goto error;
    /* The other side will recv() instead of recvmsg(), this fd would be lost.
     * Couldn't find any specification on this semantic */
    if(sendfd(sock, fd, 8, "mismatch") != 8) goto error;
    if(send(sock, "mismatch", 8, 0) != 8) return 0;
    close(fd);
    return 1;
error:
    perror("sendmsg");
    close(fd);
    return 0;
}
int test_parent(int sock) {
    char buf[4096];
    int fd;
    size_t len;

    len = sizeof(buf); fd = recvfd(sock, &len, buf);
    if(!test_fd_msg(fd, len, buf, 1, "a")) return 0;
    len = sizeof(buf); fd = recvfd(sock, &len, buf);
    if(!test_fd_msg(fd, len, buf, 1, "\0")) return 0;
    len = sizeof(buf); fd = recvfd(sock, &len, buf);
    if(!test_fd_msg(fd, len, buf, 10, "full write")) return 0;
    len = sizeof(buf); fd = recvfd(sock, &len, buf);
    if(!test_fd_msg(fd, len, buf, 10, "short writ")) return 0;

    /* sendmsg/recv mismatch */
    if(recv(sock, buf, sizeof(buf), 0) != 8) return 0;
    len = sizeof(buf); fd = recvfd(sock, &len, buf);
    if(fd != -2) {
        fprintf(stderr, "recvfd should have failed: %d", fd);
        return 0;
    }
    return 1;
}

int main(void) {
    int sv_stream[2], sv_dgram[2];
    int err, status;
    pid_t pid;

    err = socketpair(AF_UNIX, SOCK_STREAM, 0, sv_stream);
    if(err) {
        perror("socketpair (SOCK_STREAM)");
        return 1;
    }
    err = socketpair(AF_UNIX, SOCK_DGRAM, 0, sv_dgram);
    if(err) {
        perror("socketpair (SOCK_DGRAM)");
        return 1;
    }
    pid = fork();
    if(pid == 0) {
        close(sv_stream[1]);
        close(sv_dgram[1]);
        err = test_child(sv_stream[0]);
        close(sv_stream[0]);
        err = test_child(sv_dgram[0]) && err;
        close(sv_dgram[0]);
        return ! err;
    }
    if(pid > 0) {
        close(sv_stream[0]);
        close(sv_dgram[0]);
        err = test_parent(sv_stream[1]);
        close(sv_stream[1]);
        err = test_parent(sv_dgram[1]) && err;
        close(sv_dgram[1]);
        waitpid(pid, &status, 0);
        err = err && ! status;
        return ! err;
    }
    perror("fork");
    return 1;
}

