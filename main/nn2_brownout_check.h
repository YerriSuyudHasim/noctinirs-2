/* Issue #49: the brownout detector is on, and the recorded level is the
 * ESP-IDF v6.1 esp32s3 Kconfig default SEL_7. IDF labels that choice "2.44V"
 * and says the voltages are estimates. This is not a measured NoctiNIRS
 * threshold. Agent 04 confirms or replaces it.
 */
#ifndef NN2_BROWNOUT_CHECK_H
#define NN2_BROWNOUT_CHECK_H

#include "sdkconfig.h"

#ifndef CONFIG_ESP_BROWNOUT_DET
#error issue #49 requires CONFIG_ESP_BROWNOUT_DET
#endif
#ifndef CONFIG_ESP_BROWNOUT_DET_LVL_SEL_7
#error issue #49 records brownout level SEL_7, the ESP-IDF v6.1 esp32s3 default
#endif
#ifndef CONFIG_ESP_BROWNOUT_DET_LVL
#error issue #49 requires the recorded brownout level to be present
#endif

#endif /* NN2_BROWNOUT_CHECK_H */
