#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <unistd.h>
#include <limits.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "driver/sdmmc_host.h"

#include "nvs.h"
#include "nvs_flash.h"

#include "aws_iot_config.h"
#include "aws_iot_log.h"
#include "aws_iot_version.h"
#include "aws_iot_mqtt_client_interface.h"

#include <wifi.h>
#include "sensors.h"

static const char *TAG = "MQTTAWS";

/**
 * @brief Default MQTT HOST URL is pulled from the aws_iot_config.h
 */
static char HostAddress[255] = AWS_IOT_MQTT_HOST;

/**
 * @brief Default MQTT port is pulled from the aws_iot_config.h
 */
static uint32_t port = AWS_IOT_MQTT_PORT;


/* CA Root certificate, device ("Thing") certificate and device
 * ("Thing") key.

   Example can be configured one of two ways:

   "Embedded Certs" are loaded from files in "certs/" and embedded into the app binary.

   "Filesystem Certs" are loaded from the filesystem (SD card, etc.)

   See example README for more details.
*/
#if defined(CONFIG_AWS_EMBEDDED_CERTS)

extern const uint8_t aws_root_ca_pem_start[] asm("_binary_aws_root_ca_pem_start");
extern const uint8_t aws_root_ca_pem_end[] asm("_binary_aws_root_ca_pem_end");
extern const uint8_t certificate_pem_crt_start[] asm("_binary_weathersynders_certificate_pem_crt_start");
extern const uint8_t certificate_pem_crt_end[] asm("_binary_weathersynders_certificate_pem_crt_end");
extern const uint8_t private_pem_key_start[] asm("_binary_weathersynders_private_pem_key_start");
extern const uint8_t private_pem_key_end[] asm("_binary_weathersynders_private_pem_key_end");

#elif defined(CONFIG_AWS_FILESYSTEM_CERTS)

static const char * DEVICE_CERTIFICATE_PATH = CONFIG_AWS_CERTIFICATE_PATH;
static const char * DEVICE_PRIVATE_KEY_PATH = CONFIG_AWS_PRIVATE_KEY_PATH;
static const char * ROOT_CA_PATH = CONFIG_AWS_ROOT_CA_PATH;

#else
#error "Invalid method for loading certs"
#endif


void disconnectCallbackHandler(AWS_IoT_Client *pClient, void *data) {
    ESP_LOGW(TAG, "MQTT Disconnect");
    IoT_Error_t rc = FAILURE;

    if(NULL == pClient) {
        return;
    }

    if(aws_iot_is_autoreconnect_enabled(pClient)) {
        ESP_LOGI(TAG, "Auto Reconnect is enabled, Reconnecting attempt will start now");
    } else {
        ESP_LOGW(TAG, "Auto Reconnect not enabled. Starting manual reconnect...");
        rc = aws_iot_mqtt_attempt_reconnect(pClient);
        if(NETWORK_RECONNECTED == rc) {
            ESP_LOGW(TAG, "Manual Reconnect Successful");
        } else {
            ESP_LOGW(TAG, "Manual Reconnect Failed - %d", rc);
        }
    }
}

#define MAX_ID_STRING (32)

static char *create_id_string(void)
{
    uint8_t mac[6];
    char *id_string = calloc(1, MAX_ID_STRING);
    if (id_string != NULL)
    {
        esp_read_mac(mac, ESP_MAC_WIFI_STA);
        snprintf(id_string, MAX_ID_STRING,  "%s_%02x%02X%02X", CONFIG_DEVICE_LOCATION_NAME, mac[3], mac[4], mac[5]);
    }
    return id_string;
}


void aws_iot_task(void *param) {
    static char cPayload[1024] = {0};
    static char topic[256] = {0};
    int topic_len = 0;
    sensor_data *sensorinfo = NULL;

    IoT_Error_t rc = FAILURE;

    AWS_IoT_Client client;
    IoT_Client_Init_Params mqttInitParams = iotClientInitParamsDefault;
    IoT_Client_Connect_Params connectParams = iotClientConnectParamsDefault;

    IoT_Publish_Message_Params paramsQOS0;

    mqttInitParams.enableAutoReconnect = false; // We enable this later below
    mqttInitParams.pHostURL = HostAddress;
    mqttInitParams.port = port;

#if defined(CONFIG_AWS_EMBEDDED_CERTS)
    mqttInitParams.pRootCALocation = (const char *)aws_root_ca_pem_start;
    mqttInitParams.pDeviceCertLocation = (const char *)certificate_pem_crt_start;
    mqttInitParams.pDevicePrivateKeyLocation = (const char *)private_pem_key_start;

#elif defined(CONFIG_AWS_FILESYSTEM_CERTS)
    mqttInitParams.pRootCALocation = ROOT_CA_PATH;
    mqttInitParams.pDeviceCertLocation = DEVICE_CERTIFICATE_PATH;
    mqttInitParams.pDevicePrivateKeyLocation = DEVICE_PRIVATE_KEY_PATH;
#endif

    mqttInitParams.mqttCommandTimeout_ms = 20000;
    mqttInitParams.tlsHandshakeTimeout_ms = 5000;
    mqttInitParams.isSSLHostnameVerify = true;
    mqttInitParams.disconnectHandler = disconnectCallbackHandler;
    mqttInitParams.disconnectHandlerData = NULL;

#ifdef CONFIG_AWS_SDCARD_CERTS
    ESP_LOGI(TAG, "Mounting SD card...");
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 3,
    };
    sdmmc_card_t* card;
    esp_err_t ret = esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot_config, &mount_config, &card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount SD card VFAT filesystem. Error: %s", esp_err_to_name(ret));
        abort();
    }
