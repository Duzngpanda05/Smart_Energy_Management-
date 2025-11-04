#include <stdio.h>
#include <math.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/adc.h"
#include "esp_adc_cal.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_http_client.h"

// TensorFlow Lite Micro
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"
#include "model_data.cc"  // model tflite

// ---------------- Configuration ----------------
#define WIFI_SSID "TP-Link_3964"
#define WIFI_PASS "56113297"

#define DEFAULT_VREF        1100
#define NO_OF_SAMPLES_I     2000
#define SAMPLE_DELAY_US     100
#define ADC_VOLTAGE_CHANNEL ADC1_CHANNEL_7
#define ADC_CURRENT_CHANNEL ADC1_CHANNEL_6
#define VOLT_CALIBRATION_FACTOR 676.643

static esp_adc_cal_characteristics_t adc_chars;

// TensorFlow Lite Micro
constexpr int kTensorArenaSize = 40 * 1024;
static uint8_t tensor_arena[kTensorArenaSize];
const char* labels[] = {"Fan", "Computer"};

// ---------------- Sensitivity ----------------
double get_sensitivity(double Vrms) {
    if (Vrms < 0.042) return 0.657;
    else if (Vrms >= 0.042 && Vrms <= 0.061) return 0.324;
    else if (Vrms > 0.061 && Vrms < 0.067) return 0.301;
    else if (Vrms >= 0.067 && Vrms < 0.069) return 0.265;
    else if (Vrms >= 0.069 && Vrms < 0.188) return 0.1666;
    else if (Vrms >= 0.188 && Vrms < 0.25) return 0.1584;
    else if (Vrms >= 0.25 && Vrms < 0.288) return 0.1393;
    else if (Vrms >= 0.288 && Vrms < 0.38) return 0.1318;
    else if (Vrms >= 0.38 && Vrms <= 0.4) return 0.129;
    else if (Vrms > 0.4 && Vrms <= 0.5) return 0.1255;
    else if (Vrms > 0.5 && Vrms <= 0.55) return 0.106;
    else if (Vrms > 0.55 && Vrms <= 0.79) return 0.101;
    else return 0.0907;
}

// ---------------- WiFi Init ----------------
void wifi_init(void) {
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    wifi_config_t wifi_config = {};
    strcpy((char*)wifi_config.sta.ssid, WIFI_SSID);
    strcpy((char*)wifi_config.sta.password, WIFI_PASS);

    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    esp_wifi_start();
    esp_wifi_connect();

    printf("Connecting to WiFi...\n");
    vTaskDelay(pdMS_TO_TICKS(5000));
    printf("WiFi connected!\n");
}

// ---------------- ADC Read ----------------
uint32_t read_adc_mv(adc1_channel_t channel) {
    uint32_t adc_reading = adc1_get_raw(channel);
    return esp_adc_cal_raw_to_voltage(adc_reading, &adc_chars);
}

// ---------------- HTTP POST ----------------
void http_post_data(float Vrms_current, float Vrms_sensor, float Vrms_grid,
                    float Irms, float P, float S, float PF,
                    const char* label, float p_fan, float p_comp) 
{
    char post_data[512];
    sprintf(post_data,
        "{\"Vrms_current\": %.4f, \"Vrms_sensor\": %.4f, \"Vrms_grid\": %.1f, "
        "\"Irms\": %.3f, \"P\": %.3f, \"S\": %.3f, \"PF\": %.3f, "
        "\"prediction\": \"%s\", \"p_fan\": %.2f, \"p_comp\": %.2f}",
        Vrms_current, Vrms_sensor, Vrms_grid,
        Irms, P, S, PF,
        label, p_fan, p_comp);

    esp_http_client_config_t config = {
        .url = "http://192.168.0.105:3000/data",
        .method = HTTP_METHOD_POST,
        .timeout_ms = 5000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, post_data, strlen(post_data));

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) printf("Data sent successfully\n");
    else printf("Error sending data: %s\n", esp_err_to_name(err));

    esp_http_client_cleanup(client);
}

