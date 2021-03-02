
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

#include "rainsensor.h"

static const char *TAG = "RSEN";

#define RAINSENSOR_PARSER_RUNTIME_BUFFER_SIZE (1024)
#define RAINSENSOR_EVENT_LOOP_QUEUE_SIZE (16)
#define EX_UART_NUM UART_NUM_1

ESP_EVENT_DEFINE_BASE(ESP_RAINSENSOR_EVENT);

/**
 * @brief Rain Sensor parser library runtime structure
 *
 */
typedef struct {
    uint8_t item_pos;                              /*!< Current position in item */
    uint8_t item_num;                              /*!< Current item number */
    uint8_t *buffer;                               /*!< Runtime buffer */
    esp_event_loop_handle_t event_loop_hdl;        /*!< Event loop handle */
    rainsensor_t data;                             /*!< Rain Sensor Data object */
    TaskHandle_t tsk_hdl;                          /*!< Rain Sensor Parser task handle */
    QueueHandle_t event_queue;                     /*!< UART event queue handle */
} esp_rainsensor_t;

#define EX_UART_NUM UART_NUM_1

#define RAINSENSOR_PARSER_TASK_STACK_SIZE configMINIMAL_STACK_SIZE * 4
#define RAINSENSOR_PARSER_TASK_PRIORITY 4

#define GOT_DATA_BIT BIT0

/**
 * @brief Parse Rain Sensor statements from Rain Sensor receiver. We receive a line at a time and parse
 * it as requires. For our purposes, we are really only interested in three items:
 * Event - denote an rain event occurred
 * PwrDays - denotes reset finished
 * Acc - a line with the data of interest
 * 
 * Everything else is ignored 
 *
 * @param esp_rainsensor esp_rainsensor_t type object
 * @param len number of bytes to decode
 * @return esp_err_t ESP_OK on success, ESP_FAIL on error
 */
static esp_err_t rainsensor_decode(esp_rainsensor_t *esp_rainsensor, size_t len)
{
    bool found = false;
    const char *data = (const char *)esp_rainsensor->buffer;
    ESP_LOGI(TAG, "Data: %s", data);

    if (strncmp(data, "PwrDays", 7)==0)
    {
            ESP_LOGI(TAG, "Device reboot received");
            /* Send signal to notify that Rain Sensor information has been updated */
            esp_event_post_to(esp_rainsensor->event_loop_hdl, ESP_RAINSENSOR_EVENT, RAINSENSOR_RESET_COMPLETE, NULL, 0, 100 / portTICK_PERIOD_MS);
            found = true;
    }
    if (!found && strncmp(data, "Event", 5)==0)
    {
            ESP_LOGI(TAG, "Device event received");
            /* Send signal to notify that Rain Sensor sent a rain event */
            esp_event_post_to(esp_rainsensor->event_loop_hdl, ESP_RAINSENSOR_EVENT, RAINSENSOR_EVENT, NULL, 0, 100 / portTICK_PERIOD_MS);
            found = true;
    }

    if (!found && strncmp(data, "Acc", 3)==0)
    {
        ESP_LOGI(TAG, "Device data received");
        size_t start = 0;
        char number[16] = {0};
        int item = 0;
        for (size_t i = 0; i<len; i++)
        {
            if (!start && (data[i] >= '0') && (data[i] <= '9'))
            {
                start = i;
                continue;
            }
            if (start && data[i] == ' ')
            {
                strncpy(number, data+start, i-start);
                start = 0;
                switch (item)
                {
                    case 0:
                        esp_rainsensor->data.current_acc_rain = atof(number);
                        break;
                    case 1:
                        esp_rainsensor->data.event_acc_rain = atof(number);
                        break;
                    case 2:
                        esp_rainsensor->data.total_rain = atof(number);
                        break;
                    case 3:
                        esp_rainsensor->data.mm_per_hour_rain = atof(number);
                        break;
                }
                item++;
            }
        }
        /* Send signal to notify that Rain Sensor sent rain data*/
        esp_event_post_to(esp_rainsensor->event_loop_hdl, ESP_RAINSENSOR_EVENT, RAINSENSOR_UPDATE, &(esp_rainsensor->data), sizeof(rainsensor_t), 100 / portTICK_PERIOD_MS);
        found = true;
    }

    return ESP_OK;
}

/**
 * @brief Handle when a pattern has been detected by uart
 *
 * @param esp_rainsensor  esp_rainsensor_t type object
 */
static void esp_handle_uart_pattern(esp_rainsensor_t *esp_rainsensor)
{
    int pos = uart_pattern_pop_pos(EX_UART_NUM);
    if (pos != -1) {
        /* read one line(include '\n') */
        int read_len = uart_read_bytes(EX_UART_NUM, esp_rainsensor->buffer, pos + 1, 100 / portTICK_PERIOD_MS);
        /* make sure the line is a standard string */
        esp_rainsensor->buffer[read_len] = '\0';
        /* Send new line to handle */
        if (rainsensor_decode(esp_rainsensor, read_len + 1) != ESP_OK) {
            ESP_LOGW(TAG, "Rain sensor decode line failed");
        }
    } else {
        ESP_LOGW(TAG, "Pattern Queue Size too small");
        uart_flush_input(EX_UART_NUM);
    }
}