#endif

    rc = aws_iot_mqtt_init(&client, &mqttInitParams);
    if(SUCCESS != rc) {
        ESP_LOGE(TAG, "aws_iot_mqtt_init returned error : %d ", rc);
        abort();
    }

    connectParams.keepAliveIntervalInSec = 10;
    connectParams.isCleanSession = true;
    connectParams.MQTTVersion = MQTT_3_1_1;
    /* Client ID is is generated from the WIFI MAC address */
    connectParams.pClientID = create_id_string();
    connectParams.clientIDLen = (uint16_t) strlen(connectParams.pClientID);
    connectParams.isWillMsgPresent = false;

    ESP_LOGI(TAG, "Connecting to AWS: %s:%d...", mqttInitParams.pHostURL, mqttInitParams.port);
    do {
        rc = aws_iot_mqtt_connect(&client, &connectParams);
        if(SUCCESS != rc) {
            ESP_LOGE(TAG, "Error(%d) connecting to %s:%d", rc, mqttInitParams.pHostURL, mqttInitParams.port);
            vTaskDelay(2000 / portTICK_RATE_MS);
        }
    } while(SUCCESS != rc);
    ESP_LOGI(TAG, "Connected");

    /*
     * Enable Auto Reconnect functionality. Minimum and Maximum time of Exponential backoff are set in aws_iot_config.h
     *  #AWS_IOT_MQTT_MIN_RECONNECT_WAIT_INTERVAL
     *  #AWS_IOT_MQTT_MAX_RECONNECT_WAIT_INTERVAL
     */
    rc = aws_iot_mqtt_autoreconnect_set_status(&client, true);
    if(SUCCESS != rc) {
        ESP_LOGE(TAG, "Unable to set Auto Reconnect to true - %d", rc);
        abort();
    }

    sprintf(topic, "%s/%s", CONFIG_AWS_TOPIC, connectParams.pClientID);
    topic_len = strlen(topic);

    ESP_LOGI(TAG, "Publishing to topic: %s", topic);

    paramsQOS0.qos = QOS0;
    paramsQOS0.payload = (void *) cPayload;
    paramsQOS0.isRetained = 0;

    while((NETWORK_ATTEMPTING_RECONNECT == rc || NETWORK_RECONNECTED == rc || SUCCESS == rc)) {

        //Max time the yield function will wait for read messages
        rc = aws_iot_mqtt_yield(&client, 100);
        if(NETWORK_ATTEMPTING_RECONNECT == rc) {
            // If the client is attempting to reconnect we will skip the rest of the loop.
            continue;
        }
        vTaskDelay(1000 / portTICK_RATE_MS);

        sensorinfo = get_sensors();

        sprintf(cPayload, "{\"location\":\"%s\", \"type\": \"%s\", \"id\": \"%s\", \"temperature\": %0.01f, \"humidity\": %0.01f, \"rain\": %0.01f, \"groundtemperature\": %0.01f, \"groundmoisture\": %d, \"pressure\": %0.02f}",
             CONFIG_DEVICE_LOCATION_NAME,
             CONFIG_DEVICE_TYPE_NAME,
             connectParams.pClientID,
             sensorinfo->temperature,
             sensorinfo->humidity,
             sensorinfo->rainmm,
             sensorinfo->groundtemperature,
             sensorinfo->groundmoisture,
             sensorinfo->pressure
        );
        paramsQOS0.payloadLen = strlen(cPayload);
        ESP_LOGI(TAG, "Sending to %s: %s", topic, cPayload);
        rc = aws_iot_mqtt_publish(&client, topic, topic_len, &paramsQOS0);

        vTaskDelay(10000 / portTICK_RATE_MS);
    }

    ESP_LOGE(TAG, "An error occurred in the main loop.");
    abort();
}

void start_mqtt(void)
{
    ESP_LOGI(TAG, "AWS IoT SDK Version %d.%d.%d-%s", VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH, VERSION_TAG);
   xTaskCreatePinnedToCore(&aws_iot_task, "aws_iot_task", 9216, NULL, 5, NULL, 1);
}