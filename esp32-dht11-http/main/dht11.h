#pragma once
#include "esp_err.h"
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    gpio_num_t pin;
} dht11_t;

typedef struct {
    int temperature_int; // องศาเซลเซียส (DHT11 = ความละเอียด 1°C)
    int humidity_int;    // %
} dht11_reading_t;

esp_err_t dht11_init(dht11_t *dev, gpio_num_t pin);
esp_err_t dht11_read(dht11_t *dev, dht11_reading_t *out);

#ifdef __cplusplus
}
#endif
