#pragma once

#include <stdlib.h>

typedef struct sensordata 
{
    float temperature;
    float humidity;
    float groundtemperature;
    uint32_t groundmoisture;
    uint32_t groundvoltage;
    float pressure;
    float rainmm;
    uint16_t uvlevel;
    uint16_t lightlevel;
} sensor_data;

void configure_sensors(void);
void configure_uart(void);
sensor_data* get_sensors(void);
void poll_uart(void);
