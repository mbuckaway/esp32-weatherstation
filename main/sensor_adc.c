#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "esp_system.h"
#include "esp_log.h"
#include "driver/adc.h"
#include "esp_adc_cal.h"


#if CONFIG_IDF_TARGET_ESP32
static const adc_bits_width_t width = ADC_WIDTH_BIT_12;
#elif CONFIG_IDF_TARGET_ESP32S2
static const adc_bits_width_t width = ADC_WIDTH_BIT_13;
#endif
static const adc_atten_t atten = ADC_ATTEN_DB_11;
static const adc_unit_t unit = ADC_UNIT_1;
static const int32_t DEFAULT_VREF = 1100;        //Use adc2_vref_to_gpio() to obtain a better estimate

static esp_adc_cal_characteristics_t *adc_chars = NULL;

static const char *TAG = "MOISTURE_ADC";

static void check_efuse(void)
{
#if CONFIG_IDF_TARGET_ESP32
    //Check if TP is burned into eFuse
    if (esp_adc_cal_check_efuse(ESP_ADC_CAL_VAL_EFUSE_TP) == ESP_OK) {
        ESP_LOGI(TAG, "eFuse Two Point: Supported\n");
    } else {
        ESP_LOGI(TAG, "eFuse Two Point: NOT supported\n");
    }
    //Check Vref is burned into eFuse
    if (esp_adc_cal_check_efuse(ESP_ADC_CAL_VAL_EFUSE_VREF) == ESP_OK) {
        ESP_LOGI(TAG, "eFuse Vref: Supported\n");
    } else {
        ESP_LOGI(TAG, "eFuse Vref: NOT supported\n");
    }
#elif CONFIG_IDF_TARGET_ESP32S2
    if (esp_adc_cal_check_efuse(ESP_ADC_CAL_VAL_EFUSE_TP) == ESP_OK) {
        ESP_LOGI(TAG, "eFuse Two Point: Supported\n");
    } else {
        ESP_LOGI(TAG, "Cannot retrieve eFuse Two Point calibration values. Default calibration values will be used.\n");
    }
#else
#error "This app is not configured for ESP32/ESP32S2."
#endif
}

void configure_adc(void)
{
    esp_err_t r = 0;
    gpio_num_t moisture_gpio_num = 0;

    ESP_ERROR_CHECK(adc_digi_init());

    check_efuse();
    adc1_config_width(width);
    adc1_config_channel_atten( CONFIG_MOISTURE_ADC_CHANNEL, atten );

    r = adc1_pad_get_io_num( CONFIG_MOISTURE_ADC_CHANNEL, &moisture_gpio_num );
    if (r != ESP_OK)
    {
        ESP_LOGE(TAG, "Moisture sensor (ADC1) config failed");
    }
    ESP_LOGI(TAG, "Moisture sensor channel %d @ GPIO %d", CONFIG_MOISTURE_ADC_CHANNEL, moisture_gpio_num);

    adc_chars = calloc(1, sizeof(esp_adc_cal_characteristics_t));
    esp_adc_cal_value_t val_type = esp_adc_cal_characterize(unit, atten, width, DEFAULT_VREF, adc_chars);
    if (val_type == ESP_ADC_CAL_VAL_EFUSE_TP) {
        ESP_LOGI(TAG, "Characterized using Two Point Value");
    } else if (val_type == ESP_ADC_CAL_VAL_EFUSE_VREF) {
        ESP_LOGI(TAG, "Characterized using eFuse Vref");
    } else {
        ESP_LOGI(TAG, "Characterized using Default Vref");
    }
}

void read_moisture_adc(uint32_t *raw, uint32_t *voltage)
{
        //Multisampling
#if CONFIG_ADC_MULTISAMPLING_COUNT == 1
        *raw = adc1_get_raw((adc1_channel_t)CONFIG_MOISTURE_ADC_CHANNEL);
#else
        uint32_t adc_reading = 0;
        for (int i = 0; i < CONFIG_ADC_MULTISAMPLING_COUNT; i++) {
            adc_reading += adc1_get_raw((adc1_channel_t)CONFIG_MOISTURE_ADC_CHANNEL);
        }
        *raw = adc_reading/CONFIG_ADC_MULTISAMPLING_COUNT;
#endif        
        //Convert adc_reading to voltage in mV
        *voltage = esp_adc_cal_raw_to_voltage(*raw, adc_chars);
}
