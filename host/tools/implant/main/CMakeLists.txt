idf_component_register(
    SRCS "../main.c"
    INCLUDE_DIRS "."
    REQUIRES esp_wifi esp_now nvs_flash driver freertos
)
