// ============================================================
// File: components/nocti_common/nocti_ringbuf.c
// ============================================================
// Revisi fokus:
// 1) wrap index tanpa modulo (lebih ringan)
// 2) tambah API drop_oldest untuk producer realtime
// 3) tambah API drop untuk buang data tanpa read
// 4) guard state lebih ketat, tapi tetap SPSC dan ringan
// ============================================================

#include "nocti_ringbuf.h"

#include <string.h>

// ============================================================
// Internal helpers
// ============================================================

static inline bool rb_ok(const nocti_ringbuf_t *rb)
{
    // slash: validasi minimal supaya operasi tidak aneh
    return (rb && rb->buf && rb->cap >= 2);
}

static inline size_t rb_advance(const nocti_ringbuf_t *rb, size_t idx, size_t n)
{
    // slash: fast path tanpa modulo untuk kasus normal (n < cap)
    if (n < rb->cap) {
        size_t v = idx + n;
        if (v >= rb->cap) v -= rb->cap;
        return v;
    }

    // slash: slow path aman untuk n besar (misalnya ada caller salah kirim len)
    // slash: modulo jarang kepakai, tapi bikin fungsi robust
    n %= rb->cap;
    size_t v = idx + n;
    if (v >= rb->cap) v -= rb->cap;
    return v;
}

static inline size_t rb_contig_from_head(const nocti_ringbuf_t *rb)
{
    // slash: berapa byte kontigu yang bisa ditulis dari head sampai akhir buffer
    return rb->cap - rb->head;
}

static inline size_t rb_contig_from_tail(const nocti_ringbuf_t *rb)
{
    // slash: berapa byte kontigu yang bisa dibaca dari tail sampai akhir buffer
    return rb->cap - rb->tail;
}

// ============================================================
// Public: init and reset
// ============================================================

bool nocti_ringbuf_init(nocti_ringbuf_t *rb, uint8_t *storage, size_t capacity)
{
    if (!rb || !storage || capacity < 2) {
        return false; // slash: minimal 2 byte agar head dan tail meaningful
    }

    rb->buf  = storage;
    rb->cap  = capacity;
    rb->head = 0;
    rb->tail = 0;
    rb->full = false;

    return true;
}

void nocti_ringbuf_reset(nocti_ringbuf_t *rb)
{
    if (!rb) return;

    rb->head = 0;
    rb->tail = 0;
    rb->full = false;
}

// ============================================================
// Public: stats
// ============================================================

size_t nocti_ringbuf_capacity(const nocti_ringbuf_t *rb)
{
    return rb_ok(rb) ? rb->cap : 0;
}

bool nocti_ringbuf_empty(const nocti_ringbuf_t *rb)
{
    if (!rb_ok(rb)) return true;
    return (!rb->full && (rb->head == rb->tail));
}

bool nocti_ringbuf_full(const nocti_ringbuf_t *rb)
{
    if (!rb_ok(rb)) return false;
    return rb->full;
}

size_t nocti_ringbuf_size(const nocti_ringbuf_t *rb)
{
    if (!rb_ok(rb)) return 0;

    if (rb->full) return rb->cap;

    if (rb->head >= rb->tail) {
        return rb->head - rb->tail;
    }

    return rb->cap - (rb->tail - rb->head);
}

size_t nocti_ringbuf_free(const nocti_ringbuf_t *rb)
{
    if (!rb_ok(rb)) return 0;
    return rb->cap - nocti_ringbuf_size(rb);
}

// ============================================================
// Internal: chunk write and read
// ============================================================

static size_t rb_write_chunk(nocti_ringbuf_t *rb, const uint8_t *p, size_t n)
{
    // slash: tulis kontigu dari head sampai end, lalu wrap jika perlu
    size_t to_end = rb_contig_from_head(rb);
    size_t first  = (n < to_end) ? n : to_end;

    memcpy(&rb->buf[rb->head], p, first);
    rb->head = rb_advance(rb, rb->head, first);

    size_t remain = n - first;
    if (remain > 0) {
        memcpy(&rb->buf[rb->head], p + first, remain);
        rb->head = rb_advance(rb, rb->head, remain);
    }

    return n;
}

