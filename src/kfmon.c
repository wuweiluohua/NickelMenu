#pragma once

// Mostly to make IDEs happy
#include "kfmon.h"

// Handle replies from the IPC socket
static int handle_reply(int data_fd)
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
