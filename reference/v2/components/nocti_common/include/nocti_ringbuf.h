// ============================================================
// File: components/nocti_common/include/nocti_ringbuf.h
// ============================================================
//
// Tujuan file ini
// - Ring buffer byte oriented untuk buffering ringan yang deterministik (tanpa malloc)
// - Cocok untuk util internal: parser, queue byte kecil, bridging antar modul sederhana
// - Catatan: nocti_stream memakai FreeRTOS Ringbuffer (xRingbufferCreate), bukan modul ini
//
// Catatan desain
// - Single-producer single-consumer (SPSC) tanpa mutex
// - Paling aman jika producer dan consumer berada di core yang sama
// - Jika beda core, pin task ke core yang sama atau tambahkan proteksi di level atas
//
// Batasan
// - Ini ring buffer byte stream, tidak ada batas record (record boundary)
// - Kalau butuh record boundary, pakai FreeRTOS Ringbuffer atau framing di level atas


#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t *buf;   // slash: pointer ke storage eksternal
    size_t   cap;   // slash: kapasitas total dalam byte
    size_t   head;  // slash: index tulis berikutnya
    size_t   tail;  // slash: index baca berikutnya
    bool     full;  // slash: pembeda full ketika head == tail
} nocti_ringbuf_t;

// ============================================================
// Init and reset
// ============================================================

// Init ring buffer dengan buffer eksternal
// return true jika sukses
bool   nocti_ringbuf_init(nocti_ringbuf_t *rb, uint8_t *storage, size_t capacity);

// Reset ring buffer (mengosongkan isi)
void   nocti_ringbuf_reset(nocti_ringbuf_t *rb);

// ============================================================
// Stats
// ============================================================

size_t nocti_ringbuf_capacity(const nocti_ringbuf_t *rb);
size_t nocti_ringbuf_size(const nocti_ringbuf_t *rb);
size_t nocti_ringbuf_free(const nocti_ringbuf_t *rb);

bool   nocti_ringbuf_empty(const nocti_ringbuf_t *rb);
bool   nocti_ringbuf_full(const nocti_ringbuf_t *rb);

// ============================================================
// IO basic
// ============================================================

// Tulis data ke ring buffer
// return jumlah byte yang berhasil ditulis (bisa < len jika tidak cukup ruang)
size_t nocti_ringbuf_write(nocti_ringbuf_t *rb, const void *data, size_t len);

// Tulis data harus utuh, kalau tidak cukup ruang maka gagal
// return true jika semua byte berhasil ditulis
bool   nocti_ringbuf_write_all(nocti_ringbuf_t *rb, const void *data, size_t len);

// Baca data dari ring buffer
// return jumlah byte yang berhasil dibaca
size_t nocti_ringbuf_read(nocti_ringbuf_t *rb, void *out, size_t len);

// Peek data tanpa menggeser tail
// return jumlah byte yang berhasil di-copy ke out
size_t nocti_ringbuf_peek(const nocti_ringbuf_t *rb, void *out, size_t len);

// ============================================================
// Optional helpers (berguna untuk streaming realtime)
// ============================================================

// Tulis data, kalau tidak cukup ruang maka drop data paling lama sampai cukup
// return jumlah byte yang berhasil ditulis (biasanya == len kalau rb->cap memadai)
// slash: ini cocok untuk producer realtime, daripada gagal terus saat buffer penuh
size_t nocti_ringbuf_write_drop_oldest(nocti_ringbuf_t *rb, const void *data, size_t len);

// Drop (buang) N byte dari sisi tail, tanpa membaca
// return jumlah byte yang benar benar ter-drop
size_t nocti_ringbuf_drop(nocti_ringbuf_t *rb, size_t len);

#ifdef __cplusplus
}
#endif
