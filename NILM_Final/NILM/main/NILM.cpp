#include <stdio.h>
#include <string.h>
#include <math.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_log.h"

// ESP-IDF WiFi & HTTP
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_http_client.h"
#include "driver/adc.h"
#include "esp_adc_cal.h"

// TensorFlow Lite Micro
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"
#include "model_data.cc"

#include "esp_dsp.h"
#include "dsps_fft2r.h"

// ================= CONFIG =================
#define TAG "ESP32_NILM"

#define WIFI_SSID "TP-Link_3964"
#define WIFI_PASS "56113297"

#define SERVER_IP   "192.168.0.105"   // IP máy chạy NodeJS
#define SERVER_PORT 3000
#define POST_URL    "http://" SERVER_IP ":3000/data"

#define NO_LOAD_THRESHOLD 0.025f

#define ADC_WIDTH        ADC_WIDTH_BIT_12
#define ADC_ATTEN        ADC_ATTEN_DB_11
#define DEFAULT_VREF     1100
#define ADC_V_CHANNEL    ADC1_CHANNEL_7
#define ADC_I_CHANNEL    ADC1_CHANNEL_6
#define VOLT_CAL_FACTOR  676.643f

#define SAMPLE_RATE_HZ   10000
#define CAPTURE_TIME_MS  50
#define NUM_SAMPLES      600

#define N_SAMPLES      NUM_SAMPLES
#define SAMPLE_RATE    SAMPLE_RATE_HZ

static float fft_input[N_SAMPLES * 2];
static float fft_mag[N_SAMPLES];

#define INPUT_SIZE   13
#define OUTPUT_SIZE  4
constexpr int kTensorArenaSize = 2 * 1024;

// ================= BUFFERS =================
static int16_t raw_v[NUM_SAMPLES];
static int16_t raw_i[NUM_SAMPLES];
static float v_wave[NUM_SAMPLES];
static float i_wave[NUM_SAMPLES];

static volatile int sample_index = 0;
static esp_timer_handle_t sample_timer;
static esp_adc_cal_characteristics_t adc_chars;

// ================= TFLITE =================
uint8_t tensor_arena[kTensorArenaSize];
tflite::MicroInterpreter* interpreter;
TfLiteTensor* input_tensor;
TfLiteTensor* output_tensor;

const char* labels[OUTPUT_SIZE] = {
    "Fan", "Laptop_D", "Adapter", "Laptop_C"
};

// ================= MIN–MAX (THAY BẰNG SỐ THẬT TỪ PYTHON) =================
float X_min[INPUT_SIZE] = {
0.090000, 0.027015, -31.330000, 12.790000, 0.203000, 2.026000, 1096.300000, 0.028023, 0.053922, 0.044539, 0.012178, 0.040595, 0.016876, };

float X_max[INPUT_SIZE] = {
0.230000, 0.069337, 26.920000, 50.760000, 1.118000, 8.628000, 11893.700000, 11.026630, 7.873805, 31.748216, 36.537747, 12.394069, 17.071069, };

// ================= WIFI =================
// static void wifi_init(void) {
//     esp_netif_init();
//     esp_event_loop_create_default();
//     esp_netif_create_default_wifi_sta();

//     wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
//     esp_wifi_init(&cfg);

//     wifi_config_t wifi_config = {};
//     strcpy((char*)wifi_config.sta.ssid, WIFI_SSID);
//     strcpy((char*)wifi_config.sta.password, WIFI_PASS);

//     esp_wifi_set_mode(WIFI_MODE_STA);
//     esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
//     esp_wifi_start();
//     esp_wifi_connect();

//     ESP_LOGI(TAG, "WiFi connected");
// }

// ================= TIMER ISR =================
static void IRAM_ATTR sample_timer_cb(void* arg) {
    if (sample_index < NUM_SAMPLES) {
        raw_v[sample_index] = adc1_get_raw(ADC_V_CHANNEL);
        raw_i[sample_index] = adc1_get_raw(ADC_I_CHANNEL);
        sample_index++;
    }
}

double get_sensitivity(double Vrms) {
    if (Vrms < 0.022) return 0.657;
    else if (Vrms >= 0.022 && Vrms <= 0.08) return 0.301;
    else if (Vrms > 0.08 && Vrms < 0.188) return 0.1666;
    else if (Vrms >= 0.188 && Vrms < 0.25) return 0.1584;
    else if (Vrms >= 0.25 && Vrms < 0.288) return 0.1393;
    else if (Vrms >= 0.288 && Vrms < 0.38) return 0.1318;
    else if (Vrms >= 0.38 && Vrms <= 0.4) return 0.129;
    else if (Vrms > 0.4 && Vrms <= 0.5) return 0.1255;
    else if (Vrms > 0.5 && Vrms <= 0.55) return 0.106;
    else if (Vrms > 0.55 && Vrms <= 0.79) return 0.101;
    else return 0.0907;
}

