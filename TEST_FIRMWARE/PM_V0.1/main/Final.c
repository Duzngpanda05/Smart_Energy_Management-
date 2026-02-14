#include <stdio.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/adc.h"
#include "esp_adc_cal.h"

// --- General Configuration ---
#define DEFAULT_VREF        1100   // mV
#define NO_OF_SAMPLES_I     2000   // 5 Periods 50Hz
#define SAMPLE_DELAY_US     100    // 0.1 ms (10kHz sampling)

#define ADC_VOLTAGE_CHANNEL ADC1_CHANNEL_7  // GPIO35
#define ADC_CURRENT_CHANNEL ADC1_CHANNEL_6  // GPIO34

#define VOLT_CALIBRATION_FACTOR 676.643


static esp_adc_cal_characteristics_t adc_chars;

// ------------------- ADC read -------------------
uint32_t read_adc_mv(adc1_channel_t channel)
{
    uint32_t adc_reading = adc1_get_raw(channel);
    return esp_adc_cal_raw_to_voltage(adc_reading, &adc_chars);
}

// ------------------- Measurement task -------------------
void measure_task(void *pvParameter)
{
// --- Offset Caculation ---
        double offset_i = 0.0, offset_v = 0.0;
        for (int i = 0; i < NO_OF_SAMPLES_I; i++) {
            offset_i += read_adc_mv(ADC_CURRENT_CHANNEL);
            offset_v += read_adc_mv(ADC_VOLTAGE_CHANNEL);
            ets_delay_us(SAMPLE_DELAY_US);
        }
        offset_i /= NO_OF_SAMPLES_I;
        offset_v /= NO_OF_SAMPLES_I;

        printf("Offset measured: offset_i = %.2f mV, offset_v = %.2f mV\n", offset_i, offset_v);

    while (1)
    {
        double sum_i = 0.0, sum_v = 0.0;
        double sum_p = 0.0;

        // --- RMS Caculation ---
        for (int i = 0; i < NO_OF_SAMPLES_I; i++) {
            double v_raw = read_adc_mv(ADC_VOLTAGE_CHANNEL);
            double i_raw = read_adc_mv(ADC_CURRENT_CHANNEL);

            double diff_v = v_raw - offset_v;
            double diff_i = i_raw - offset_i;
            if (fabs(diff_v) < 5) diff_v = 0;
            if (fabs(diff_i) < 5) diff_i = 0;

            sum_v += diff_v * diff_v;
            sum_i += diff_i * diff_i;

            // Instantaneous V
            double v_real = (diff_v / 1000.0) * VOLT_CALIBRATION_FACTOR; // mV -> V

            // Instantaneous I 
            double Vrms_current_tmp = sqrt(sum_i / (i+1)) / 1000.0; // mV -> V

            double sensitivity = 0;
             if (Vrms_current_tmp <= 0.06) sensitivity = 1.0;
            else if (Vrms_current_tmp > 0.06 && Vrms_current_tmp < 0.19) sensitivity = 0.352;
            else if (Vrms_current_tmp >= 0.19 && Vrms_current_tmp < 0.21) sensitivity = 0.1584;
            else if (Vrms_current_tmp >= 0.21 && Vrms_current_tmp < 0.25) sensitivity = 0.1491;
            else if (Vrms_current_tmp >= 0.25 && Vrms_current_tmp < 0.29) sensitivity = 0.1393;
            else if (Vrms_current_tmp >= 0.29 && Vrms_current_tmp <= 0.39) sensitivity = 0.1953;
            else if (Vrms_current_tmp > 0.39 && Vrms_current_tmp <= 0.5) sensitivity = 0.1255;
            else if (Vrms_current_tmp > 0.5 && Vrms_current_tmp <= 0.61) sensitivity = 0.1121;
            else if (Vrms_current_tmp > 0.61 && Vrms_current_tmp <= 0.79) sensitivity = 0.101;
            else sensitivity = 0.0907;

            double i_real = (diff_i / 1000.0) / sensitivity; // mV -> V -> A

            // --- Instantaneous power ---
            sum_p += v_real * i_real;

            ets_delay_us(SAMPLE_DELAY_US);
        }

        // --- RMS Average Power ---
        double Vrms_current = sqrt(sum_i / NO_OF_SAMPLES_I) / 1000.0; // mV -> V
        double Vrms_sensor  = sqrt(sum_v / NO_OF_SAMPLES_I) / 1000.0; // mV -> V
        double Vrms_grid = Vrms_sensor * VOLT_CALIBRATION_FACTOR;

        // Irms
        double sensitivity = 0;
        if (Vrms_current <= 0.06) sensitivity = 1.0;
        else if (Vrms_current > 0.06 && Vrms_current < 0.19) sensitivity = 0.352;
        else if (Vrms_current >= 0.19 && Vrms_current < 0.21) sensitivity = 0.1584;
        else if (Vrms_current >= 0.21 && Vrms_current < 0.25) sensitivity = 0.1491;
        else if (Vrms_current >= 0.25 && Vrms_current < 0.288) sensitivity = 0.1393;
        else if (Vrms_current >= 0.288 && Vrms_current <= 0.4) sensitivity = 0.1953;
        else if (Vrms_current > 0.4 && Vrms_current <= 0.5) sensitivity = 0.1255;
        else if (Vrms_current > 0.5 && Vrms_current <= 0.61) sensitivity = 0.1121;
        else if (Vrms_current > 0.61 && Vrms_current <= 0.79) sensitivity = 0.101;
        else sensitivity = 0.0907;

        double Irms = Vrms_current / sensitivity;

        // --- Average Power ---
        double P = abs(sum_p / NO_OF_SAMPLES_I);  // W

        // --- Apparent Power --- 
        double S = Vrms_grid * Irms;         // VA

        // --- Power Factor ----
        double PF = 0.0;
        if (S != 0) 
            PF = P / S;

       printf("Vrms_current: %.4f V | Vrms_sensor: %.4f V | Vrms_grid: %.1f V | Irms: %.3f A | P: %.3f W | S: %.3f VA | PF: %.3f\n",
            Vrms_current, Vrms_sensor, Vrms_grid, Irms, P, S, PF);

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// ------------------- Main -------------------
void app_main(void)
{
    // --- ADC init ---
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(ADC_VOLTAGE_CHANNEL, ADC_ATTEN_DB_11);
    adc1_config_channel_atten(ADC_CURRENT_CHANNEL, ADC_ATTEN_DB_11);
    esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11,
                             ADC_WIDTH_BIT_12, DEFAULT_VREF, &adc_chars);

    printf("Starting measurement task...\n");
    xTaskCreatePinnedToCore(measure_task, "measure_task", 8192, NULL, 5, NULL, 1);
}
