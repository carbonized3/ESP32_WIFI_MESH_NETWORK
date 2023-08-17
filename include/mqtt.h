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
#define CONFIG_BROKER_URL   "mqtt://srv2.clusterfly.ru"	/*	Адрес брокера	*/
#define QoS_0               0
#define QoS_1               1
#define QoS_2               2
#define MQTT_EVT_CONNECTED BIT0
#define BROKER_PASSWORD     "pass_*9*4a**c"  /*	Пароль от аккаунта брокера clusterfly.ru	*/
#define BROKER_USERNAME     "user_*8c**df*"	 /*	Логин от аккаунта брокера clusterfly.ru		*/
#define STR_TOPIC_SIZE		40
#define STR_DATA_SIZE		40

typedef struct {            
    char str_topic[STR_TOPIC_SIZE];
    char str_data[STR_DATA_SIZE];
} mqttPayload;
/*	Функция для запуска mqtt клиента	*/
void mqtt_start(void);
extern esp_mqtt_client_handle_t client;        
extern xQueueHandle mqtt_queue_rx;             
extern xQueueHandle mqtt_queue_tx;             
extern EventGroupHandle_t mqtt_state_event_group;
#endif