static void rainsensor_parser_task_entry(void *arg)
{
    esp_rainsensor_t *esp_rainsensor = (esp_rainsensor_t *)arg;
    uart_event_t event;
    ESP_LOGI(TAG, "uart event handler start");
    for(;;) {
        //Waiting for UART event.
        if(xQueueReceive(esp_rainsensor->event_queue, (void * )&event, (portTickType)portMAX_DELAY)) {
            switch(event.type) {
                case UART_DATA:
                    break;
                case UART_FIFO_OVF:
                    ESP_LOGE(TAG, "[UART ERROR]: hw fifo overflow");
                    uart_flush(EX_UART_NUM);
                    xQueueReset(esp_rainsensor->event_queue);
                    break;
                case UART_BUFFER_FULL:
                    ESP_LOGW(TAG, "[UART ERROR]: ring buffer full");
                    uart_flush(EX_UART_NUM);
                    xQueueReset(esp_rainsensor->event_queue);
                    break;
                case UART_BREAK:
                    ESP_LOGI(TAG, "[UART BREAK]: uart rx break");
                    break;
                case UART_PARITY_ERR:
                    ESP_LOGE(TAG, "[UART ERROR]: uart parity error");
                    break;
                case UART_FRAME_ERR:
                    ESP_LOGE(TAG, "[UART ERROR]: uart frame error");
                    break;
                case UART_PATTERN_DET:
                    ESP_LOGW(TAG, "[UART DATA]: uart pattern detect");
                    esp_handle_uart_pattern(esp_rainsensor);                    
                    break;
                //Others
                default:
                    ESP_LOGW(TAG, "[UART ERROR]: unknown uart event type: %d", event.type);
                    break;
            }
        }
    }
    esp_event_loop_run(esp_rainsensor->event_loop_hdl, pdMS_TO_TICKS(50));
    vTaskDelete(NULL);
}

static void sendcmd(char cmd)
{
    char cmdtext[3];
    snprintf(cmdtext, 3, "%c\n", cmd);
    ESP_LOGI(TAG, "Sending Rain Sensor command: %c", cmd);
    // Only output the first two bytes of the text, and not the 0 byte
    uart_write_bytes(UART_NUM_1, (const char *) cmdtext, 2);
}

/**
 * @brief Hard reset the rain sensor. Used on system startup to ensure the rain sensor is in the correct mode.
 */
void rainsensor_reset(void)
{
    ESP_LOGI(TAG, "Reseting rain sensor...");
    // Reset the rain sensor
    gpio_set_level(CONFIG_RAIN_MCLR_GPIO, 0);
    vTaskDelay(500 / portTICK_RATE_MS);
    gpio_set_level(CONFIG_RAIN_MCLR_GPIO, 1);
}

/**
 * @brief Set Polling mode in the rain sensor
 */
void rainsensor_polling_mode(void)
{
    ESP_LOGI(TAG, "Requesting polling mode...");
    sendcmd('p');
}

/**
 * @brief Send read data command to rain sensor
 */
void rainsensor_read(void)
{
    ESP_LOGI(TAG, "Requesting data...");
    sendcmd('r');
}

/**
 * @brief Init Rain Sensor Parser
 *
 * @param config Configuration of Rain Sensor Parser
 * @return rainsensor_parser_handle_t handle of rainsensor_parser
 */
