set(COMPONENT_SRCS "rainsensor.c" "sensors.c" "sensor_adc.c" "mqtt_aws.c" "sensors.c" "sensor_adc.c" "app_main.c")
set(COMPONENT_ADD_INCLUDEDIRS ".")


register_component()

if(CONFIG_AWS_EMBEDDED_CERTS)
target_add_binary_data(${COMPONENT_TARGET} "certs/aws-root-ca.pem" TEXT)
target_add_binary_data(${COMPONENT_TARGET} "certs/weathersynders-certificate.pem.crt" TEXT)
target_add_binary_data(${COMPONENT_TARGET} "certs/weathersynders-private.pem.key" TEXT)
endif()
