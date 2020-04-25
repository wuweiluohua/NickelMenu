#include "util.h"
#include "kfmon_priv.h"
#include "kfmon.h"

// Handle replies from the IPC socket
static int handle_reply(int data_fd) {
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

// Handle a KFMon IPC request
nm_action_result_t* nm_kfmon_request(const char *ipc_cmd, const char *ipc_arg, char **err_out) {
    #define NM_ERR_RET NULL

    // Setup the local socket
    int data_fd = -1;
    if ((data_fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0)) == -1) {
        NM_RETURN_ERR("Failed to create local KFMon IPC socket (socket: %m)");
    }

    struct sockaddr_un sock_name = { 0 };
    sock_name.sun_family         = AF_UNIX;
    strncpy(sock_name.sun_path, KFMON_IPC_SOCKET, sizeof(sock_name.sun_path) - 1);

    // Connect to IPC socket
    if (connect(data_fd, (const struct sockaddr*) &sock_name, sizeof(sock_name)) == -1) {
        NM_RETURN_ERR("KFMon IPC is down (connect: %m)");
    }

    // Attempt to send the specified command in full over the wire
    char buf[PIPE_BUF] = { 0 };
    int packet_len = snprintf(buf, sizeof(buf), "%s:%s", ipc_cmd, ipc_arg);
    // Send it (w/ a NUL)
    if (write_in_full(data_fd, buf, (size_t)(packet_len + 1)) < 0) {
        // Only actual failures are left, xwrite handles the rest
        // Don't forget to close the socket...
        close(data_fd);
        NM_RETURN_ERR("write: %m");
    }

    // We'll be polling the socket for a reply, this'll make things neater, and allows us to abort on timeout,
    // in the unlickely event there's already an IPC session being handled by KFMon...
    int       poll_num;
    struct pollfd pfd = { 0 };
    // Data socket
    pfd.fd     = data_fd;
    pfd.events = POLLIN;

    // Assume everything's peachy until shit happens...
    int failed = 0;

    // Here goes...
    while (1) {
        // We'll wait for a few short windows of 500ms
        size_t retries = 0U;
        poll_num = poll(&pfd, 1, 500);
        if (poll_num == -1) {
            if (errno == EINTR) {
                continue;
            }
            // Don't forget to close the socket...
            close(data_fd);
            NM_RETURN_ERR("poll: %m");
        }

        if (poll_num > 0) {
            if (pfd.revents & POLLIN) {
                // There was a reply from the socket
                int reply = handle_reply(data_fd);
                if (reply != EXIT_SUCCESS) {
                    // If the remote closed the connection, we get POLLIN|POLLHUP w/ EoF ;).
                    if (pfd.revents & POLLHUP) {
                        // Flag that as an error
                        failed |= KFMON_IPC_EPIPE;
                    } else {
                        if (reply == KFMON_IPC_READ_FAILURE) {
                            // We failed to read the reply
                            failed |= KFMON_IPC_READ_FAILURE;
                        } else if (reply == EXIT_FAILURE) {
                            // There wasn't actually any data!
                            failed |= KFMON_IPC_ENODATA;
                        } else {
                            // That's an IPC-specific failure, pass it as-is
                            failed |= reply;
                        }
                    }
                    // We're obviously done if something went wrong.
                    break;
                } else {
                    // We break on success, too, as we only need to send a single command.
                    break;
                }
            }

            // Remote closed the connection
            if (pfd.revents & POLLHUP) {
                // Flag that as an error
                failed |= KFMON_IPC_EPIPE;
                break;
            }
        }

        if (poll_num == 0) {
            // Timed out, increase the retry counter
            retries++;
        }

        // Drop the axe after 2s.
        if (retries >= 4) {
            failed |= KFMON_IPC_ETIMEDOUT;
            break;
        }
    }

    // Bye now!
    close(data_fd);

    if (failed > 0) {
        // Fail w/ the right log message (this returns, so the bitmask is a bit wasted here, meh.)
        if (failed & KFMON_IPC_ETIMEDOUT) {
            NM_RETURN_ERR("Timed out waiting for a reply from KFMon");
        } else if (failed & KFMON_IPC_EPIPE) {
            NM_RETURN_ERR("KFMon closed the connection");
        } else if (failed & KFMON_IPC_ENODATA) {
            NM_RETURN_ERR("No more data to read");
        } else if (failed & KFMON_IPC_READ_FAILURE) {
            // NOTE: We probably can't salvage errno here, because we're behind close() :/
            NM_RETURN_ERR("Failed to read KFMon's reply");
        } else if (failed & KFMON_IPC_ERR_INVALID_ID) {
            NM_RETURN_ERR("Requested to start an invalid watch index");
        } else if (failed & KFMON_IPC_WARN_ALREADY_RUNNING) {
            NM_RETURN_ERR("Requested watch is already running");
        } else if (failed & KFMON_IPC_WARN_SPAWN_BLOCKED) {
            NM_RETURN_ERR("A spawn blocker is currently running");
        } else if (failed & KFMON_IPC_WARN_SPAWN_INHIBITED) {
            NM_RETURN_ERR("Spawns are currently inhibited");
        } else if (failed & KFMON_IPC_ERR_REALLY_MALFORMED_CMD) {
            NM_RETURN_ERR("KFMon couldn't parse our command");
        } else if (failed & KFMON_IPC_ERR_MALFORMED_CMD) {
            NM_RETURN_ERR("Bad command syntax");
        } else if (failed & KFMON_IPC_ERR_INVALID_CMD) {
            NM_RETURN_ERR("Command wasn't recognized by KFMon");
        } else if (failed & KFMON_IPC_UNKNOWN_REPLY) {
            NM_RETURN_ERR("We couldn't make sense of KFMon's reply");
        } else {
            // Should never happen
            NM_RETURN_ERR("Something went wrong");
        }
    } else {
        NM_RETURN_OK(nm_action_result_silent());
    }

    #undef NM_ERR_RET
}