rainsensor_parser_handle_t rainsensor_parser_init(void)
{
    esp_rainsensor_t *esp_rainsensor = calloc(1, sizeof(esp_rainsensor_t));
    if (!esp_rainsensor) {
        ESP_LOGE(TAG, "calloc memory for esp_fps failed");
        goto err_rainsensor;
    }
    esp_rainsensor->buffer = calloc(1, RAINSENSOR_PARSER_RUNTIME_BUFFER_SIZE);
    if (!esp_rainsensor->buffer) {
        ESP_LOGE(TAG, "calloc memory for runtime buffer failed");
        goto err_buffer;
    }

    gpio_config_t io_conf_mclr = {
        .intr_type = GPIO_PIN_INTR_DISABLE,
        .pin_bit_mask = (1ULL<<CONFIG_RAIN_MCLR_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_down_en = 0,
        .pull_up_en = 1
    };
    gpio_config(&io_conf_mclr);

    /* Install UART friver */
    /* Configure parameters of an UART driver, communication pins and install the driver */
    uart_config_t uart_config = {
        .baud_rate = 9600,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_APB,
    };
    if (uart_driver_install(EX_UART_NUM, RAINSENSOR_PARSER_RUNTIME_BUFFER_SIZE, 0,
                            RAINSENSOR_EVENT_LOOP_QUEUE_SIZE, &esp_rainsensor->event_queue, 0) != ESP_OK) {
        ESP_LOGE(TAG, "install uart driver failed");
        goto err_uart_install;
    }
    if (uart_param_config(EX_UART_NUM, &uart_config) != ESP_OK) {
        ESP_LOGE(TAG, "config uart parameter failed");
        goto err_uart_config;
    }

    if (uart_set_pin(EX_UART_NUM, CONFIG_UART_GPIO_TXD, CONFIG_UART_GPIO_RXD, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) != ESP_OK) {
        ESP_LOGE(TAG, "config uart gpio failed");
        goto err_uart_config;
    }
    /* Set pattern interrupt, used to detect the end of a line */
    uart_enable_pattern_det_baud_intr(EX_UART_NUM, '\n', 1, 9, 0, 0);
    /* Set pattern queue size */
    uart_pattern_queue_reset(EX_UART_NUM, RAINSENSOR_EVENT_LOOP_QUEUE_SIZE);
    uart_flush(EX_UART_NUM);

    ESP_LOGI(TAG, "Rain Sensor UART set to TXD GPIO %d and RXD GPIO %d and MCLR on %d", CONFIG_UART_GPIO_TXD, CONFIG_UART_GPIO_RXD, CONFIG_RAIN_MCLR_GPIO);

    /* Create Event loop */
    esp_event_loop_args_t loop_args = {
        .queue_size = RAINSENSOR_EVENT_LOOP_QUEUE_SIZE,
        .task_name = NULL
    };
    if (esp_event_loop_create(&loop_args, &esp_rainsensor->event_loop_hdl) != ESP_OK) {
        ESP_LOGE(TAG, "create event loop faild");
        goto err_eloop;
    }
    /* Create Rain Sensor Parser task */
    BaseType_t err = xTaskCreate(
                         rainsensor_parser_task_entry,
                         "rainsensor_parser",
                         RAINSENSOR_PARSER_TASK_STACK_SIZE,
                         esp_rainsensor,
                         RAINSENSOR_PARSER_TASK_PRIORITY,
                         &esp_rainsensor->tsk_hdl);
    if (err != pdTRUE) {
        ESP_LOGE(TAG, "create Rain Sensor Parser task failed");
        goto err_task_create;
    }
    ESP_LOGI(TAG, "Rain Sensor Parser init OK");
    return esp_rainsensor;
    /*Error Handling*/
err_task_create:
    esp_event_loop_delete(esp_rainsensor->event_loop_hdl);
err_eloop:
err_uart_install:
    uart_driver_delete(EX_UART_NUM);
err_uart_config:
err_buffer:
    free(esp_rainsensor->buffer);
err_rainsensor:
    free(esp_rainsensor);
    return NULL;
}

/**
 * @brief Deinit Rain Sensor Parser
 *
 * @param rainsensor_hdl handle of Rain Sensor parser
 * @return esp_err_t ESP_OK on success,ESP_FAIL on error
 */
esp_err_t rainsensor_parser_deinit(rainsensor_parser_handle_t rainsensor_hdl)
{
    esp_rainsensor_t *esp_rainsensor = (esp_rainsensor_t *)rainsensor_hdl;
    vTaskDelete(esp_rainsensor->tsk_hdl);
    esp_event_loop_delete(esp_rainsensor->event_loop_hdl);
    esp_err_t err = uart_driver_delete(EX_UART_NUM);
    free(esp_rainsensor->buffer);
    free(esp_rainsensor);
    return err;
}

/**
 * @brief Add user defined handler for Rain Sensor parser
 *
 * @param rainsensor_hdl handle of Rain Sensor parser
 * @param event_handler user defined event handler
 * @param handler_args handler specific arguments
 * @return esp_err_t
 *  - ESP_OK: Success
 *  - ESP_ERR_NO_MEM: Cannot allocate memory for the handler
 *  - ESP_ERR_INVALIG_ARG: Invalid combination of event base and event id
 *  - Others: Fail
 */
esp_err_t rainsensor_parser_add_handler(rainsensor_parser_handle_t rainsensor_hdl, esp_event_handler_t event_handler, void *handler_args)
{
    esp_rainsensor_t *esp_rainsensor = (esp_rainsensor_t *)rainsensor_hdl;
    return esp_event_handler_register_with(esp_rainsensor->event_loop_hdl, ESP_RAINSENSOR_EVENT, ESP_EVENT_ANY_ID,
                                           event_handler, handler_args);
}

/**
 * @brief Remove user defined handler for Rain Sensor parser
 *
 * @param rainsensor_hdl handle of Rain Sensor parser
 * @param event_handler user defined event handler
 * @return esp_err_t
 *  - ESP_OK: Success
 *  - ESP_ERR_INVALIG_ARG: Invalid combination of event base and event id
 *  - Others: Fail
 */
esp_err_t rainsensor_parser_remove_handler(rainsensor_parser_handle_t rainsensor_hdl, esp_event_handler_t event_handler)
{
    esp_rainsensor_t *esp_rainsensor = (esp_rainsensor_t *)rainsensor_hdl;
    return esp_event_handler_unregister_with(esp_rainsensor->event_loop_hdl, ESP_RAINSENSOR_EVENT, ESP_EVENT_ANY_ID, event_handler);
}
