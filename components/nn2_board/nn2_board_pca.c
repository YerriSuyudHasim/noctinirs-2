#include "nn2_board_pca.h"
#include "nn2_board_wiring.h"
#include <string.h>
#define MODE1 0x00u
#define MODE2 0x01u
#define LEDOUT0 0x02u
#define IREF0 0x22u
#define PWMALL 0x3fu
#define IREFALL 0x40u
#define EFLAG0 0x41u
static const uint8_t channels[20] = {
    0,1,2,3,4,5,6,7,8,9,14,15,16,17,18,19,20,21,22,23
};
uint8_t nn2_board_pca_channel(uint8_t slot) {
    return slot < 20 ? channels[slot] : UINT8_MAX;
}
static nn2_status_t reg(nn2_board_pca_t *p, uint8_t address, uint8_t value) {
    const uint8_t data[2] = {address, value};
    return p->io.write(p->io.ctx, data, sizeof(data));
}
static nn2_status_t fail(nn2_board_pca_t *p) {
    if (!p->fault_saved) { p->first_fault = p->diagnostics; p->fault_saved = true; }
    p->ready = false;
    p->selected = -1;
    (void)p->io.reset(p->io.ctx, true);
    return NN2_ERR_IO;
}
static bool matches(nn2_board_pca_t *p, uint8_t address,
                    const uint8_t *expected, size_t n) {
    uint8_t actual[24];
    return n <= sizeof(actual) &&
        p->io.read(p->io.ctx, address, actual, n) == NN2_OK &&
        !memcmp(expected, actual, n);
}
nn2_status_t nn2_board_pca_safe_boot(const nn2_board_pca_io_t *io) {
    if (!io || !io->reset || !io->delay_us || !io->read || !io->write)
        return NN2_ERR_INVALID_ARG;
    nn2_board_pca_t p = {.io = *io, .selected = -1};
    if (io->reset(io->ctx, true) != NN2_OK) return fail(&p);
    io->delay_us(io->ctx, 10);
    if (io->reset(io->ctx, false) != NN2_OK) return fail(&p);
    io->delay_us(io->ctx, 2000);
    if (reg(&p, IREFALL, 0) != NN2_OK || reg(&p, PWMALL, 0) != NN2_OK)
        return fail(&p);
    for (unsigned i = 0; i < 6; ++i)
        if (reg(&p, (uint8_t)(LEDOUT0 + i), 0) != NN2_OK) return fail(&p);
    const uint8_t zero = 0;
    for (unsigned i = 0; i < 24; ++i)
        if (!matches(&p, (uint8_t)(IREF0 + i), &zero, 1)) return fail(&p);
    for (unsigned i = 0; i < 6; ++i)
        if (!matches(&p, (uint8_t)(LEDOUT0 + i), &zero, 1)) return fail(&p);
    return io->reset(io->ctx, true);
}
nn2_status_t nn2_board_pca_prepare(nn2_board_pca_t *p,
                                  const nn2_board_pca_io_t *io,
                                  const uint16_t current[20]) {
    if (!p || !io || !io->reset || !io->delay_us || !io->read || !io->write || !current)
        return NN2_ERR_INVALID_ARG;
    for (unsigned i = 0; i < 20; ++i)
        if (current[i] > NN2_OPTICAL_MAX_MA) return NN2_ERR_INVALID_ARG;
    memset(p, 0, sizeof(*p));
    p->io = *io;
    p->selected = -1;
    if (io->reset(io->ctx, true) != NN2_OK) return fail(p);
    io->delay_us(io->ctx, 10);
    if (io->reset(io->ctx, false) != NN2_OK) return fail(p);
    io->delay_us(io->ctx, 2000); /* Datasheet recovery <=1.5 ms. */
    /* Safe zeroing uses single-register access before assuming AI mode. */
    if (reg(p, IREFALL, 0) != NN2_OK || reg(p, PWMALL, 0) != NN2_OK)
        return fail(p);
    for (unsigned i = 0; i < 6; ++i)
        if (reg(p, (uint8_t)(LEDOUT0 + i), 0) != NN2_OK) return fail(p);
    /* Disable sub/all-call, SLEEP=0, AI=110 for contiguous config registers.
     * OCH=0 commits the complete LEDOUT burst together at I2C STOP. */
    if (reg(p, MODE1, 0x40) != NN2_OK || reg(p, MODE2, 0) != NN2_OK)
        return fail(p);
    const uint8_t zero[24] = {0};
    if (!matches(p, (uint8_t)(0x80 | IREF0), zero, 24) ||
        !matches(p, (uint8_t)(0x80 | LEDOUT0), zero, 6)) return fail(p);
    uint8_t mode1, mode2;
    if (io->read(io->ctx, MODE1, &mode1, 1) != NN2_OK ||
        io->read(io->ctx, MODE2, &mode2, 1) != NN2_OK ||
        (mode1 & 0x7f) != 0x40 || (mode2 & 0xe8) != 0) return fail(p);
    /* NXP 7.3.14: detection is disabled below PWM code8, even though
     * LEDOUT FULL_ON bypasses dimming. Keep detection eligible explicitly. */
    p->diagnostics.mode2 = mode2;
    uint8_t pwm[24];
    memset(pwm, 0xff, sizeof(pwm));
    if (reg(p, PWMALL, 0xff) != NN2_OK ||
        !matches(p, (uint8_t)(0x80 | 0x0a), pwm, sizeof(pwm))) return fail(p);
    uint8_t iref[25] = {0x80 | IREF0};
    for (unsigned slot = 0; slot < 20; ++slot) {
        uint8_t ch = channels[slot];
        /* I_LED = code * 0.9/(4*Rext); main:R4=1000 ohm. Floor avoids
         * rounding above the requested nominal current; tolerance still applies. */
        iref[1 + ch] = (uint8_t)((uint32_t)current[slot] *
            (4u * NN2_BOARD_PCA_REXT_OHM) / 900u);
        p->on[slot][0] = 0x80 | LEDOUT0;
        p->off[slot][0] = (uint8_t)(LEDOUT0 + ch / 4);
        p->off[slot][1] = 0;
        if (iref[1 + ch]) p->expected_channels |= UINT32_C(1) << ch;
        if (iref[1 + ch])
            p->on[slot][1 + ch / 4] = (uint8_t)(1u << (2u * (ch % 4)));
    }
    if (io->write(io->ctx, iref, sizeof(iref)) != NN2_OK ||
        !matches(p, (uint8_t)(0x80 | IREF0), iref + 1, 24)) return fail(p);
    p->ready = true;
    return NN2_OK;
}
nn2_status_t nn2_board_pca_on(nn2_board_pca_t *p, uint8_t slot) {
    if (!p || !p->ready || slot >= 20) return NN2_ERR_INVALID_ARG;
    if (p->selected >= 0) return fail(p);
    if (slot == 0) {
        p->frame_checked = false;
        uint32_t epoch = p->diagnostics.epoch + 1;
        uint32_t warnings = p->diagnostics.warning_frames;
        p->diagnostics = (nn2_board_pca_diagnostics_t){.epoch = epoch, .warning_frames = warnings};
    }
    if (p->io.write(p->io.ctx, p->on[slot], 7) != NN2_OK) return fail(p);
    uint8_t ch = channels[slot];
    p->selected = ch;
    return NN2_OK;
}
nn2_status_t nn2_board_pca_off(nn2_board_pca_t *p) {
    if (!p || !p->ready) return NN2_ERR_INVALID_STATE;
    if (p->selected < 0) return NN2_OK;
    uint8_t ch = (uint8_t)p->selected;
    /* Sample at the end of the ON half-slot, before OFF. No busy wait. */
    /* Capture the active channel while ON, before fault reset clears EFLAG.
     * Other channels in this register may have changed since their ON slot:
     * merge only this channel, preserving its peers' last actual samples. */
    if (p->expected_channels & (UINT32_C(1) << ch)) {
        uint8_t flags;
        if (p->io.read(p->io.ctx, (uint8_t)(EFLAG0 + ch / 4), &flags, 1) != NN2_OK)
            return fail(p);
        unsigned shift = 2u * (ch % 4u);
        uint8_t field_mask = (uint8_t)(3u << shift);
        uint8_t code = (uint8_t)((flags >> shift) & 3u);
        nn2_board_pca_diagnostics_t *d = &p->diagnostics;
        d->flags[ch / 4] = (uint8_t)((d->flags[ch / 4] & (uint8_t)~field_mask) |
                                    (flags & field_mask));
        uint32_t bit = UINT32_C(1) << ch;
        if (d->valid_channels & bit) return fail(p);
        d->valid_channels |= bit;
        d->open_channels = (d->open_channels & ~bit) | (code == 2 ? bit : 0);
        d->short_channels = (d->short_channels & ~bit) | (code == 1 ? bit : 0);
        d->invalid_channels = (d->invalid_channels & ~bit) | (code == 3 ? bit : 0);
        if (code >= 2) return fail(p); /* Stop at the observed sample. */
    }
    /* V4 transaction idea: FULL ON established a known one-hot image. Only
     * its selected LEDOUT byte needs clearing for normal OFF. Reset and boot
     * still clear all outputs; unknown/failed state is never eligible. */
    if (p->compact_off) {
        unsigned slot = 0;
        while (slot < NN2_OPTICAL_SLOTS && channels[slot] != ch) ++slot;
        if (slot == NN2_OPTICAL_SLOTS ||
            p->io.write(p->io.ctx, p->off[slot], 2) != NN2_OK) return fail(p);
        p->selected = -1;
        return NN2_OK;
    }
    /* Conservative full-image OFF remains available outside the async owner. */
    const uint8_t all_off[7] = {0x80 | LEDOUT0, 0, 0, 0, 0, 0, 0};
    if (p->io.write(p->io.ctx, all_off, sizeof(all_off)) != NN2_OK) return fail(p);
    p->selected = -1;
    return NN2_OK;
}
nn2_status_t nn2_board_pca_health(nn2_board_pca_t *p) {
    uint8_t modes[2];
    if (p && p->frame_checked) return fail(p);
    if (!p || !p->ready) return NN2_ERR_INVALID_STATE;
    /* MODE1 AI[1:0]=10 permits this adjacent two-byte read (NXP Table6).
     * EFLAG is outside that range and is still sampled individually while ON. */
    if (p->io.read(p->io.ctx, (uint8_t)(0x80 | MODE1), modes, 2) != NN2_OK) return fail(p);
    uint8_t mode1 = modes[0], mode = modes[1];
    nn2_board_pca_diagnostics_t *d = &p->diagnostics;
    d->mode2 = mode;
    if ((mode1 & 0x7f) != 0x40 || (mode & 0xa8) ||
        d->valid_channels != p->expected_channels || d->open_channels || d->invalid_channels)
        return fail(p);
    bool error = (mode & 0x40) != 0;
    if (error) {
        if (!p->short_advisory || p->expected_channels != UINT32_C(0xffc3ff) ||
            d->short_channels != UINT32_C(0xffc3ff)) return fail(p);
        ++d->warning_frames;
    } else if (d->short_channels) return fail(p); /* Inconsistent live evidence. */
    p->frame_checked = true;
    p->last_complete = *d;
    return NN2_OK;
}
nn2_status_t nn2_board_pca_shutdown(nn2_board_pca_t *p) {
    if (!p || !p->io.reset) return NN2_ERR_INVALID_ARG;
    p->ready = false;
    p->selected = -1;
    return p->io.reset(p->io.ctx, true);
}
