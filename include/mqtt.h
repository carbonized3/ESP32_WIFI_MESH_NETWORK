#ifndef _MQTT_H
#define _MQTT_H

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "mqtt_client.h"
#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"
#include "mesh_main.h"
//-------------------------------------------------------------
#define CONFIG_BROKER_URL   "mqtt://srv2.clusterfly.ru"
#define QoS_0               0
#define QoS_1               1
#define QoS_2               2

#define MQTT_EVT_CONNECTED BIT0

typedef struct {            // Структура, которую мы передадим в очередь дальше на передачу всем нодам, так удобнее
    char str_topic[40];
    char str_data[40];
} mqttPayload;

void mqtt_start(void);

extern esp_mqtt_client_handle_t client;        
extern xQueueHandle mqtt_queue_rx;             
extern xQueueHandle mqtt_queue_tx;             
extern EventGroupHandle_t mqtt_state_event_group;

#endif