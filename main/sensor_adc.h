
#include <stdint.h>

void configure_adc(void);
void read_moisture_adc(uint32_t *raw, uint32_t *voltage);
void read_rain_adc(uint32_t *raw, uint32_t *voltage);
