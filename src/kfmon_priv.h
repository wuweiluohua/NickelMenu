#ifndef NM_KFMON_PRIV_H
#define NM_KFMON_PRIV_H
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

// read & write wrappers that Do the Right Thing.
static ssize_t xread(int fd, void* buf, size_t len);
static ssize_t xwrite(int fd, const void* buf, size_t len);
//static ssize_t read_in_full(int fd, void* buf, size_t count);
static ssize_t write_in_full(int fd, const void* buf, size_t count);
#include "helpers.h"

#ifdef __cplusplus
}
#endif
#endif
