idf_component_register(
    SRCS "model_data.cc" "NILM.cpp"
    INCLUDE_DIRS "."
    REQUIRES esp-dsp esp-tflite-micro driver esp_adc_cal esp_wifi esp_event nvs_flash esp_http_client
)