// ================= ADC CONVERT =================
static void convert_and_remove_offset(void) {
    float sum_v = 0, sum_i = 0;

    for (int i = 0; i < NUM_SAMPLES; i++) {
        float mv_v = esp_adc_cal_raw_to_voltage(raw_v[i], &adc_chars);
        float mv_i = esp_adc_cal_raw_to_voltage(raw_i[i], &adc_chars);

        v_wave[i] = mv_v * 0.001f * VOLT_CAL_FACTOR;
        i_wave[i] = mv_i * 0.001f;

        sum_v += v_wave[i];
        sum_i += i_wave[i];
    }

    float off_v = sum_v / NUM_SAMPLES;
    float off_i = sum_i / NUM_SAMPLES;

    for (int i = 0; i < NUM_SAMPLES; i++) {
        v_wave[i] -= off_v;
        i_wave[i] -= off_i;
    }
}

float get_mag_at_freq(float target_freq) {
    int center_bin = (int)(target_freq * N_SAMPLES / SAMPLE_RATE);
    if(center_bin <= 0) center_bin = 1;
    if(center_bin >= N_SAMPLES/2) center_bin = (N_SAMPLES/2) - 1;

    float max_val = 0.0f;
    int window = 2;

    for (int k = center_bin - window; k <= center_bin + window; k++) {
        float val = fft_mag[k];
        if (val > max_val) max_val = val;
    }
    return max_val;
}

// ================= FEATURE EXTRACTION =================
static void compute_feature(
    float *Vrms,
    float *Irms_voltage,
    float *Irms,
    float *P, float *Q,
    float *Ipeak, float *CF,
    float *max_di_dt,
    float *H85, float *H100,
    float *H150, float *H170,
    float *H250, float *H350
){    
                                
    const double dt = 1.0 / SAMPLE_RATE_HZ;
    double sum_i2_adc = 0;

    for (int i = 0; i < NUM_SAMPLES; i++) {
        double i_adc = i_wave[i];
        sum_i2_adc += i_adc * i_adc;
         fft_input[i*2 + 0] = i_wave[i]; 
        fft_input[i*2 + 1] = 0;
    }

    *Irms_voltage = sqrt(sum_i2_adc / NUM_SAMPLES);

    double sens = get_sensitivity(*Irms_voltage);
    if (sens < 1e-6) sens = 1e-6;

    static float i_wave_scaled[NUM_SAMPLES];

    for (int i = 0; i < NUM_SAMPLES; i++){
        i_wave_scaled[i] = i_wave[i] / sens;
    }

    double sum_v2 = 0, sum_i2 = 0, sum_p = 0;
    double i_peak = 0, max_didt = 0;

    for (int i = 0; i < NUM_SAMPLES; i++) {
        sum_v2 += v_wave[i] * v_wave[i];
        sum_i2 += i_wave_scaled[i] * i_wave_scaled[i];
        sum_p  += v_wave[i] * i_wave_scaled[i];

        if (fabs(i_wave_scaled[i]) > i_peak)
            i_peak = fabs(i_wave_scaled[i]);

        if (i > 0) {
            double di_dt = fabs(i_wave_scaled[i] - i_wave_scaled[i - 1]) / dt;
            if (di_dt > max_didt)
                max_didt = di_dt;
        }
    }

    *Vrms = sqrt(sum_v2 / NUM_SAMPLES);
    *Irms = sqrt(sum_i2 / NUM_SAMPLES);
    *P    = fabs(sum_p / NUM_SAMPLES);

    double S = (*Vrms) * (*Irms);
    *Q = sqrt(fabs(S * S - (*P) * (*P)));

    *Ipeak     = i_peak;
    *CF        = i_peak / (*Irms);
    *max_di_dt = max_didt;

    dsps_fft2r_init_fc32(NULL, N_SAMPLES);
    dsps_fft2r_fc32(fft_input, N_SAMPLES);
    dsps_bit_rev_fc32(fft_input, N_SAMPLES);

// Magnitude
for (int i = 0; i < N_SAMPLES/2; i++) {
    float re = fft_input[i*2];
    float im = fft_input[i*2 + 1];
    fft_mag[i] = sqrtf(re*re + im*im) * 2.0f / N_SAMPLES;
}


float mag50 = get_mag_at_freq(50.0f);
if (mag50 < 1e-6f) mag50 = 1e-6f;

*H85  = get_mag_at_freq(85.0f)  / mag50;
*H100 = get_mag_at_freq(100.0f) / mag50;
*H150 = get_mag_at_freq(150.0f) / mag50;
*H170 = get_mag_at_freq(170.0f) / mag50;
*H250 = get_mag_at_freq(250.0f) / mag50;
*H350 = get_mag_at_freq(350.0f) / mag50;

}
// ================= MLP INIT =================
void mlp_init() {
const tflite::Model* model =
    tflite::GetModel(load_classifier_tflite);

    static tflite::MicroMutableOpResolver<3> resolver;
    resolver.AddFullyConnected();
    resolver.AddRelu();
    resolver.AddSoftmax();

    static tflite::MicroInterpreter static_interpreter(
        model, resolver, tensor_arena, kTensorArenaSize
    );

    interpreter = &static_interpreter;
    interpreter->AllocateTensors();

    input_tensor = interpreter->input(0);
    output_tensor = interpreter->output(0);

    ESP_LOGI(TAG, "MLP initialized");
}

