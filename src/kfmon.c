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
        return KFMON_IPC_REPLY_READ_FAILURE;
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

// Connect to KFMon's IPC socket. Returns error code, store data fd by ref.
static int connect_to_kfmon_socket(int *restrict data_fd) {
    // Setup the local socket
    if ((*data_fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0)) == -1) {
        return KFMON_IPC_SOCKET_FAILURE;
    }

    struct sockaddr_un sock_name = { 0 };
    sock_name.sun_family         = AF_UNIX;
    strncpy(sock_name.sun_path, KFMON_IPC_SOCKET, sizeof(sock_name.sun_path) - 1);

    // Connect to IPC socket
    if (connect(*data_fd, (const struct sockaddr*) &sock_name, sizeof(sock_name)) == -1) {
        return KFMON_IPC_CONNECT_FAILURE;
    }

    // Wheee!
    return EXIT_SUCCESS;
}

// Send a packet to KFMon over the wire (payload *MUST* be NUL-terminated to avoid truncation, and len *MUST* include that NUL).
static int send_packet(int data_fd, const char *restrict payload, size_t len) {
    // Send it (w/ a NUL)
    if (write_in_full(data_fd, payload, len) < 0) {
        // Only actual failures are left, xwrite handles the rest
        return KFMON_IPC_WRITE_FAILURE;
    }

    // Wheee!
    return EXIT_SUCCESS;
}

// Send the requested IPC command:arg pair
static int send_ipc_command(int data_fd, const char *restrict ipc_cmd, const char *restrict ipc_arg) {
    char buf[256] = { 0 };
    int packet_len = snprintf(buf, sizeof(buf), "%s:%s", ipc_cmd, ipc_arg);
    // Send it (w/ a NUL)
    return send_packet(data_fd, buf, (size_t) (packet_len + 1));
}

// Poll the IPC socket for a *single* reply, timeout after attempts * timeout (ms)
static int wait_for_reply(int data_fd, int timeout, size_t attempts) {
    int status = EXIT_SUCCESS;

    struct pollfd pfd = { 0 };
    // Data socket
    pfd.fd     = data_fd;
    pfd.events = POLLIN;

    // Here goes... We'll wait for <attempts> windows of <timeout>ms
    size_t retry = 0U;
    while (1) {
        int poll_num = poll(&pfd, 1, timeout);
        if (poll_num == -1) {
            if (errno == EINTR) {
                continue;
            }
            return KFMON_IPC_POLL_FAILURE;
        }

        if (poll_num > 0) {
            if (pfd.revents & POLLIN) {
                // There was a reply from the socket
                int reply = handle_reply(data_fd);
                if (reply != EXIT_SUCCESS) {
                    // If the remote closed the connection, we get POLLIN|POLLHUP w/ EoF ;).
                    if (pfd.revents & POLLHUP) {
                        // Flag that as an error
                        status = KFMON_IPC_EPIPE;
                    } else {
                        if (reply == KFMON_IPC_REPLY_READ_FAILURE) {
                            // We failed to read the reply
                            status = KFMON_IPC_REPLY_READ_FAILURE;
                        } else if (reply == EXIT_FAILURE) {
                            // There wasn't actually any data!
                            status = KFMON_IPC_ENODATA;
                        } else {
                            // That's an IPC-specific failure, pass it as-is
                            status = reply;
                        }
                    }
                    // We're obviously done if something went wrong.
                    break;
                } else {
                    // We break on success, too, as we only need to send a single command.
                    status = EXIT_SUCCESS;
                    break;
                }
            }

            // Remote closed the connection
            if (pfd.revents & POLLHUP) {
                // Flag that as an error
                status = KFMON_IPC_EPIPE;
                break;
            }
        }

        if (poll_num == 0) {
            // Timed out, increase the retry counter
            retry++;
        }

        // Drop the axe after the final attempt
        if (retry >= attempts) {
            status = KFMON_IPC_ETIMEDOUT;
            break;
        }
    }

    return status;
}

