#include "nn2_core_acquisition.h"
#include <string.h>
#include <limits.h>
_Static_assert(NN2_ACQ_PAYLOAD_SIZE <= NN2_CORE_PAYLOAD_MAX, "Acquisition MTU");
bool nn2_core_acquisition_fs_valid(uint8_t fs)
{ return fs == 2 || fs == 4 || fs == 5 || fs == 8 || fs == 10; }
void nn2_core_acquisition_reset(nn2_core_acquisition_t *a) { memset(a, 0, sizeof(*a)); }
nn2_status_t nn2_core_acquisition_window(nn2_core_acquisition_t *a, const nn2_core_acq_window_t *w)
{
    if (!a || !w || w->phase >= 40 || w->end_us <= w->commit_us ||
        w->commit_us < w->slot_start_us || w->commit_us-w->slot_start_us>65535u ||
        w->commit_us < w->origin_us || w->commit_us - w->origin_us > 1000000u)
        return NN2_ERR_INVALID_ARG;
    a->windows[a->window_count++ % NN2_ACQ_WINDOWS] = *w;
    return NN2_OK;
}
static void encode(const nn2_core_acquisition_t *a, uint8_t *p)
{
    memset(p, 0, NN2_ACQ_PAYLOAD_SIZE);
    memcpy(p, "ACQ1", 4); /* Local versioned binding, not the upstream TBD layout. */
    nn2_core_put_u32(p+4, a->frame);
    nn2_core_put_u32(p+8, a->adc_session);
    nn2_core_put_u64(p+12, a->origin_us);
    p[20]=20; p[21]=4; p[22]=NN2_ACQ_K_TARGET; p[23]=NN2_ACQ_K_MIN;
    nn2_core_put_u16(p+24, NN2_ACQ_SETTLE_US);
    nn2_core_put_u16(p+26, NN2_ACQ_FILTER_US);
    nn2_core_put_u16(p+28, NN2_ACQ_GUARD_US);
    p[30] = a->frame == 0 ? 1 : 0; /* First frame is warmup, never scientific. */
    for (unsigned s=0; s<20; ++s) {
        const nn2_core_acq_slot_t *v=&a->slots[s]; uint8_t *q=p+36+s*56;
        bool valid=v->used[0]>=8 && v->used[0]==v->used[1] && a->frame != 0;
        q[0]=(uint8_t)s; q[1]=v->used[0]; q[2]=v->used[1];
        q[3]=a->frame==0 ? NN2_ACQ_WARMUP :
            (v->used[0]<8 || v->used[1]<8) ? NN2_ACQ_UNDER_K :
            !valid ? NN2_ACQ_INVALID : v->used[0]<10 ? NN2_ACQ_MINIMUM : NN2_ACQ_TARGET;
        nn2_core_put_u32(q+4, valid ? 0u : 1u); /* Invalid pair / warmup. */
        for (unsigned phase=0; phase<2; ++phase) {
            for (unsigned ch=0; ch<4; ++ch) {
                int32_t mean=v->used[phase] ? (int32_t)v->sum[phase][ch] : INT32_MIN;
                nn2_core_put_u32(q+8+phase*16+ch*4, (uint32_t)mean);
            }
            nn2_core_put_u16(q+40+phase*2, v->commit[phase]);
            nn2_core_put_u16(q+44+phase*2, v->stable[phase]);
            nn2_core_put_u32(q+48+phase*4, v->first_sequence[phase]);
        }
    }
}
nn2_status_t nn2_core_acquisition_sample(nn2_core_acquisition_t *a,
    const nn2_core_adc_record_t *r, uint8_t *payload)
{
    if (!a || !r || !payload) return NN2_ERR_INVALID_ARG;
    if (!r->drdy_us || r->source != NN2_SAMPLE_SRC_ADC || r->quality ||
        (a->seen_sample && (r->sequence-a->last_sequence != 1u || r->session != a->adc_session)))
        return NN2_ERR_IO;
    a->last_sequence=r->sequence; a->adc_session=r->session; a->seen_sample=true;
    const nn2_core_acq_window_t *w=NULL;
    unsigned count=a->window_count<NN2_ACQ_WINDOWS?a->window_count:NN2_ACQ_WINDOWS;
    for (unsigned i=0; i<count; ++i) {
        const nn2_core_acq_window_t *v=&a->windows[i];
        if (r->drdy_us >= v->commit_us && r->drdy_us < v->end_us) { w=v; break; }
    }
    if (!w) return NN2_ERR_EMPTY; /* Transition, guard, pre-scan. */
    nn2_status_t result=NN2_ERR_EMPTY;
    if (!a->active || w->frame != a->frame) {
        if (a->active) {
            if (w->frame-a->frame != 1u) return NN2_ERR_IO;
            encode(a,payload); result=NN2_OK;
        }
        memset(a->slots,0,sizeof(a->slots));
        a->frame=w->frame; a->origin_us=w->origin_us; a->active=true;
    }
    unsigned phase=w->phase&1u, slot=w->phase/2u;
    nn2_core_acq_slot_t *v=&a->slots[slot];
    /* Full filter support must lie after analog settling. SPI completion is
     * irrelevant. A late software latch remains TS_ESTIMATED on the wire. */
    uint64_t begin=w->commit_us+NN2_ACQ_SETTLE_US+NN2_ACQ_FILTER_US;
    v->commit[phase]=(uint16_t)(w->commit_us-w->slot_start_us);
    v->stable[phase]=w->end_us>begin?(uint16_t)(w->end_us-begin):0;
    if (r->drdy_us < begin || v->used[phase]>=NN2_ACQ_K_TARGET) return result;
    if (!v->used[phase]) v->first_sequence[phase]=r->sequence;
    else if (r->sequence-v->first_sequence[phase] != v->used[phase]) return NN2_ERR_IO;
    for (unsigned ch=0;ch<4;++ch) v->sum[phase][ch]+=r->channel[ch];
    ++v->used[phase];
    return result;
}
