#include "nn2_core_optical_codec.h"
#include <stdbool.h>
_Static_assert(NN2_OPT2_PREFIX_BYTES == 24u, "OPT2 prefix");
_Static_assert(NN2_OPT2_SLOT_BYTES == 64u, "OPT2 slot");
_Static_assert(NN2_OPT2_DETECTOR_BYTES == 13u, "OPT2 detector");
_Static_assert(12u + 4u * NN2_OPT2_DETECTOR_BYTES == NN2_OPT2_SLOT_BYTES, "OPT2 slot fields");
_Static_assert(NN2_OPT2_PREFIX_BYTES + 20u * NN2_OPT2_SLOT_BYTES + 80u == NN2_OPT2_PAYLOAD_BYTES && NN2_OPT2_PAYLOAD_BYTES == 1384u, "OPT2 payload");
_Static_assert(NN2_RAW_PREFIX_BYTES == 16u, "RAW prefix");
_Static_assert(NN2_RAW_SAMPLE_BYTES == 14u, "RAW sample");
_Static_assert(NN2_RAW_PREFIX_BYTES + 100u * NN2_RAW_SAMPLE_BYTES == NN2_RAW_PAYLOAD_MAX && NN2_RAW_PAYLOAD_MAX == 1416u, "RAW max");
static void put(uint8_t *p, uint32_t v, unsigned n)
{ for (unsigned i=0;i<n;++i) p[i]=(uint8_t)(v >> (8u*i)); }
static uint32_t get(const uint8_t *p, unsigned n)
{ uint32_t v=0; for (unsigned i=0;i<n;++i) v |= (uint32_t)p[i] << (8u*i); return v; }
/* Avoid implementation-defined unsigned-to-signed conversion. */
static int32_t signed_word(uint32_t v, unsigned bits)
{ uint32_t top=UINT32_C(1) << (bits-1u); return (v & top) ? -1-(int32_t)((top-1u) & ~v) : (int32_t)v; }
static nn2_oc_result_t large(nn2_oc_stats_t *s)
{ if (s->oversized_payloads != UINT32_MAX) ++s->oversized_payloads; return NN2_OC_TOO_LARGE; }
static bool valid_opt2(const nn2_oc_opt2_t *f)
{
    const bool engineering=(f->frame_flags & 4u)!=0u;
    if ((f->frame_flags & ~15u) || !f->sample_count ||
        (f->gate_config_id==0u && !(f->frame_flags & 4u))) return false;
    for (unsigned i=0;i<20u;++i) {
        const nn2_oc_slot_t *s=&f->slot[i];
        if (s->slot_index!=i || s->quality>4u || (s->reason & 0xF000u) ||
            (s->verified_mask & 0xF0u) || (s->witness_ain>3u && s->witness_ain!=255u) ||
            s->n_on_used>s->n_on_available || s->n_off_used>s->n_off_available) return false;
        if (engineering) {
            if (s->verified_mask || s->witness_ain!=255u || (s->reason & 0xC00u)) return false;
        } else if ((s->quality==0u || s->quality>=3u || s->reason!=0u) && s->verified_mask) return false;
        for (unsigned d=0;d<4u;++d) {
            const nn2_oc_detector_t *v=&s->det[d];
            if (v->det_flags & 0x80u) return false;
            if (engineering) {
                if (!(v->det_flags & 0x40u) || (v->det_flags & 0x3Cu) ||
                    s->n_on_used<2u || s->n_off_used<2u ||
                    v->sum_on==INT32_MIN || v->sum_off==INT32_MIN ||
                    v->sd_on_q4==UINT16_MAX || v->sd_off_q4==UINT16_MAX) return false;
            } else if (v->det_flags & 0x40u) return false;
            else if (!(s->verified_mask & (1u<<d))) {
                if (v->sum_on!=INT32_MIN || v->sum_off!=INT32_MIN ||
                    v->sd_on_q4!=UINT16_MAX || v->sd_off_q4!=UINT16_MAX) return false;
            } else if (s->n_on_used<2u || s->n_off_used<2u ||
                v->sum_on==INT32_MIN || v->sum_off==INT32_MIN ||
                v->sd_on_q4==UINT16_MAX || v->sd_off_q4==UINT16_MAX) return false;
        }
    }
    return true;
}
static bool valid_raw(const nn2_oc_raw_t *f)
{
    if (!f->sample_count || !(f->raw_flags & 1u) || (f->raw_flags & ~3u)) return false;
    for (unsigned i=0;i<f->sample_count;++i) {
        if (f->sample[i].phase_class>8u ||
            (f->sample[i].phase_class==0u ? f->sample[i].slot_index!=255u :
             f->sample[i].slot_index>19u)) return false;
        for (unsigned d=0;d<4u;++d)
            if (f->sample[i].ain[d]<-8388608 || f->sample[i].ain[d]>8388607) return false;
    }
    return true;
}
nn2_oc_result_t nn2_oc_encode_opt2(const nn2_oc_opt2_t *f, uint8_t *p,
    size_t cap, size_t *written, nn2_oc_stats_t *stats)
{
    if (!f || !p || !written || !stats) return NN2_OC_INVALID;
    if (!valid_opt2(f)) return NN2_OC_INVALID;
    if (cap<NN2_OPT2_PAYLOAD_BYTES) return NN2_OC_TRUNCATED;
    p[0]=1; p[1]=2; put(p+2,f->slot_plan_epoch,2); put(p+4,f->frame_sequence,4);
    put(p+8,f->first_sample_sequence,4); put(p+12,f->sample_count,2);
    put(p+14,f->frame_flags,2); p[16]=20; p[17]=4;
    put(p+18,f->geometry_map_id,2); put(p+20,f->gate_config_id,2); put(p+22,0,2);
    for (unsigned i=0;i<20u;++i) {
        const nn2_oc_slot_t *s=&f->slot[i]; uint8_t *q=p+24u+64u*i;
        q[0]=s->slot_index; q[1]=s->quality; put(q+2,s->reason,2);
        q[4]=s->n_on_used; q[5]=s->n_off_used; q[6]=s->n_on_available; q[7]=s->n_off_available;
        q[8]=s->on_commit_late; q[9]=s->off_commit_late; q[10]=s->witness_ain; q[11]=s->verified_mask;
        for (unsigned d=0;d<4u;++d) {
            const nn2_oc_detector_t *v=&s->det[d]; uint8_t *r=q+12u+13u*d;
            put(r,(uint32_t)v->sum_on,4); put(r+4,(uint32_t)v->sum_off,4);
            put(r+8,v->sd_on_q4,2); put(r+10,v->sd_off_q4,2); r[12]=v->det_flags;
        }
    }
    for (unsigned i=0;i<80u;++i) p[1304u+i]=f->stab_q[i];
    *written=NN2_OPT2_PAYLOAD_BYTES; return NN2_OC_OK;
}
nn2_oc_result_t nn2_oc_encode_raw(const nn2_oc_raw_t *f, uint8_t *p,
    size_t cap, size_t *written, nn2_oc_stats_t *stats)
{
    if (!f || !p || !written || !stats) return NN2_OC_INVALID;
    if (f->sample_count>100u) return large(stats);
    if (!valid_raw(f)) return NN2_OC_INVALID;
    size_t len=16u+14u*f->sample_count;
    if (cap<len) return NN2_OC_TRUNCATED;
    p[0]=1; p[1]=3; put(p+2,f->slot_plan_epoch,2); put(p+4,f->first_sample_sequence,4);
    put(p+8,f->sample_count,2); put(p+10,f->raw_flags,2); put(p+12,f->geometry_map_id,2); put(p+14,0,2);
    for (unsigned i=0;i<f->sample_count;++i) {
        uint8_t *q=p+16u+14u*i;
        for (unsigned d=0;d<4u;++d) put(q+3u*d,(uint32_t)f->sample[i].ain[d],3);
        q[12]=f->sample[i].phase_class; q[13]=f->sample[i].slot_index;
    }
    *written=len; return NN2_OC_OK;
}
nn2_oc_result_t nn2_oc_decode(const uint8_t *p, size_t len, nn2_oc_opt2_t *opt,
    nn2_oc_raw_t *raw, nn2_oc_stats_t *stats)
{
    if (!p || !stats) return NN2_OC_INVALID;
    if (len>NN2_RAW_PAYLOAD_MAX) return large(stats);
    if (len<2u) return NN2_OC_TRUNCATED;
    if (p[0]!=1u) return NN2_OC_INVALID;
    if (p[1]==2u) {
        nn2_oc_opt2_t f={0};
        if (!opt) return NN2_OC_INVALID;
        if (len>NN2_OPT2_PAYLOAD_BYTES) return large(stats);
        if (len<NN2_OPT2_PAYLOAD_BYTES) return NN2_OC_TRUNCATED;
        if (p[16]!=20u || p[17]!=4u || get(p+22,2)) return NN2_OC_INVALID;
        f.slot_plan_epoch=(uint16_t)get(p+2,2); f.frame_sequence=get(p+4,4);
        f.first_sample_sequence=get(p+8,4); f.sample_count=(uint16_t)get(p+12,2);
        f.frame_flags=(uint16_t)get(p+14,2); f.geometry_map_id=(uint16_t)get(p+18,2);
        f.gate_config_id=(uint16_t)get(p+20,2);
        for (unsigned i=0;i<20u;++i) {
            nn2_oc_slot_t *s=&f.slot[i]; const uint8_t *q=p+24u+64u*i;
            s->slot_index=q[0]; s->quality=q[1]; s->reason=(uint16_t)get(q+2,2);
            s->n_on_used=q[4]; s->n_off_used=q[5]; s->n_on_available=q[6]; s->n_off_available=q[7];
            s->on_commit_late=q[8]; s->off_commit_late=q[9]; s->witness_ain=q[10]; s->verified_mask=q[11];
            for (unsigned d=0;d<4u;++d) {
                nn2_oc_detector_t *v=&s->det[d]; const uint8_t *r=q+12u+13u*d;
                v->sum_on=signed_word(get(r,4),32); v->sum_off=signed_word(get(r+4,4),32);
                v->sd_on_q4=(uint16_t)get(r+8,2); v->sd_off_q4=(uint16_t)get(r+10,2); v->det_flags=r[12];
            }
        }
        for (unsigned i=0;i<80u;++i) f.stab_q[i]=p[1304u+i];
        if (!valid_opt2(&f)) return NN2_OC_INVALID;
        *opt=f; return NN2_OC_OK;
    }
    if (p[1]==3u) {
        nn2_oc_raw_t f={0};
        if (!raw) return NN2_OC_INVALID;
        if (len<16u) return NN2_OC_TRUNCATED;
        f.sample_count=(uint16_t)get(p+8,2);
        if (f.sample_count>100u) return large(stats);
        if (len!=16u+14u*f.sample_count) return NN2_OC_INVALID;
        if (get(p+14,2)) return NN2_OC_INVALID;
        f.slot_plan_epoch=(uint16_t)get(p+2,2); f.first_sample_sequence=get(p+4,4);
        f.raw_flags=(uint16_t)get(p+10,2); f.geometry_map_id=(uint16_t)get(p+12,2);
        for (unsigned i=0;i<f.sample_count;++i) {
            const uint8_t *q=p+16u+14u*i;
            for (unsigned d=0;d<4u;++d) f.sample[i].ain[d]=signed_word(get(q+3u*d,3),24);
            f.sample[i].phase_class=q[12]; f.sample[i].slot_index=q[13];
        }
        if (!valid_raw(&f)) return NN2_OC_INVALID;
        *raw=f; return NN2_OC_OK;
    }
    return NN2_OC_UNKNOWN_LAYOUT;
}
