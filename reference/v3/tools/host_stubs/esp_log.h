#pragma once
#include <stdio.h>

typedef enum {
    ESP_LOG_NONE = 0,
    ESP_LOG_ERROR = 1,
    ESP_LOG_WARN = 2,
    ESP_LOG_INFO = 3,
    ESP_LOG_DEBUG = 4,
    ESP_LOG_VERBOSE = 5,
} esp_log_level_t;

static inline void esp_log_set_level_master(esp_log_level_t level)
{
    (void)level;
}

static inline esp_log_level_t esp_log_get_level_master(void)
{
    return ESP_LOG_INFO;
}

static inline esp_log_level_t esp_log_get_default_level(void)
{
    return ESP_LOG_INFO;
}

static inline void esp_log_level_set(const char *tag, esp_log_level_t level)
{
    (void)tag;
    (void)level;
}

static inline esp_log_level_t esp_log_level_get(const char *tag)
{
    (void)tag;
    return ESP_LOG_INFO;
}

#define ESP_LOGI(tag,fmt,...) do { if (0) fprintf(stderr,"%s " fmt "\n",(tag),##__VA_ARGS__); } while(0)
#define ESP_LOGW(tag,fmt,...) do { if (0) fprintf(stderr,"%s " fmt "\n",(tag),##__VA_ARGS__); } while(0)
#define ESP_LOGE(tag,fmt,...) do { if (0) fprintf(stderr,"%s " fmt "\n",(tag),##__VA_ARGS__); } while(0)
