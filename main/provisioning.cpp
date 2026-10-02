#include "provisioning.hpp"

#include <cstdio>
#include <cstring>

#include "esp_event.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "network_provisioning/manager.h"
#include "network_provisioning/scheme_ble.h"

namespace {
constexpr char TAG[] = "provisioning";
constexpr char kProofOfPossession[] = "myRetroScope";

portMUX_TYPE status_lock = portMUX_INITIALIZER_UNLOCKED;
provisioning_status_t make_initial_status()
{
    provisioning_status_t initial = {};
    std::snprintf(initial.wifi_state, sizeof(initial.wifi_state), "%s", "Starting");
    std::snprintf(initial.ssid, sizeof(initial.ssid), "%s", "Not configured");
    std::snprintf(initial.proof_of_possession, sizeof(initial.proof_of_possession), "%s",
                  kProofOfPossession);
    return initial;
}

provisioning_status_t status = make_initial_status();

void set_status_text(char *destination, size_t size, const char *value)
{
    std::snprintf(destination, size, "%s", value);
}

void set_wifi_state(const char *value)
{
    portENTER_CRITICAL(&status_lock);
    set_status_text(status.wifi_state, sizeof(status.wifi_state), value);
    portEXIT_CRITICAL(&status_lock);
}

void event_handler(void *, esp_event_base_t base, int32_t id, void *data)
{
    if (base == NETWORK_PROV_EVENT) {
        switch (id) {
        case NETWORK_PROV_START:
            portENTER_CRITICAL(&status_lock);
            status.ble_advertising = true;
            set_status_text(status.wifi_state, sizeof(status.wifi_state), "Awaiting Wi-Fi setup");
            portEXIT_CRITICAL(&status_lock);
            ESP_LOGI(TAG, "BLE provisioning started");
            break;
        case NETWORK_PROV_WIFI_CRED_RECV: {
            const auto *cfg = static_cast<wifi_sta_config_t *>(data);
            portENTER_CRITICAL(&status_lock);
            status.ble_advertising = false;
            set_status_text(status.ssid, sizeof(status.ssid), reinterpret_cast<const char *>(cfg->ssid));
            set_status_text(status.wifi_state, sizeof(status.wifi_state), "Connecting");
            portEXIT_CRITICAL(&status_lock);
            ESP_LOGI(TAG, "Received credentials for SSID: %s", cfg->ssid);
            break;
        }
        case NETWORK_PROV_WIFI_CRED_FAIL:
            set_wifi_state("Connection failed");
            ESP_LOGE(TAG, "Provisioning failed; check the Wi-Fi credentials");
            break;
        case NETWORK_PROV_WIFI_CRED_SUCCESS:
            set_wifi_state("Connected");
            ESP_LOGI(TAG, "Provisioning credentials accepted");
            break;
        case NETWORK_PROV_END:
            portENTER_CRITICAL(&status_lock);
            status.ble_advertising = false;
            portEXIT_CRITICAL(&status_lock);
            network_prov_mgr_deinit();
            break;
        default:
            break;
        }
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        set_wifi_state("Connecting");
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        portENTER_CRITICAL(&status_lock);
        status.wifi_connected = false;
        status.ip_address[0] = '\0';
        set_status_text(status.wifi_state, sizeof(status.wifi_state), "Reconnecting");
        portEXIT_CRITICAL(&status_lock);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const auto *event = static_cast<ip_event_got_ip_t *>(data);
        portENTER_CRITICAL(&status_lock);
        status.wifi_connected = true;
        set_status_text(status.wifi_state, sizeof(status.wifi_state), "Connected");
        std::snprintf(status.ip_address, sizeof(status.ip_address), IPSTR, IP2STR(&event->ip_info.ip));
        portEXIT_CRITICAL(&status_lock);
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
    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                    event_handler, nullptr), TAG, "Wi-Fi handler");

    network_prov_mgr_config_t config = {};
    config.scheme = network_prov_scheme_ble;
    config.scheme_event_handler = NETWORK_PROV_SCHEME_BLE_EVENT_HANDLER_FREE_BTDM;
    config.app_event_handler = NETWORK_PROV_EVENT_HANDLER_NONE;
    ESP_RETURN_ON_ERROR(network_prov_mgr_init(config), TAG, "provisioning manager");

    // Tab5's C6 factory image may contain this placeholder station profile.
    // The provisioning manager treats every nonempty SSID as user credentials,
    // which would otherwise prevent its BLE service from advertising.
    wifi_config_t wifi_config = {};
    ESP_RETURN_ON_ERROR(esp_wifi_get_config(WIFI_IF_STA, &wifi_config), TAG, "read Wi-Fi config");
    if (std::strcmp(reinterpret_cast<const char *>(wifi_config.sta.ssid), "M5Stack-Production") == 0) {
        ESP_LOGI(TAG, "Clearing C6 factory Wi-Fi profile");
        wifi_config_t empty_config = {};
        ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &empty_config), TAG, "clear Wi-Fi config");
        wifi_config = {};
    }

    uint8_t mac[6] = {};
    // The Wi-Fi station MAC belongs to the C6 and is not exposed through the
    // P4 host's esp_read_mac(). The P4 factory eFuse MAC is stable and gives
    // this host-side provisioning service a unique name.
    ESP_RETURN_ON_ERROR(esp_read_mac(mac, ESP_MAC_EFUSE_FACTORY), TAG, "read factory MAC");
    char service_name[sizeof("RetroScope_") + 6];
    std::snprintf(service_name, sizeof(service_name), "RetroScope_%02X%02X%02X", mac[3], mac[4], mac[5]);
    portENTER_CRITICAL(&status_lock);
    set_status_text(status.ble_name, sizeof(status.ble_name), service_name);
    set_status_text(status.proof_of_possession, sizeof(status.proof_of_possession), kProofOfPossession);
    if (wifi_config.sta.ssid[0] != '\0') {
        set_status_text(status.ssid, sizeof(status.ssid), reinterpret_cast<const char *>(wifi_config.sta.ssid));
    }
    portEXIT_CRITICAL(&status_lock);

    bool provisioned = false;
    ESP_RETURN_ON_ERROR(network_prov_mgr_is_wifi_provisioned(&provisioned), TAG, "provisioned state");
    if (provisioned) {
        ESP_LOGI(TAG, "Wi-Fi is already provisioned; BLE provisioning is not advertising");
        network_prov_mgr_deinit();
        return esp_wifi_start();
    }

    // Security 1 provides proof-of-possession authenticated encryption. Change
    // this per product/device before shipping rather than using a fleet secret.
    ESP_LOGI(TAG, "Provision with ESP BLE Provisioning: name=%s, PoP=%s", service_name, kProofOfPossession);
    return network_prov_mgr_start_provisioning(NETWORK_PROV_SECURITY_1, kProofOfPossession, service_name, nullptr);
}

void provisioning_get_status(provisioning_status_t *current_status)
{
    if (current_status == nullptr) {
        return;
    }
    portENTER_CRITICAL(&status_lock);
    *current_status = status;
    portEXIT_CRITICAL(&status_lock);
}
