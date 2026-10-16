// ============================================================
// File: components/nocti_common/include/nocti_common.h
// ============================================================
//
// Tujuan file ini
// - Header agregator untuk semua tipe dan utilitas nocti_common
// - Modul lain cukup include file ini jika ingin akses semuanya
//
// Catatan
// - Kalau modul cuma butuh 1-2 header, lebih baik include spesifik
//   agar compile lebih cepat dan dependency lebih jelas

#pragma once

#include "nocti_types.h"
#include "nocti_err.h"
#include "nocti_version.h"
#include "nocti_time.h"
#include "nocti_status.h"
#include "nocti_packet.h"
#include "nocti_crc.h"
#include "nocti_ringbuf.h"
#include "nocti_log.h"
