#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <esp_system.h>
#include <esp_log.h>

#include <string.h>

#include <bmp280.h>
#include <ds18x20.h>
#include "sensors.h"
#include "sensor_adc.h"
#include <bh1750.h>

static const char *TAG = "SENSORS";

#define UART_BUF_SIZE (1024)

// Only look for the first sensor on the bus
#define MAX_DB18X20_SENSORS 1

static int ds18x20_sensor_count = 0;
static i2c_dev_t light_dev;
static bmp280_params_t bme280_params;
static bmp280_t bme280_dev;
static ds18x20_addr_t addrs[MAX_DB18X20_SENSORS];

static sensor_data sensorinfo = {0};

sensor_data* get_sensors(void)
{
#if CONFIG_DHT22_ENABLED
    if (dht_read_float_data(sensor_type, CONFIG_GPIO_OUTPUT_IO_DHT22, &sensorinfo.humidity, &sensorinfo.temperature) == ESP_OK)
    {
        ESP_LOGI(TAG, "Sensor Read: Temperature: %0.01f Humidity: %0.01f", sensorinfo.temperature, sensorinfo.humidity);
    }
    else
    {
        ESP_LOGE(TAG, "Could not read data from sensor on GPIO %d\n", CONFIG_GPIO_OUTPUT_IO_DHT22);
    }
#endif
    if (bmp280_read_float(&bme280_dev, &sensorinfo.temperature, &sensorinfo.pressure, &sensorinfo.humidity) != ESP_OK)
    {
        ESP_LOGE(TAG, "Temperature/pressure reading failed");
    }

    if (bh1750_read(&light_dev, &sensorinfo.lightlevel) == ESP_OK)
    {
        ESP_LOGI(TAG, "Lux value: %d", sensorinfo.lightlevel);
    }
    else
    {
        ESP_LOGE(TAG, "Could not read lux data");
    }

    if (ds18x20_sensor_count==0)
    {
        ESP_LOGW(TAG, "Rescan for ds18x20 sensors on pin %d", CONFIG_DS18X20_GPIO_PIN);
        ds18x20_sensor_count = ds18x20_scan_devices(CONFIG_DS18X20_GPIO_PIN, addrs, MAX_DB18X20_SENSORS);
        if (ds18x20_sensor_count==0)
        {
            ESP_LOGE(TAG, "Rescan found no ds18x20 sensors found on pin %d", CONFIG_DS18X20_GPIO_PIN);
        }
    }
    if (ds18x20_sensor_count>0)
    {
        ds18x20_measure_and_read(CONFIG_DS18X20_GPIO_PIN, addrs[0], &sensorinfo.groundtemperature);
        ESP_LOGI(TAG, "DS18B20 Ground Temperature: %0.02f", sensorinfo.groundtemperature);
    }

    read_moisture_adc(&sensorinfo.groundmoisture, &sensorinfo.groundvoltage);

    return &sensorinfo;
}

void configure_sensors(void)
{
    //ds18x20_addr_t addrs[MAX_DB18X20_SENSORS];
    memset(&bme280_dev, 0, sizeof(bmp280_t));
    memset(&light_dev, 0, sizeof(i2c_dev_t)); // Zero descriptor

    configure_adc();

    bmp280_init_default_params(&bme280_params);

    ds18x20_sensor_count = ds18x20_scan_devices(CONFIG_DS18X20_GPIO_PIN, addrs, MAX_DB18X20_SENSORS);
    if (ds18x20_sensor_count==0)
    {
        ESP_LOGE(TAG, "No ds18x20 sensors found on pin %d", CONFIG_DS18X20_GPIO_PIN);
    }

    ESP_ERROR_CHECK(i2cdev_init()); // Init library

    // Setup the light sensor
    ESP_ERROR_CHECK(bh1750_init_desc(&light_dev, BH1750_ADDR_LO, 0, CONFIG_I2C_GPIO_SDA, CONFIG_I2C_GPIO_SCL));
    if (bh1750_setup(&light_dev, BH1750_MODE_CONTINUOUS, BH1750_RES_HIGH) == ESP_OK)
    {
        ESP_LOGI(TAG, "BH1750 light sensor found");
    }
    else
    {
        ESP_LOGE(TAG, "Could not configure BH1750 on SCL pin %d and SDA pin %d", CONFIG_I2C_GPIO_SCL, CONFIG_I2C_GPIO_SDA);
    }

    // Setup the temperature/etc sensor
    bmp280_init_desc(&bme280_dev, BMP280_I2C_ADDRESS_0, 0, CONFIG_I2C_GPIO_SDA, CONFIG_I2C_GPIO_SCL);
    if (bmp280_init(&bme280_dev, &bme280_params) == ESP_OK)
    {
        bool bme280p = bme280_dev.id == BME280_CHIP_ID;
        ESP_LOGI(TAG, "BMP280: found %s", bme280p ? "BME280" : "BMP280");
    }
    else
    {
        ESP_LOGE(TAG, "Could not configure BME280 on SCL pin %d and SDA pin %d", CONFIG_I2C_GPIO_SCL, CONFIG_I2C_GPIO_SCL);
    }
}
