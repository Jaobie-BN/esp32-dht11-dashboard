idf_component_register(
    SRCS "main.c" "dht11.c"
    INCLUDE_DIRS "."
    REQUIRES esp_wifi esp_http_client nvs_flash json esp_event driver esp_timer
    EMBED_TXTFILES isrgrootx1.pem
)
