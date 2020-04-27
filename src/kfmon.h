#ifndef NM_KFMON_H
#define NM_KFMON_H
#ifdef __cplusplus
extern "C" {
#endif

#include "action.h"

// Path to KFMon's IPC Unix socket
#define KFMON_IPC_SOCKET "/tmp/kfmon-ipc.ctl"

// Flags for the failure bingo bitmask
#define KFMON_IPC_ETIMEDOUT                (1 << 1)
#define KFMON_IPC_EPIPE                    (1 << 2)
#define KFMON_IPC_ENODATA                  (1 << 3)
// syscall failures
#define KFMON_IPC_READ_FAILURE             (1 << 4)
#define KFMON_IPC_SEND_FAILURE             (1 << 5)
#define KFMON_IPC_SOCKET_FAILURE           (1 << 6)
#define KFMON_IPC_CONNECT_FAILURE          (1 << 7)
#define KFMON_IPC_POLL_FAILURE             (1 << 8)
#define KFMON_IPC_REPLY_READ_FAILURE       (1 << 9)
// Those match the actual string sent over the wire
#define KFMON_IPC_ERR_INVALID_ID           (1 << 10)
#define KFMON_IPC_WARN_ALREADY_RUNNING     (1 << 11)
#define KFMON_IPC_WARN_SPAWN_BLOCKED       (1 << 12)
#define KFMON_IPC_WARN_SPAWN_INHIBITED     (1 << 13)
#define KFMON_IPC_ERR_REALLY_MALFORMED_CMD (1 << 14)
#define KFMON_IPC_ERR_MALFORMED_CMD        (1 << 15)
#define KFMON_IPC_ERR_INVALID_CMD          (1 << 16)
// Not an error ;p
//#define KFMON_IPC_OK
#define KFMON_IPC_UNKNOWN_REPLY            (1 << 17)

// Given one of the error codes listed above, return properly from an action. Success is silent.
nm_action_result_t* nm_kfmon_return_handler(int error, char **err_out);

// Send a simple KFMon IPC request, one where the reply is only used for its diagnostic value.
int nm_kfmon_simple_request(const char *restrict ipc_cmd, const char *restrict ipc_arg);

#ifdef __cplusplus
}
#endif
#endif
