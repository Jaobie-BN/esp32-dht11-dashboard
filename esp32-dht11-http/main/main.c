#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_system.h"
#include "esp_log.h"
#include "esp_err.h"

#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_event.h"

#include "esp_http_client.h"
#include "cJSON.h"

#include "dht11.h" // ไดรเวอร์ DHT11 ของคุณ

#include "esp_sntp.h"
#if CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
#include "esp_crt_bundle.h"
#endif

#define TAG "APP"

// ====== ค่าจาก menuconfig ======
#define WIFI_SSID CONFIG_APP_WIFI_SSID
#define WIFI_PASS CONFIG_APP_WIFI_PASS
#define API_URL CONFIG_APP_API_URL // https://<your>.onrender.com/api/readings
#define API_KEY CONFIG_APP_API_KEY
#define DEVICE_ID CONFIG_APP_DEVICE_ID
#define POST_INTERVAL_MS (CONFIG_APP_POST_INTERVAL_SEC * 1000)

// ====== Root CA ฝังมาจาก EMBED_TXTFILES ======
extern const uint8_t isrgrootx1_pem_start[] asm("_binary_isrgrootx1_pem_start");
extern const uint8_t isrgrootx1_pem_end[] asm("_binary_isrgrootx1_pem_end");

// ====== Wi-Fi ======
static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START)
    {
        esp_wifi_connect();
    }
    else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED)
    {
        ESP_LOGW(TAG, "WiFi disconnected, retrying...");
        esp_wifi_connect();
    }
    else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *e = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&e->ip_info.ip));
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static void wifi_init_sta(void)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    wifi_config_t w = {0};
    strncpy((char *)w.sta.ssid, WIFI_SSID, sizeof(w.sta.ssid));
    strncpy((char *)w.sta.password, WIFI_PASS, sizeof(w.sta.password));
    w.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &w));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "Connecting to WiFi: %s", WIFI_SSID);
}

static void sntp_sync_blocking(void)
{
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "pool.ntp.org");
    sntp_init();

    time_t now = 0;
    struct tm ti = {0};
    for (int i = 0; i < 15 && ti.tm_year < (2016 - 1900); ++i)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
        time(&now);
        localtime_r(&now, &ti);
    }
    ESP_LOGI(TAG, "Time sync: %s", asctime(&ti));
}

// ====== HTTP POST ======
static esp_err_t http_post_reading(int temp_c, int hum_pct)
{
    // ใช้ใบรับรองที่ฝัง (.cert_pem) — อย่าใส่ .crt_bundle_attach
    esp_http_client_config_t cfg = {
        .url = API_URL,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 10000,
    #if CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
        .crt_bundle_attach = esp_crt_bundle_attach,
    #else
        .cert_pem = (const char *)isrgrootx1_pem_start,
    #endif
    };

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client)
        return ESP_FAIL;

    // headers
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, "X-API-Key", API_KEY);

    // body
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "deviceId", DEVICE_ID);
    cJSON_AddNumberToObject(root, "temperature", temp_c);
    cJSON_AddNumberToObject(root, "humidity", hum_pct);
    char *payload = cJSON_PrintUnformatted(root);

    esp_http_client_set_post_field(client, payload, strlen(payload));
    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK)
    {
        int code = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "POST status=%d", code);
        char buf[128];
        int n = esp_http_client_read(client, buf, sizeof(buf) - 1);
        if (n > 0)
        {
            buf[n] = 0;
            ESP_LOGI(TAG, "Response: %s", buf);
        }
    }
    else
    {
        ESP_LOGE(TAG, "POST failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    cJSON_free(payload);
    cJSON_Delete(root);
    return err;
}

void app_main(void)
{
    // NVS + Wi-Fi
    ESP_ERROR_CHECK(nvs_flash_init());
    wifi_init_sta();

    // รอเชื่อม Wi-Fi
    while (!(xEventGroupGetBits(s_wifi_event_group) & WIFI_CONNECTED_BIT))
    {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    ESP_LOGI(TAG, "WiFi connected.");
    sntp_sync_blocking();

    // DHT11 @ GPIO4
    dht11_t dev;
    ESP_ERROR_CHECK(dht11_init(&dev, (gpio_num_t)4));
    ESP_LOGI(TAG, "DHT11 on GPIO 4");

    while (1)
    {
        dht11_reading_t r;
        if (dht11_read(&dev, &r) == ESP_OK)
        {
            printf("Temperature: %d °C, Humidity: %d %%\n", r.temperature_int, r.humidity_int);

            // retry 2 ครั้ง กันพลาดตอน Render cold-start
            for (int i = 0; i < 2; ++i)
            {
                if (http_post_reading(r.temperature_int, r.humidity_int) == ESP_OK)
                    break;
                vTaskDelay(pdMS_TO_TICKS(1500));
            }
        }
        else
        {
            printf("DHT11 read failed\n");
        }
        vTaskDelay(pdMS_TO_TICKS(POST_INTERVAL_MS)); // >= 2s สำหรับ DHT11
    }
}
