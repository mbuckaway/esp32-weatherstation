/*
 * Copyright 2010-2015 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * Additions Copyright 2016 Espressif Systems (Shanghai) PTE LTD
 *
 *
 * Licensed under the Apache License, Version 2.0 (the "License").
 * You may not use this file except in compliance with the License.
 * A copy of the License is located at
 *
 *  http://aws.amazon.com/apache2.0
 *
 * or in the "license" file accompanying this file. This file is distributed
 * on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
 * express or implied. See the License for the specific language governing
 * permissions and limitations under the License.
 */
/**
 * @file app_main.c
 * @brief A simple connected window example demonstrating the use of Thing Shadow
 *
 * See example README for more details.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "mqtt_aws.h"
#include "sensors.h"
#include "rainsensor.h"
#include <wifi.h>

static const char *TAG = "WSTN";

void app_main()
{
    sensor_data *sensorinfo = {0};

    ESP_LOGI(TAG, "[APP] Startup...");
    ESP_LOGI(TAG, "[APP] Free memory: %d bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "[APP] IDF version: %s", esp_get_idf_version());

    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("TRANSPORT_TCP", ESP_LOG_VERBOSE);
    esp_log_level_set("TRANSPORT_SSL", ESP_LOG_VERBOSE);
    esp_log_level_set("TRANSPORT", ESP_LOG_VERBOSE);
    esp_log_level_set("OUTBOX", ESP_LOG_VERBOSE);

    ESP_LOGI(TAG, "[APP] Creating main thread...");

    configure_sensors();
    rainsensor_start();
#if 0
    // Configuring WIFI
    wifi_setup();
    wifi_connect();
    wifi_waitforconnect();
    start_mqtt();
#else
    while(1)
    {
        poll_rainsensor();
        sensorinfo =  get_sensors();
        ESP_LOGI(TAG, "Temperature: %0.02f", sensorinfo->temperature);
        ESP_LOGI(TAG, "Humidity: %0.02f", sensorinfo->humidity);
        ESP_LOGI(TAG, "Pressure: %0.02f", sensorinfo->pressure);
        ESP_LOGI(TAG, "Ground Temperature: %0.02f", sensorinfo->groundtemperature);
        ESP_LOGI(TAG, "Ground Moisture: %d", sensorinfo->groundmoisture);
        ESP_LOGI(TAG, "Ground Moisture Voltage: %d", sensorinfo->groundvoltage);
        ESP_LOGI(TAG, "Rain: %0.02f", sensorinfo->rainmm);
        ESP_LOGI(TAG, "UV Level: %d", sensorinfo->uvlevel);
        ESP_LOGI(TAG, "Light Level: %d", sensorinfo->lightlevel);
        ESP_LOGI(TAG, "==============");
        vTaskDelay(5000 / portTICK_RATE_MS);
    }
#endif
}
