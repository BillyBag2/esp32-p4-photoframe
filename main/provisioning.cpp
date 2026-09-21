#include "provisioning.hpp"

#include <cstdio>
#include <cstring>

#include "esp_event.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "network_provisioning/manager.h"
#include "network_provisioning/scheme_ble.h"

namespace {
constexpr char TAG[] = "provisioning";

void event_handler(void *, esp_event_base_t base, int32_t id, void *data)
{
    if (base == NETWORK_PROV_EVENT) {
        switch (id) {
        case NETWORK_PROV_START:
            ESP_LOGI(TAG, "BLE provisioning started");
            break;
        case NETWORK_PROV_WIFI_CRED_RECV: {
            const auto *cfg = static_cast<wifi_sta_config_t *>(data);
            ESP_LOGI(TAG, "Received credentials for SSID: %s", cfg->ssid);
            break;
        }
        case NETWORK_PROV_WIFI_CRED_FAIL:
            ESP_LOGE(TAG, "Provisioning failed; check the Wi-Fi credentials");
            break;
        case NETWORK_PROV_WIFI_CRED_SUCCESS:
            ESP_LOGI(TAG, "Provisioning credentials accepted");
            break;
        case NETWORK_PROV_END:
            network_prov_mgr_deinit();
            break;
        default:
            break;
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const auto *event = static_cast<ip_event_got_ip_t *>(data);
        ESP_LOGI(TAG, "Connected, IP: " IPSTR, IP2STR(&event->ip_info.ip));
    }
}
} // namespace

esp_err_t provisioning_start()
{
    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "esp_netif_init");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "event loop");
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t wifi_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&wifi_cfg), TAG, "esp_wifi_init");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(NETWORK_PROV_EVENT, ESP_EVENT_ANY_ID,
                                                    event_handler, nullptr), TAG, "prov handler");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                    event_handler, nullptr), TAG, "IP handler");

    network_prov_mgr_config_t config = {};
    config.scheme = network_prov_scheme_ble;
    config.scheme_event_handler = NETWORK_PROV_SCHEME_BLE_EVENT_HANDLER_FREE_BTDM;
    config.app_event_handler = NETWORK_PROV_EVENT_HANDLER_NONE;
    ESP_RETURN_ON_ERROR(network_prov_mgr_init(config), TAG, "provisioning manager");

    bool provisioned = false;
    ESP_RETURN_ON_ERROR(network_prov_mgr_is_wifi_provisioned(&provisioned), TAG, "provisioned state");
    if (provisioned) {
        network_prov_mgr_deinit();
        return esp_wifi_start();
    }

    uint8_t mac[6] = {};
    ESP_RETURN_ON_ERROR(esp_read_mac(mac, ESP_MAC_WIFI_STA), TAG, "read MAC");
    char service_name[16];
    std::snprintf(service_name, sizeof(service_name), "PROV_%02X%02X%02X", mac[3], mac[4], mac[5]);

    // Security 1 provides proof-of-possession authenticated encryption. Change
    // this per product/device before shipping rather than using a fleet secret.
    constexpr char pop[] = "tab5-frame";
    ESP_LOGI(TAG, "Provision with ESP BLE Provisioning: name=%s, PoP=%s", service_name, pop);
    return network_prov_mgr_start_provisioning(NETWORK_PROV_SECURITY_1, pop, service_name, nullptr);
}
