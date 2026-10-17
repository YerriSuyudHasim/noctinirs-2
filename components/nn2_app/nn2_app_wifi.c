#include "nn2_app_wifi.h"

#include <string.h>

static int store_ready(const nn2_port_wifi_cred_store_t *store)
{
    return store != NULL && store->load != NULL && store->save != NULL;
}

nn2_status_t nn2_app_wifi_provision_if_needed(const nn2_app_wifi_provision_deps_t *deps,
                                              nn2_wifi_credentials_t *out)
{
    nn2_wifi_credentials_t loaded;
    nn2_status_t status;

    if (deps == NULL || out == NULL || !store_ready(deps->store) || deps->console == NULL ||
        deps->console->read_once == NULL) {
        return NN2_ERR_INVALID_ARG;
    }

    nn2_wifi_cred_wipe(&loaded);
    status = deps->store->load(deps->store->ctx, &loaded);
    if (status == NN2_OK) {
        status = nn2_wifi_cred_validate(&loaded);
        if (status == NN2_OK) {
            *out = loaded;
            nn2_wifi_cred_wipe(&loaded);
            return NN2_OK;
        }
    } else if (status == NN2_ERR_NVS_CORRUPT) {
        if (deps->store->clear != NULL) {
            (void)deps->store->clear(deps->store->ctx);
        }
    } else if (status != NN2_ERR_MISSING && status != NN2_ERR_INVALID) {
        nn2_wifi_cred_wipe(&loaded);
        return status;
    }
    nn2_wifi_cred_wipe(&loaded);

    status = deps->console->read_once(deps->console->ctx, &loaded, deps->console_timeout_ms);
    if (status != NN2_OK) {
        nn2_wifi_cred_wipe(&loaded);
        return status;
    }
    status = nn2_wifi_cred_validate(&loaded);
    if (status != NN2_OK) {
        nn2_wifi_cred_wipe(&loaded);
        return status;
    }
    status = deps->store->save(deps->store->ctx, &loaded);
    if (status != NN2_OK) {
        nn2_wifi_cred_wipe(&loaded);
        return status;
    }
    *out = loaded;
    nn2_wifi_cred_wipe(&loaded);
    return NN2_OK;
}

nn2_status_t nn2_app_wifi_connect_from_creds(const nn2_port_wifi_station_t *station,
                                             const nn2_wifi_credentials_t *creds)
{
    nn2_status_t status;

    if (station == NULL || station->connect == NULL || creds == NULL) {
        return NN2_ERR_INVALID_ARG;
    }
    status = nn2_wifi_cred_validate(creds);
    if (status != NN2_OK) {
        return status;
    }
    return station->connect(station->ctx, creds);
}

nn2_status_t nn2_app_wifi_on_link_event(const nn2_port_wifi_station_t *station,
                                        nn2_wifi_link_t link,
                                        nn2_wifi_sta_status_t *status_out)
{
    if (station == NULL || status_out == NULL) {
        return NN2_ERR_INVALID_ARG;
    }
    if (link == NN2_WIFI_LINK_DOWN) {
        status_out->link = NN2_WIFI_LINK_DOWN;
        status_out->ipv4_addr = 0u;
        if (station->schedule_reconnect == NULL) {
            return NN2_ERR_INVALID_ARG;
        }
        return station->schedule_reconnect(station->ctx);
    }
    if (link == NN2_WIFI_LINK_UP) {
        status_out->link = NN2_WIFI_LINK_UP;
        return NN2_OK;
    }
    return NN2_ERR_INVALID_ARG;
}
