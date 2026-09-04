idf_component_register(
    SRCS
        "led_renderer.cpp"
        "lua_led_engine.cpp"
        "main.cpp"
    PRIV_REQUIRES
        "spi_flash"
        "esp_driver_gpio"
        "esp_driver_spi"
        "esp_timer"
        "lua"
    INCLUDE_DIRS ""
)

target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_23)

target_compile_definitions(${COMPONENT_LIB} PRIVATE
    LUA_32BITS
)

idf_component_get_property(lua_component_lib espressif__lua COMPONENT_LIB)
target_compile_definitions(${lua_component_lib} PRIVATE
    LUA_32BITS
)