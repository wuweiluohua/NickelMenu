#include "kfmon.h"

// Any and all resemblance to the git wrappers would be purely fortuitous... ^^
// c.f., https://github.com/git/git/blob/master/wrapper.c

/*
 * Limit size of IO chunks, because huge chunks only cause pain.  OS X
 * 64-bit is buggy, returning EINVAL if len >= INT_MAX; and even in
 * the absence of bugs, large chunks can result in bad latencies when
 * you decide to kill the process.
 *
 * We pick 8 MiB as our default, but if the platform defines SSIZE_MAX
 * that is smaller than that, clip it to SSIZE_MAX, as a call to
 * read(2) or write(2) larger than that is allowed to fail.  As the last
 * resort, we allow a port to pass via CFLAGS e.g. "-DMAX_IO_SIZE=value"
 * to override this, if the definition of SSIZE_MAX given by the platform
 * is broken.
 */
#ifndef MAX_IO_SIZE
#   define MAX_IO_SIZE_DEFAULT (8 * 1024 * 1024)
#   if defined(SSIZE_MAX) && (SSIZE_MAX < MAX_IO_SIZE_DEFAULT)
#      define MAX_IO_SIZE SSIZE_MAX
#   else
#      define MAX_IO_SIZE MAX_IO_SIZE_DEFAULT
#   endif
#endif

static int handle_nonblock(int fd, short poll_events, int err) {
    struct pollfd pfd;

    // NOTE: EWOULDBLOCK is defined as EAGAIN on Linux, no need to check both.
    if (err != EAGAIN)
        return 0;

    pfd.fd     = fd;
    pfd.events = poll_events;

    /*
    * no need to check for errors, here;
    * a subsequent read/write will detect unrecoverable errors
    */
    poll(&pfd, 1, -1);
    return 1;
}

/*
 * xread() is the same a read(), but it automatically restarts read()
 * operations with a recoverable error (EAGAIN and EINTR). xread()
 * DOES NOT GUARANTEE that "len" bytes is read even if the data is available.
 */
ssize_t xread(int fd, void* buf, size_t len) {
    ssize_t nr;
    if (len > MAX_IO_SIZE)
        len = MAX_IO_SIZE;
    while (1) {
        nr = read(fd, buf, len);
        if (nr < 0) {
            if (errno == EINTR)
                continue;
            if (handle_nonblock(fd, POLLIN, errno))
                continue;
        }
        return nr;
    }
}

/*
 * xwrite() is the same a write(), but it automatically restarts write()
 * operations with a recoverable error (EAGAIN and EINTR). xwrite() DOES NOT
 * GUARANTEE that "len" bytes is written even if the operation is successful.
 */
ssize_t xwrite(int fd, const void* buf, size_t len) {
    ssize_t nr;
    if (len > MAX_IO_SIZE)
        len = MAX_IO_SIZE;
    while (1) {
        nr = write(fd, buf, len);
        if (nr < 0) {
            if (errno == EINTR)
                continue;
            if (handle_nonblock(fd, POLLOUT, errno))
                continue;
        }

        return nr;
    }
}

ssize_t read_in_full(int fd, void* buf, size_t count) {
    char*   p     = buf;
    ssize_t total = 0;

    while (count > 0) {
        ssize_t loaded = xread(fd, p, count);
        if (loaded < 0)
            return -1;
        if (loaded == 0)
            return total;
        count -= (size_t) loaded;
        p += loaded;
        total += loaded;
    }

    return total;
}

ssize_t write_in_full(int fd, const void* buf, size_t count) {
    const char* p     = buf;
    ssize_t     total = 0;

    while (count > 0) {
        ssize_t written = xwrite(fd, p, count);
        if (written < 0)
            return -1;
        if (!written) {
            errno = ENOSPC;
            return -1;
        }
        count -= (size_t) written;
        p += written;
        total += written;
    }

    return total;
}

// Handle replies from the IPC socket
int handle_reply(int data_fd)
{
    // Eh, recycle PIPE_BUF, it should be more than enough for our needs.
    char buf[PIPE_BUF] = { 0 };

    // We don't actually know the size of the reply, so, best effort here.
    ssize_t len = xread(data_fd, buf, sizeof(buf));
    if (len < 0) {
        // Only actual failures are left, xread handles the rest
        return KFMON_IPC_READ_FAILURE;
    }

    // If there's actually nothing to read (EoF), abort.
    if (len == 0) {
        return EXIT_FAILURE;
    }

    // Check the reply for failures
    if (strncmp(buf, "ERR_INVALID_ID", 14) == 0) {
        return KFMON_IPC_ERR_INVALID_ID;
    } else if (strncmp(buf, "WARN_ALREADY_RUNNING", 20) == 0) {
        return KFMON_IPC_WARN_ALREADY_RUNNING;
    } else if (strncmp(buf, "WARN_SPAWN_BLOCKED", 18) == 0) {
        return KFMON_IPC_WARN_SPAWN_BLOCKED;
    } else if (strncmp(buf, "WARN_SPAWN_INHIBITED", 20) == 0) {
        return KFMON_IPC_WARN_SPAWN_INHIBITED;
    } else if (strncmp(buf, "ERR_REALLY_MALFORMED_CMD", 24) == 0) {
        return KFMON_IPC_ERR_REALLY_MALFORMED_CMD;
    } else if (strncmp(buf, "ERR_MALFORMED_CMD", 17) == 0) {
        return KFMON_IPC_ERR_MALFORMED_CMD;
    } else if (strncmp(buf, "ERR_INVALID_CMD", 15) == 0) {
        return KFMON_IPC_ERR_INVALID_CMD;
    } else if (strncmp(buf, "OK", 2) == 0) {
        return EXIT_SUCCESS;
    } else {
        // NOTE: Obviously not viable if the command was "list" ;).
        return KFMON_IPC_UNKNOWN_REPLY;
    }

    // Done
    return EXIT_SUCCESS;
}
