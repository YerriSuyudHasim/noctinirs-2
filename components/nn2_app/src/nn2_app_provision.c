#include "nn2_app_provision.h"
#include <string.h>

static int nibble(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}
static bool unhex(const char *p, size_t n, char *out, size_t max)
{
    if (!n || n % 2u || n / 2u > max) {
        return false;
    }
    for (size_t i = 0; i < n; i += 2u) {
        int a = nibble(p[i]), b = nibble(p[i + 1]);
        if (a < 0 || b < 0 || !(a | b)) {
            return false;
        }
        out[i / 2u] = (char)((unsigned)a * 16u + (unsigned)b);
    }
    return true;
}
bool nn2_app_network_config_valid(const nn2_network_config_t *v)
{
    if (!v || v->version != 1 || v->reserved || !v->ssid_length || v->ssid_length > 32
        || v->password_length < 8 || v->password_length > 63 || v->ssid[v->ssid_length]
        || v->password[v->password_length]) {
        return false;
    }
    for (unsigned i = 0; i < v->ssid_length; ++i) {
        if (!v->ssid[i]) {
            return false;
        }
    }
    for (unsigned i = 0; i < v->password_length; ++i) {
        if ((unsigned char)v->password[i] < 32 || (unsigned char)v->password[i] > 126) {
            return false;
        }
    }
    return true;
}
static void wipe(void *p, size_t n)
{
    volatile unsigned char *q = p;
    while (n--) {
        *q++ = 0;
    }
}
nn2_status_t nn2_app_provision(
    const char *line, size_t n, const nn2_port_network_config_t *port, nn2_network_config_t *out)
{
    if (!line || !port || !port->save || !out || n > 196 || n < 5 || memcmp(line, "WIFI ", 5)) {
        return NN2_ERR_INVALID_ARG;
    }
    size_t split = 5;
    while (split < n && line[split] != ' ') {
        ++split;
    }
    if (split == n) {
        return NN2_ERR_INVALID_ARG;
    }
    nn2_network_config_t v = { .version = 1 };
    bool ok = unhex(line + 5, split - 5, v.ssid, 32)
        && unhex(line + split + 1, n - split - 1, v.password, 63);
    v.ssid_length = (uint8_t)((split - 5) / 2);
    v.password_length = (uint8_t)((n - split - 1) / 2);
    nn2_status_t result = NN2_ERR_INVALID_ARG;
    if (ok && nn2_app_network_config_valid(&v)) {
        result = port->save(port->ctx, &v);
        if (result == NN2_OK) {
            *out = v;
        }
    }
    wipe(&v, sizeof(v));
    return result;
}