// static void send_to_backend(float Irms, float Vrms, float P, float Q, const char* label)
// {
//     char post_data[256];

//     snprintf(post_data, sizeof(post_data),
//         "{"
//         "\"Irms\":%.3f,"
//         "\"Vrms\":%.1f,"
//         "\"P\":%.2f,"
//         "\"Q\":%.2f,"
//         "\"label\":\"%s\""
//         "}",
//         Irms, Vrms, P, Q, label
//     );

//     esp_http_client_config_t config = {
//         .url = POST_URL,
//         .method = HTTP_METHOD_POST,
//         .timeout_ms = 3000,
//     };

//     esp_http_client_handle_t client = esp_http_client_init(&config);

//     esp_http_client_set_header(client, "Content-Type", "application/json");
//     esp_http_client_set_post_field(client, post_data, strlen(post_data));

//     esp_err_t err = esp_http_client_perform(client);
//     if (err == ESP_OK) {
//         ESP_LOGI(TAG, "Data sent to backend");
//     } else {
//         ESP_LOGE(TAG, "HTTP POST failed: %s", esp_err_to_name(err));
//     }

//     esp_http_client_cleanup(client);
// }


// ================= MAIN TASK =================
void measure_task(void* arg) {
    while (1) {
        int idx = -1;
        sample_index = 0;
        esp_timer_start_periodic(sample_timer, 1000000 / SAMPLE_RATE_HZ);
        while (sample_index < NUM_SAMPLES) vTaskDelay(1);
        esp_timer_stop(sample_timer);

        convert_and_remove_offset();

float Vrms, Irms_voltage, Irms, P, Q;
float Ipeak, CF, max_di_dt;
float H85, H100, H150, H170, H250, H350;      

compute_feature(&Vrms, &Irms_voltage, &Irms,
                &P, &Q,
                &Ipeak, &CF,
                &max_di_dt,
                &H85, &H100, &H150,
                &H170, &H250, &H350);

        if (Irms_voltage < NO_LOAD_THRESHOLD) {
            ESP_LOGI(TAG, "[AI] No_load");
        } else {

float features[INPUT_SIZE] = {
    Irms,
    Irms_voltage,
    P,
    Q,
    Ipeak,
    CF,
    max_di_dt,
    H85,
    H100,
    H150,
    H170,
    H250,
    H350
};

            float* in = input_tensor->data.f;

            for (int i = 0; i < INPUT_SIZE; i++) {
                float denom = X_max[i] - X_min[i];
                if (denom < 1e-6f) denom = 1e-6f;

                float x = (features[i] - X_min[i]) / denom;
                if (x < 0) x = 0;
                if (x > 1) x = 1;

                in[i] = x;
            }

            interpreter->Invoke();

            int idx = 0;
            float best = output_tensor->data.f[0];
            for (int i = 1; i < OUTPUT_SIZE; i++) {
                if (output_tensor->data.f[i] > best) {
                    best = output_tensor->data.f[i];
                    idx = i;
                }
            }

            ESP_LOGI(TAG, "[AI] %s (%.3f)", labels[idx], best);
        }

    ESP_LOGI(TAG,
 "Irms=%.3f Vrms=%.1f P=%.1f Q=%.1f CF=%.2f dI/dt=%.0f "
 "H85=%.4f H100=%.4f H150=%.4f H170=%.4f H250=%.4f H350=%.4f",
 Irms, Vrms, P, Q, CF, max_di_dt,
 H85, H100, H150, H170, H250, H350);

    //send_to_backend(Irms, Vrms, P, Q, labels[idx]);

    vTaskDelay(pdMS_TO_TICKS(3000));
 
    }
}

// ================= APP MAIN =================
extern "C" void app_main() {
    nvs_flash_init();
    //wifi_init();

    adc1_config_width(ADC_WIDTH);
    adc1_config_channel_atten(ADC_V_CHANNEL, ADC_ATTEN);
    adc1_config_channel_atten(ADC_I_CHANNEL, ADC_ATTEN);
    esp_adc_cal_characterize(
        ADC_UNIT_1, ADC_ATTEN, ADC_WIDTH, DEFAULT_VREF, &adc_chars
    );

    esp_timer_create_args_t timer_args = {};
    timer_args.callback = &sample_timer_cb;
    esp_timer_create(&timer_args, &sample_timer);

    mlp_init();

    xTaskCreatePinnedToCore(
        measure_task, "measure_task", 8192, NULL, 5, NULL, 1
    );
}
