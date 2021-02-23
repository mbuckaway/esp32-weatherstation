
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_event.h"

#include <regex.h>

static const char *TAG = "RSEN";

static QueueHandle_t uart0_queue;
static EventGroupHandle_t data_event;

#define EX_UART_NUM UART_NUM_1

#define THREAD_READDATA_NAME "uart_event_task"
#define THREAD_READDATA_STACKSIZE configMINIMAL_STACK_SIZE * 4
#define THREAD_READDATA_PRIORITY 4

static const size_t UART_BUF_SIZE = 1024;
static uint8_t* databuffer = NULL;
static size_t datalen = 0L;

#define GOT_DATA_BIT BIT0

static void uart_event_task(void *pvParameters)
{
    uart_event_t event;
    ESP_LOGI(TAG, "uart event handler start");
    for(;;) {
        //Waiting for UART event.
        if(xQueueReceive(uart0_queue, (void * )&event, (portTickType)portMAX_DELAY)) {
            bzero(databuffer, UART_BUF_SIZE);
            ESP_LOGI(TAG, "uart[%d] event:", EX_UART_NUM);
            switch(event.type) {
                //Event of UART receving data
                /*We'd better handler data event fast, there would be much more data events than
                other types of events. If we take too much time on data event, the queue might
                be full.*/
                case UART_DATA:
                    datalen = event.size;
                    ESP_LOGI(TAG, "[UART DATA]: %d bytes", event.size);
                    uart_read_bytes(EX_UART_NUM, databuffer, event.size, portMAX_DELAY);
                    xEventGroupSetBits(data_event, GOT_DATA_BIT);
                    break;
                //Event of HW FIFO overflow detected
                case UART_FIFO_OVF:
                    ESP_LOGE(TAG, "[UART ERROR]: hw fifo overflow");
                    // If fifo overflow happened, you should consider adding flow control for your application.
                    // The ISR has already reset the rx FIFO,
                    // As an example, we directly flush the rx buffer here in order to read more data.
                    uart_flush_input(EX_UART_NUM);
                    xQueueReset(uart0_queue);
                    break;
                //Event of UART ring buffer full
                case UART_BUFFER_FULL:
                    ESP_LOGW(TAG, "[UART ERROR]: ring buffer full");
                    // If buffer full happened, you should consider encreasing your buffer size
                    // As an example, we directly flush the rx buffer here in order to read more data.
                    uart_flush_input(EX_UART_NUM);
                    xQueueReset(uart0_queue);
                    break;
                //Event of UART RX break detected
                case UART_BREAK:
                    ESP_LOGI(TAG, "[UART BREAK]: uart rx break");
                    break;
                //Event of UART parity check error
                case UART_PARITY_ERR:
                    ESP_LOGE(TAG, "[UART ERROR]: uart parity error");
                    break;
                //Event of UART frame error
                case UART_FRAME_ERR:
                    ESP_LOGE(TAG, "[UART ERROR]: uart frame error");
                    break;
                //UART_PATTERN_DET
                case UART_PATTERN_DET:
                    // Pattern detect isn't used
                    ESP_LOGW(TAG, "[UART DATA]: uart pattern detect");
                    break;
                //Others
                default:
                    ESP_LOGW(TAG, "[UART ERROR]: uart event type: %d", event.type);
                    break;
            }
        }
    }
    // If will be a bad day if we ever get here
    free(databuffer);
    databuffer = NULL;
    vTaskDelete(NULL);
}

bool match(const char *string, const char *pattern)
{
    bool result = false;
    int status = 0;
    regex_t re;
    if (regcomp(&re, pattern, REG_EXTENDED|REG_NOSUB) == 0)
    {
        result = true;
        status = regexec(&re, string, 0, NULL, 0);
        regfree(&re);
        if (status != 0) status = false;
    }
    return result;
}

void sendcmd(char cmd)
{
    char cmdtext[3];
    snprintf(cmdtext, 3, "%c\n", cmd);
    ESP_LOGI(TAG, "Sending Rain Sensor command: %c", cmd);
    // Only output the first two bytes of the text, and not the 0 byte
    uart_write_bytes(UART_NUM_1, (const char *) cmdtext, 2);
}

/**
 * @brief Wait for data from the UART for a max of 5 seconds
 * @returns True on data buffer returned or False on timeout
 */
bool waitfordata(void)
{
    bool result = false;
    // Sit and wait until something happens for a max of 5 secs
    ESP_LOGI(TAG, "Waiting for uart data...");
    EventBits_t bits = xEventGroupWaitBits(data_event, GOT_DATA_BIT, pdTRUE, pdFALSE, 5000 / portTICK_PERIOD_MS);
    result = (bits & GOT_DATA_BIT);
    return result;
}

void resetsensor(void)
{
    ESP_LOGI(TAG, "Reseting rain sensor...");
    // Reset the rain sensor
    gpio_set_level(CONFIG_RAIN_MCLR_GPIO, 0);
    vTaskDelay(500 / portTICK_RATE_MS);
    gpio_set_level(CONFIG_RAIN_MCLR_GPIO, 1);

    // We wait for the device header. This takes a few tries to get it all.
    for (int i=0; i<4; i++)
    {
        waitfordata();
        ESP_LOGI(TAG, "Data: %d: %s", datalen, databuffer);
    }
}

void rainsensor_start(void)
{
    data_event = xEventGroupCreate();
    if (data_event == NULL)
    {
        ESP_LOGE(TAG, "Event group init failed!");
    }
    databuffer = (uint8_t*) malloc(UART_BUF_SIZE);
    if (databuffer == NULL)
    {
        ESP_LOGE(TAG, "Data buffer init failed!");
    }
    /* Configure parameters of an UART driver, communication pins and install the driver */
    uart_config_t uart_config = {
        .baud_rate = 9600,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_APB,
    };
    uart_driver_install(EX_UART_NUM, UART_BUF_SIZE * 2, 0, 20, &uart0_queue, 0);
    //uart_driver_install(EX_UART_NUM, UART_BUF_SIZE * 2, 0, 0, NULL, 0);
    uart_param_config(EX_UART_NUM, &uart_config);
    uart_set_pin(EX_UART_NUM, CONFIG_UART_GPIO_TXD, CONFIG_UART_GPIO_RXD, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

    gpio_config_t io_conf_mclr = {
        .intr_type = GPIO_PIN_INTR_DISABLE,
        .pin_bit_mask = (1ULL<<CONFIG_RAIN_MCLR_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_down_en = 0,
        .pull_up_en = 1
    };
    gpio_config(&io_conf_mclr);
    ESP_LOGI(TAG, "Rain Sensor UART set to TXD GPIO %d and RXD GPIO %d and MCLR on %d", CONFIG_UART_GPIO_TXD, CONFIG_UART_GPIO_RXD, CONFIG_RAIN_MCLR_GPIO);

    // Start the read data task
    xTaskCreate(uart_event_task, THREAD_READDATA_NAME, THREAD_READDATA_STACKSIZE, NULL, THREAD_READDATA_PRIORITY, NULL);

    resetsensor();

    ESP_LOGI(TAG, "Reading data...");
    sendcmd('p');

    // We wait for the device header
    waitfordata();
    ESP_LOGI(TAG, "Data: %d: %s", datalen, databuffer);
}

void poll_rainsensor(void)
{
    ESP_LOGI(TAG, "Reading data...");
    sendcmd('r');

    // We wait for the device header
    waitfordata();
    ESP_LOGI(TAG, "Data: %d: %s", datalen, databuffer);
}
