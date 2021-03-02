#pragma once


#ifdef __cplusplus
extern "C" {
#endif

#include "esp_types.h"
#include "esp_event.h"
#include "esp_err.h"
#include "driver/uart.h"

/**
 * @brief Declare of Rain Sensor Parser Event base
 *
 */
ESP_EVENT_DECLARE_BASE(ESP_RAINSENSOR_EVENT);

/**
 * @brief Rain sensor data object
 *
 */
typedef struct {
    float current_acc_rain;                                        /*!< Current accumulator of rain (mm)*/
    float event_acc_rain;                                          /*!< Rain event acculator of rain (mm) */
    float total_rain;                                              /*!< Total Rainfall since last reset */
    float mm_per_hour_rain;                                        /*!< Predicted mm per hour of rain for current event */
} rainsensor_t;

/**
 * @brief Rain Sensor Parser Event ID
 *
 */
typedef enum {
    RAINSENSOR_UPDATE,          /*!< Rain Sensor information has been updated */
    RAINSENSOR_RESET_COMPLETE,  /*!< Rain Sensor reset complete */
    RAINSENSOR_EVENT,           /*!< Rain Sensor denoted an event start */
    RAINSENSOR_UNKNOWN          /*!< Unknown statements detected */
} rainsensor_event_id_t;


/**
 * @brief Rain Sensor Parser Handle
 *
 */
typedef void *rainsensor_parser_handle_t;

/**
 * @brief Init Rain Sensor Parser
 *
 * @param config Configuration of Rain Sensor Parser
 * @return rainsensor_parser_handle_t handle of Rain Sensor parser
 */
rainsensor_parser_handle_t rainsensor_parser_init(void);

/**
 * @brief Deinit Rain Sensor Parser
 *
 * @param rainsensor_hdl handle of Rain Sensor parser
 * @return esp_err_t ESP_OK on success, ESP_FAIL on error
 */
esp_err_t rainsensor_parser_deinit(rainsensor_parser_handle_t rainsensor_hdl);

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
esp_err_t rainsensor_parser_add_handler(rainsensor_parser_handle_t rainsensor_hdl, esp_event_handler_t event_handler, void *handler_args);

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
esp_err_t rainsensor_parser_remove_handler(rainsensor_parser_handle_t rainsensor_hdl, esp_event_handler_t event_handler);

/**
 * @brief Send read data command to rain sensor
 */
void rainsensor_read(void);

/**
 * @brief Hard reset the rain sensor. Used on system startup to ensure the rain sensor is in the correct mode.
 */
void rainsensor_reset(void);

/**
 * @brief Set Polling mode in the rain sensor
 */
void rainsensor_polling_mode(void);

#ifdef __cplusplus
}
#endif