// Handle a simple KFMon IPC request
int nm_kfmon_simple_request(const char *restrict ipc_cmd, const char *restrict ipc_arg) {
    // Assume everything's peachy until shit happens...
    int status = EXIT_SUCCESS;

    int data_fd = -1;
    // Attempt to connect to KFMon...
    // As long as KFMon is up, has very little chance to fail, even if the connection backlog is full.
    status = connect_to_kfmon_socket(&data_fd);
    // If it failed, return early
    if (status != EXIT_SUCCESS) {
        return status;
    }

    // Attempt to send the specified command in full over the wire
    status = send_ipc_command(data_fd, ipc_cmd, ipc_arg);
    // If it failed, return early, after closing the socket
    if (status != EXIT_SUCCESS) {
        close(data_fd);
        return status;
    }

    // We'll be polling the socket for a reply, this'll make things neater, and allows us to abort on timeout,
    // in the unlikely event there's already an IPC session being handled by KFMon,
    // in which case the reply would be delayed by an undeterminate amount of time (i.e., until KFMon gets to it).
    // Here, we'll want to timeout after 2s
    status = wait_for_reply(data_fd, 500, 4);
    // NOTE: We happen to be done with the connection right now.
    //       But if we still needed it, KFMON_IPC_POLL_FAILURE would warrant an early abort w/ a forced close().

    // Bye now!
    close(data_fd);
    return status;
}

// Giant ladder of fail
nm_action_result_t* nm_kfmon_return_handler(int status, char **err_out) {
    #define NM_ERR_RET NULL

    if (status != EXIT_SUCCESS) {
        // Fail w/ the right log message
        if (status == KFMON_IPC_ETIMEDOUT) {
            NM_RETURN_ERR("Timed out waiting for a reply from KFMon");
        } else if (status == KFMON_IPC_EPIPE) {
            NM_RETURN_ERR("KFMon closed the connection");
        } else if (status == KFMON_IPC_ENODATA) {
            NM_RETURN_ERR("No more data to read");
        } else if (status == KFMON_IPC_READ_FAILURE) {
            NM_RETURN_ERR("read: %m");
        } else if (status == KFMON_IPC_WRITE_FAILURE) {
            NM_RETURN_ERR("write: %m");
        } else if (status == KFMON_IPC_SOCKET_FAILURE) {
            NM_RETURN_ERR("Failed to create local KFMon IPC socket (socket: %m)");
        } else if (status == KFMON_IPC_CONNECT_FAILURE) {
            NM_RETURN_ERR("KFMon IPC is down (connect: %m)");
        } else if (status == KFMON_IPC_POLL_FAILURE) {
            NM_RETURN_ERR("poll: %m");
        } else if (status == KFMON_IPC_REPLY_READ_FAILURE) {
            // NOTE: Let's hope close() won't mangle errno...
            NM_RETURN_ERR("Failed to read KFMon's reply (%m)");
        } else if (status == KFMON_IPC_ERR_INVALID_ID) {
            NM_RETURN_ERR("Requested to start an invalid watch index");
        } else if (status == KFMON_IPC_WARN_ALREADY_RUNNING) {
            NM_RETURN_ERR("Requested watch is already running");
        } else if (status == KFMON_IPC_WARN_SPAWN_BLOCKED) {
            NM_RETURN_ERR("A spawn blocker is currently running");
        } else if (status == KFMON_IPC_WARN_SPAWN_INHIBITED) {
            NM_RETURN_ERR("Spawns are currently inhibited");
        } else if (status == KFMON_IPC_ERR_REALLY_MALFORMED_CMD) {
            NM_RETURN_ERR("KFMon couldn't parse our command");
        } else if (status == KFMON_IPC_ERR_MALFORMED_CMD) {
            NM_RETURN_ERR("Bad command syntax");
        } else if (status == KFMON_IPC_ERR_INVALID_CMD) {
            NM_RETURN_ERR("Command wasn't recognized by KFMon");
        } else if (status == KFMON_IPC_UNKNOWN_REPLY) {
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
