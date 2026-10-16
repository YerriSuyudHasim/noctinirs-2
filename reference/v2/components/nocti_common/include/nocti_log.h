// ============================================================
// File: components/nocti_common/include/nocti_log.h
// ============================================================
//
// Tujuan file ini
// - Wrapper log konsisten untuk semua komponen NoctiNIRS
//
// Catatan desain
// - Default: pakai ESP_LOG jika tersedia
// - Bisa dimatikan log via NOCTI_LOG_ENABLE
//
// Cara pakai
// - NOCTI_LOGI("TAG", "hello %d", x);
// - NOCTI_LOGW("TAG", "warn");
// - NOCTI_LOGE("TAG", "err");

#pragma once

// slash: user bisa override dari build system
#ifndef NOCTI_LOG_ENABLE
#define NOCTI_LOG_ENABLE 1
#endif

#if NOCTI_LOG_ENABLE

// slash: jika ESP-IDF tersedia, pakai ESP_LOGx
#ifdef ESP_PLATFORM
#include "esp_log.h"
#define NOCTI_LOGI(tag, fmt, ...) ESP_LOGI(tag, fmt, ##__VA_ARGS__)
#define NOCTI_LOGW(tag, fmt, ...) ESP_LOGW(tag, fmt, ##__VA_ARGS__)
#define NOCTI_LOGE(tag, fmt, ...) ESP_LOGE(tag, fmt, ##__VA_ARGS__)
#else
// slash: fallback non-ESP (misal unit test host side)
#include <stdio.h>
#define NOCTI_LOGI(tag, fmt, ...) do { printf("[I][%s] " fmt "\n", tag, ##__VA_ARGS__); } while (0)
#define NOCTI_LOGW(tag, fmt, ...) do { printf("[W][%s] " fmt "\n", tag, ##__VA_ARGS__); } while (0)
#define NOCTI_LOGE(tag, fmt, ...) do { printf("[E][%s] " fmt "\n", tag, ##__VA_ARGS__); } while (0)
#endif

#else

// slash: log dimatikan
#define NOCTI_LOGI(tag, fmt, ...) do { (void)(tag); } while (0)
#define NOCTI_LOGW(tag, fmt, ...) do { (void)(tag); } while (0)
#define NOCTI_LOGE(tag, fmt, ...) do { (void)(tag); } while (0)

#endif
