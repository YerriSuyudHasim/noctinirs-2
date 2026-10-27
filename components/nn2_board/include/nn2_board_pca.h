/* PCA9956B register adapter. Portable I/O seam for host fault injection, not
 * domain policy. Main-board source/limits: docs/adr/0005-led-bringup.md. */
#ifndef NN2_BOARD_PCA_H
#define NN2_BOARD_PCA_H
#include "nn2_core_optical.h"
typedef struct {
    nn2_status_t (*reset)(void *, bool asserted);
    void (*delay_us)(void *, uint32_t);
    nn2_status_t (*write)(void *, const uint8_t *, size_t);
    nn2_status_t (*read)(void *, uint8_t, uint8_t *, size_t);
    void *ctx;
} nn2_board_pca_io_t;
typedef struct {
    nn2_board_pca_io_t io;
    uint8_t on[NN2_OPTICAL_SLOTS][7];
    int selected;
    bool ready;
} nn2_board_pca_t;
nn2_status_t nn2_board_pca_prepare(nn2_board_pca_t *, const nn2_board_pca_io_t *,
                                  const uint16_t current_ma[NN2_OPTICAL_SLOTS]);
nn2_status_t nn2_board_pca_on(nn2_board_pca_t *, uint8_t slot);
nn2_status_t nn2_board_pca_off(nn2_board_pca_t *);
nn2_status_t nn2_board_pca_health(nn2_board_pca_t *);
nn2_status_t nn2_board_pca_shutdown(nn2_board_pca_t *);
uint8_t nn2_board_pca_channel(uint8_t slot);
/* Unconditional boot zero/readback/reset sequence; no SLEEP/MODE1 writes. */
nn2_status_t nn2_board_pca_safe_boot(const nn2_board_pca_io_t *);
#endif