static size_t rb_read_chunk(nocti_ringbuf_t *rb, uint8_t *out, size_t n)
{
    // slash: baca kontigu dari tail sampai end, lalu wrap jika perlu
    size_t to_end = rb_contig_from_tail(rb);
    size_t first  = (n < to_end) ? n : to_end;

    memcpy(out, &rb->buf[rb->tail], first);
    rb->tail = rb_advance(rb, rb->tail, first);

    size_t remain = n - first;
    if (remain > 0) {
        memcpy(out + first, &rb->buf[rb->tail], remain);
        rb->tail = rb_advance(rb, rb->tail, remain);
    }

    return n;
}

// ============================================================
// Public: IO basic
// ============================================================

size_t nocti_ringbuf_write(nocti_ringbuf_t *rb, const void *data, size_t len)
{
    if (!rb_ok(rb) || !data || len == 0) return 0;

    size_t free = nocti_ringbuf_free(rb);
    size_t n = (len < free) ? len : free;
    if (n == 0) return 0;

    rb_write_chunk(rb, (const uint8_t *)data, n);

    // slash: kalau head menyusul tail, berarti full
    if (rb->head == rb->tail) rb->full = true;

    return n;
}

bool nocti_ringbuf_write_all(nocti_ringbuf_t *rb, const void *data, size_t len)
{
    if (!rb_ok(rb) || (!data && len > 0)) return false;
    if (len == 0) return true;

    size_t free = nocti_ringbuf_free(rb);
    if (len > free) return false; // slash: tidak cukup ruang, tidak menulis sebagian

    size_t wrote = nocti_ringbuf_write(rb, data, len);
    return (wrote == len);
}


size_t nocti_ringbuf_read(nocti_ringbuf_t *rb, void *out, size_t len)
{
    if (!rb_ok(rb) || !out || len == 0) return 0;

    size_t avail = nocti_ringbuf_size(rb);
    size_t n = (len < avail) ? len : avail;
    if (n == 0) return 0;

    // slash: begitu read, tidak mungkin full lagi
    rb->full = false;

    rb_read_chunk(rb, (uint8_t *)out, n);
    return n;
}

size_t nocti_ringbuf_peek(const nocti_ringbuf_t *rb, void *out, size_t len)
{
    if (!rb_ok(rb) || !out || len == 0) return 0;

    size_t avail = nocti_ringbuf_size(rb);
    size_t n = (len < avail) ? len : avail;
    if (n == 0) return 0;

    // slash: peek tidak mengubah state, jadi pakai tail temporer
    size_t tail = rb->tail;

    size_t to_end = rb->cap - tail;
    size_t first  = (n < to_end) ? n : to_end;

    memcpy(out, &rb->buf[tail], first);
    tail += first;
    if (tail >= rb->cap) tail -= rb->cap;

    size_t remain = n - first;
    if (remain > 0) {
        memcpy((uint8_t *)out + first, &rb->buf[tail], remain);
    }

    return n;
}

// ============================================================
// Public: optional helpers
// ============================================================

size_t nocti_ringbuf_drop(nocti_ringbuf_t *rb, size_t len)
{
    if (!rb_ok(rb) || len == 0) return 0;

    size_t avail = nocti_ringbuf_size(rb);
    size_t n = (len < avail) ? len : avail;
    if (n == 0) return 0;

    // slash: drop sama seperti read tapi tanpa copy
    rb->full = false;
    // slash: drop = geser tail saja, tanpa copy
    rb->tail = rb_advance(rb, rb->tail, n);

    return n;
}

size_t nocti_ringbuf_write_drop_oldest(nocti_ringbuf_t *rb, const void *data, size_t len)
{
    if (!rb_ok(rb) || !data || len == 0) return 0;

    // slash: kalau len lebih besar dari capacity, kita hanya bisa simpan tail end-nya
    // slash: ini menjaga determinisme, daripada loop drop terlalu lama
    if (len > rb->cap) {
        const uint8_t *p = (const uint8_t *)data;
        p += (len - rb->cap);
        data = p;
        len  = rb->cap;
    }

    size_t free = nocti_ringbuf_free(rb);
    if (len > free) {
        // slash: drop data lama secukupnya agar len muat
        size_t need = len - free;
        (void)nocti_ringbuf_drop(rb, need);
    }

    // slash: setelah drop, harusnya cukup ruang
    size_t wrote = nocti_ringbuf_write(rb, data, len);

    // slash: secara teori wrote harus sama dengan len
    return wrote;
}
