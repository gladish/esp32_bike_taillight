idf_component_register(
  SRCS "led_renderer.cpp" "main.cpp"
  PRIV_REQUIRES "spi_flash" "esp_driver_gpio" "esp_driver_spi" "esp_timer"
  INCLUDE_DIRS "")
