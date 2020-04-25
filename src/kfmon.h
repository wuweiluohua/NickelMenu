#ifndef NM_KFMON_H
#define NM_KFMON_H
#ifdef __cplusplus
extern "C" {
#endif

#include "action.h"

// Path to our IPC Unix socket
#define KFMON_IPC_SOCKET "/tmp/kfmon-ipc.ctl"

// Flags for the failure bingo bitmask
#define KFMON_IPC_ETIMEDOUT                (1 << 1)
#define KFMON_IPC_EPIPE                    (1 << 2)
#define KFMON_IPC_ENODATA                  (1 << 3)
#define KFMON_IPC_READ_FAILURE             (1 << 4)
// Those match the actual string sent over the wire
#define KFMON_IPC_ERR_INVALID_ID           (1 << 5)
#define KFMON_IPC_WARN_ALREADY_RUNNING     (1 << 6)
#define KFMON_IPC_WARN_SPAWN_BLOCKED       (1 << 7)
#define KFMON_IPC_WARN_SPAWN_INHIBITED     (1 << 8)
#define KFMON_IPC_ERR_REALLY_MALFORMED_CMD (1 << 9)
#define KFMON_IPC_ERR_MALFORMED_CMD        (1 << 10)
#define KFMON_IPC_ERR_INVALID_CMD          (1 << 11)
// Not an error ;p
//#define KFMON_IPC_OK
#define KFMON_IPC_UNKNOWN_REPLY            (1 << 12)

// Handle a KFMon IPC request
nm_action_result_t* nm_kfmon_request(const char *ipc_cmd, const char *ipc_arg, char **err_out);

#ifdef __cplusplus
}
#endif
#endif
