// ============================================================
// File: components/nocti_common/include/nocti_version.h
// ============================================================
//
// Tujuan file ini
// - Identitas firmware NoctiNIRS
// - Dipakai oleh BLE INFO, log, dan metadata session
//
// Catatan
// - Jangan pakai git di sini dulu, cukup macro
// - Bisa ditingkatkan nanti kalau perlu

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// ------------------------------------------------------------
// Versi firmware utama
// ------------------------------------------------------------

#define NOCTI_FW_NAME        "NoctiNIRS"
#define NOCTI_FW_VERSION_MAJ 0   // slash: major version (breaking change)
#define NOCTI_FW_VERSION_MIN 16  // firmware project revision v16
#define NOCTI_FW_VERSION_PAT 0   // slash: patch (bugfix)

// ------------------------------------------------------------
// Helper string (opsional)
// ------------------------------------------------------------

#define NOCTI_STR_HELPER(x)  #x
#define NOCTI_STR(x)         NOCTI_STR_HELPER(x)

// Contoh hasil untuk revisi ini: "0.16.0"
#define NOCTI_FW_VERSION_STR \
    NOCTI_STR(NOCTI_FW_VERSION_MAJ) "." \
    NOCTI_STR(NOCTI_FW_VERSION_MIN) "." \
    NOCTI_STR(NOCTI_FW_VERSION_PAT)

#ifdef __cplusplus
}
#endif
