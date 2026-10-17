#ifndef NN2_PORT_PROV_CONSOLE_H
#define NN2_PORT_PROV_CONSOLE_H

#include "nn2_status.h"
#include "nn2_wifi_cred.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ProvisioningConsolePort (nn2_port_prov_console).
   Locked by Owner A1 on issue #30 (comment 6226888814).

   One-shot SSID then password. This is not a host-API shell and not a
   command port. It exposes no emitter command and no LED-on command.
   Owner lock O8 on issue #33 (USB may later request emitter shutdown,
   and must never turn LEDs on) is a later issue. This port does not
   implement that request.

   Caller context: task. ISR use is forbidden.
   Blocking: read_once waits at most timeout_ms for each line. The wait
   is finite. A missing host stays in provision and retries; there is
   no SoftAP fallback.
   Ownership: the caller owns out. The password is masked on echo.
   SSID may be echoed. The password must not be logged.
   Errors: NN2_ERR_TIMEOUT, NN2_ERR_INVALID (empty SSID, bad length, or
   a line that does not fit), NN2_ERR_IO, NN2_ERR_INVALID_ARG.

   Timing expectation: the timeout is a budget, not a measured console
   latency (Class H NOT RUN). */

typedef struct nn2_port_prov_console {
    nn2_status_t (*read_once)(void *ctx, nn2_wifi_credentials_t *out, uint32_t timeout_ms);
    void *ctx;
} nn2_port_prov_console_t;

#ifdef __cplusplus
}
#endif

#endif /* NN2_PORT_PROV_CONSOLE_H */
