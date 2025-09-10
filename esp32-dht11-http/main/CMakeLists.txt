set(extra_priv "")
if(CONFIG_MBEDTLS_CERTIFICATE_BUNDLE)
    list(APPEND extra_priv esp_crt_bundle)
endif()

idf_component_register(
    SRCS "main.c" "dht11.c"
    INCLUDE_DIRS "."
    REQUIRES esp_wifi esp_http_client nvs_flash json esp_event driver esp_timer
    PRIV_REQUIRES mbedtls ${extra_priv}
)