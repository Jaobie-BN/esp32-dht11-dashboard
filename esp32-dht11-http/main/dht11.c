#include "dht11.h"
#include "driver/gpio.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "esp_log.h"

static const char *TAG = "DHT11";

#define DHT_TIMEOUT_US  1000  // timeout step-level (us)

esp_err_t dht11_init(dht11_t *dev, gpio_num_t pin) {
    if (!dev) return ESP_ERR_INVALID_ARG;
    dev->pin = pin;
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << pin),
        .mode = GPIO_MODE_INPUT_OUTPUT_OD,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    esp_err_t err = gpio_config(&io);
    if (err != ESP_OK) return err;
    gpio_set_level(pin, 1); // idle = high
    return ESP_OK;
}

static inline int wait_for_level(gpio_num_t pin, int level, int timeout_us) {
    int t0 = (int)esp_timer_get_time();
    while (gpio_get_level(pin) != level) {
        if (((int)esp_timer_get_time() - t0) > timeout_us) return -1;
    }
    return (int)esp_timer_get_time() - t0;
}

esp_err_t dht11_read(dht11_t *dev, dht11_reading_t *out) {
    if (!dev || !out) return ESP_ERR_INVALID_ARG;
    gpio_num_t pin = dev->pin;

    // 1) Start signal: MCU pulls low >=18ms, then high ~20-40us
    gpio_set_level(pin, 0);
    esp_rom_delay_us(20000); // 20 ms
    gpio_set_level(pin, 1);
    esp_rom_delay_us(30);

    // 2) Sensor response: ~80us low, ~80us high
    if (wait_for_level(pin, 0, 100) < 0)   return ESP_ERR_TIMEOUT;
    if (wait_for_level(pin, 1, 100) < 0)   return ESP_ERR_TIMEOUT;

    // 3) Read 40 bits: each bit starts with ~50us low, then high: ~26-28us (0) หรือ ~70us (1)
    uint8_t data[5] = {0};
    for (int i = 0; i < 40; i++) {
        // low ~50us (start of bit)
        if (wait_for_level(pin, 0, DHT_TIMEOUT_US) < 0) return ESP_ERR_TIMEOUT;
        if (wait_for_level(pin, 1, DHT_TIMEOUT_US) < 0) return ESP_ERR_TIMEOUT;

        // measure high length
        int t = wait_for_level(pin, 0, 120); // DHT11 ~26us (0) or ~70us (1)
        if (t < 0) return ESP_ERR_TIMEOUT;

        // threshold ~50us
        int bit = (t > 50) ? 1 : 0;

        data[i / 8] <<= 1;
        data[i / 8] |= bit;
    }

    // 4) Checksum
    uint8_t sum = (uint8_t)((data[0] + data[1] + data[2] + data[3]) & 0xFF);
    if (sum != data[4]) {
        ESP_LOGW(TAG, "Checksum mismatch: got %u expected %u", data[4], sum);
        return ESP_ERR_INVALID_RESPONSE;
    }

    // DHT11 format: data[0]=RH int, data[2]=Temp int
    out->humidity_int    = data[0];
    out->temperature_int = data[2];
    return ESP_OK;
}
