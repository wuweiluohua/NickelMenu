#ifndef NM_KFMON_H
#define NM_KFMON_H
#ifdef __cplusplus
extern "C" {
#endif

#define _GNU_SOURCE // poll
#include <errno.h>
#include <linux/limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <signal.h>
#include <poll.h>

// Path to our IPC Unix socket
#define KFMON_IPC_SOCKET "/tmp/kfmon-ipc.ctl"

// We want to return negative values on failure, always
#define ERRCODE(e) (-(e))

// read & write wrappers that Do the Right Thing.
ssize_t xread(int fd, void* buf, size_t len);
ssize_t xwrite(int fd, const void* buf, size_t len);
ssize_t read_in_full(int fd, void* buf, size_t count);
ssize_t write_in_full(int fd, const void* buf, size_t count);

int handle_reply(int data_fd);

// Flags for the failure bingo bitmask
#define KFMON_IPC_ETIMEDOUT    (1 << 1)
#define KFMON_IPC_EPIPE        (1 << 2)
#define KFMON_IPC_ENODATA      (1 << 3)
#define KFMON_IPC_READ_FAILURE (1 << 4)

#ifdef __cplusplus
}
#endif
#endif
