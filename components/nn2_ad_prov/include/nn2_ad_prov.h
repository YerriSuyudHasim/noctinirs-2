#ifndef NN2_AD_PROV_H
#define NN2_AD_PROV_H

#include "nn2_port_prov_console.h"

#ifdef __cplusplus
extern "C" {
#endif

/* USB Serial/JTAG console reader. One SSID line, then one masked password
   line. No command parser and no emitter control. */
nn2_status_t nn2_ad_prov_init(nn2_port_prov_console_t *out);

#ifdef __cplusplus
}
#endif

#endif /* NN2_AD_PROV_H */
