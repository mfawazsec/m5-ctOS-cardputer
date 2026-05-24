idf_component_register(
    SRCS "cardputer_keyboard.cpp"
    INCLUDE_DIRS "."
    REQUIRES
        m5unified
        freertos
        log
)
