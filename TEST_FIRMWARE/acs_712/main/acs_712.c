#include <stdio.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/adc.h"
#include "esp_adc_cal.h"

#define ADC_CHANNEL     ADC1_CHANNEL_6   // GPIO34
#define DEFAULT_VREF    1100             // mV, giá trị tham chiếu nội
#define NO_OF_SAMPLES   100              // số mẫu đo RMS

static esp_adc_cal_characteristics_t adc_chars;

// Read ADC and convert to mV
uint32_t read_adc_mv()
{
    uint32_t adc_reading = 0;
    for (int i = 0; i < 10; i++) {
        adc_reading += adc1_get_raw(ADC_CHANNEL);
    }
    adc_reading /= 10;

    uint32_t voltage = esp_adc_cal_raw_to_voltage(adc_reading, &adc_chars);
    return voltage; // mV
}

void app_main(void)
{
    //ADC configuration 
    adc1_config_width(ADC_WIDTH_BIT_12); //0 - 4095
    adc1_config_channel_atten(ADC_CHANNEL, ADC_ATTEN_DB_11); // Read up to 3.3V
    esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_12, DEFAULT_VREF, &adc_chars); 

    printf("ZMCT103C AC current measurement started...\n");

    while (1) {
        double offset = 0.0;
        double sum_squared = 0.0;

        // offset calculation
        for (int i = 0; i < NO_OF_SAMPLES; i++) {
            offset += read_adc_mv();
        }
        offset /= NO_OF_SAMPLES;

        //Caculate Vrms 
        for (int i = 0; i < NO_OF_SAMPLES; i++) {
            double voltage = read_adc_mv();
            double diff = voltage - offset;
            sum_squared += diff * diff;
        }

        double mean_square = sum_squared / NO_OF_SAMPLES;
        double Vrms = sqrt(mean_square) / 1000.0; // mV -> V

        //Sensisitivy of Sensor 
        double sensitivity;

      if (Vrms < 0.2)
            sensitivity = 0.27;
        else if (Vrms > 0.4)
            sensitivity = 0.132;
        else
            sensitivity = 0.14;

        double Irms = Vrms / sensitivity;

        printf("Vrms = %.4f V | Irms = %.3f A\n", Vrms, Irms);

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
