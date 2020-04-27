#ifndef NM_KFMON_PRIV_H
#define NM_KFMON_PRIV_H
#ifdef __cplusplus
extern "C" {
#endif

#define _GNU_SOURCE
#include <errno.h>
#include <linux/limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <poll.h>

// Simple read/write wrappers with retries on recoverable errors
static ssize_t xread(int fd, void* buf, size_t len);
//static ssize_t xwrite(int fd, const void* buf, size_t len);
// Ensure all of data on socket comes through.
//static ssize_t read_in_full(int fd, void* buf, size_t len);
//static ssize_t write_in_full(int fd, const void* buf, size_t len);
// This sets MSG_NOSIGNAL, so you *must* handle EPIPE!
static ssize_t send_in_full(int sockfd, const void* buf, size_t len);
#include "helpers.h"

#ifdef __cplusplus
}
#endif
#endif