// ---------------- Measurement Task ----------------
void measure_task(void *pvParameter) {
    // --- Offset calculation ---
    double offset_i = 0.0, offset_v = 0.0;
    for (int i = 0; i < NO_OF_SAMPLES_I; i++) {
        offset_i += read_adc_mv(ADC_CURRENT_CHANNEL);
        offset_v += read_adc_mv(ADC_VOLTAGE_CHANNEL);
        ets_delay_us(SAMPLE_DELAY_US);
    }
    offset_i /= NO_OF_SAMPLES_I;
    offset_v /= NO_OF_SAMPLES_I;
    printf("Offset measured: offset_i = %.2f mV, offset_v = %.2f mV\n", offset_i, offset_v);

    // --- Load TensorFlow model ---
    const tflite::Model* model = tflite::GetModel(nilm_model_tflite);
    tflite::MicroMutableOpResolver<10> resolver;
    resolver.AddFullyConnected();
    resolver.AddRelu();
    resolver.AddSoftmax();

    static tflite::MicroInterpreter interpreter(model, resolver, tensor_arena, kTensorArenaSize);
    interpreter.AllocateTensors();
    TfLiteTensor* input = interpreter.input(0);
    TfLiteTensor* output = interpreter.output(0);

    while (1) {
        double sum_i = 0.0, sum_v = 0.0, sum_p = 0.0;

        for (int i = 0; i < NO_OF_SAMPLES_I; i++) {
            double v_raw = read_adc_mv(ADC_VOLTAGE_CHANNEL);
            double i_raw = read_adc_mv(ADC_CURRENT_CHANNEL);

            double diff_v = v_raw - offset_v;
            double diff_i = i_raw - offset_i;

            if (fabs(diff_v) < 5) diff_v = 0;
            if (fabs(diff_i) < 5) diff_i = 0;

            sum_v += diff_v * diff_v;
            sum_i += diff_i * diff_i;

            double v_real = (diff_v / 1000.0) * VOLT_CALIBRATION_FACTOR;
            double Vrms_tmp = sqrt(sum_i / (i + 1)) / 1000.0;
            double sensitivity = get_sensitivity(Vrms_tmp);
            double i_real = (diff_i / 1000.0) / sensitivity;
            sum_p += v_real * i_real;

            ets_delay_us(SAMPLE_DELAY_US);
        }

        // --- RMS Calculation ---
        double Vrms_current = sqrt(sum_i / NO_OF_SAMPLES_I) / 1000.0;
        double Vrms_sensor  = sqrt(sum_v / NO_OF_SAMPLES_I) / 1000.0;
        double Vrms_grid    = Vrms_sensor * VOLT_CALIBRATION_FACTOR;
        double Irms         = Vrms_current / get_sensitivity(Vrms_current);
        double P            = fabs(sum_p / NO_OF_SAMPLES_I);
        double S            = Vrms_grid * Irms;
        double PF           = (S != 0) ? (P / S) : 0.0;

        printf("Measured: Vrms_grid=%.2f | Irms=%.3f | P=%.2f | S=%.2f | PF=%.2f\n",
               Vrms_grid, Irms, P, S, PF);

        // --- AI Prediction ---
            int predicted = -1;
            const char* label = "Unknown";
            float p_fan = 0.0f, p_comp = 0.0f;

            if (Irms < 0.08) {
                predicted = 2;
                label = "No Load";
            } else {
                // Dữ liệu gốc
                float x_raw[6] = {
                    (float)Vrms_current,
                    (float)Vrms_grid,
                    (float)Irms,
                    (float)P,
                    (float)S,
                    (float)PF
                };

                // === Bước 1: Tham số normalize (lấy từ Python training) ===
               // ---------------- Min-Max normalization parameters ----------------
                float X_min[6] = {0.04210, 207.1, 0.1300, 0.06100, 28.36, 0.002};
                float X_max[6] = {0.06670, 249.2, 0.2220, 16.557, 50.453, 0.3430};

                // Giả sử bạn đã đo x_raw từ cảm biến
                float x_norm[6];
                for (int i = 0; i < 6; i++) {
                    x_norm[i] = (x_raw[i] - X_min[i]) / (X_max[i] - X_min[i]);
                    if (x_norm[i] < 0.0f) x_norm[i] = 0.0f;   // clamp về 0
                    if (x_norm[i] > 1.0f) x_norm[i] = 1.0f;   // clamp về 1
                    input->data.f[i] = x_norm[i];             // gán cho input TensorFlow Lite
                }

                // === Bước 3: Dự đoán ===
                interpreter.Invoke();
                p_fan  = output->data.f[0];
                p_comp = output->data.f[1];
                predicted = (p_fan > p_comp) ? 0 : 1;
                label = labels[predicted];
            }

            printf("[AI] Prediction: %s | p_fan=%.3f | p_comp=%.3f\n", label, p_fan, p_comp);


        // --- Send data to backend ---
        http_post_data(Vrms_current, Vrms_sensor, Vrms_grid, Irms, P, S, PF,
                       label, p_fan, p_comp);

        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

// ---------------- Main ----------------
extern "C" void app_main(void) {
    nvs_flash_init();
    wifi_init();

    // --- ADC Init ---
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(ADC_VOLTAGE_CHANNEL, ADC_ATTEN_DB_11);
    adc1_config_channel_atten(ADC_CURRENT_CHANNEL, ADC_ATTEN_DB_11);
    esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_12,
                             DEFAULT_VREF, &adc_chars);

    printf("Starting measurement task...\n");
    xTaskCreatePinnedToCore(measure_task, "measure_task", 8192, NULL, 5, NULL, 1);
}
