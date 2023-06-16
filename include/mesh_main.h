#ifndef _MESH_MAIN_H
#define MESH_MAIN_H
#include <string.h>
#include "esp_wifi.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mesh.h"
#include "esp_mesh_internal.h"
#include "lwip/sockets.h"
#include "nvs_flash.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include "mqtt.h"
//------------------------------------------------------РЕЛЕ И LED----------------------------------------------------------------------------
#define RELAY_ON                                1
#define RELAY_OFF                               0
#define led_gpio                                17
#define relay_gpio                              4
//---------------------------------------------------NODE'S_MAC_DEFINITIONS---------------------------------------------------------------------------
#define NODE_A_STA_MAC_STR     "c8:f0:9e:a1:26:24"
#define NODE_B_STA_MAC_STR     "94:e6:86:02:77:e4"
#define NODE_C_STA_MAC_STR     "ac:67:b2:3c:2e:1c"          
#define NODE_D_STA_MAC_STR     "e0:5a:1b:76:9f:90"          
#define AMOUNT_OF_NODES                         4      					/*	Увеличивать кол-во при надобности	*/     
//--------------------------------------------------MESH_DEFINITIONS---------------------------------------------------------------------------
#define CONFIG_MESH_CHANNEL                     1						/*	Если нужно использовать фиксированный канал	*/       
#define CONFIG_MESH_ROUTER_SSID                 "TATTELECOM_D143E0"		/*	SSID точки доступа		*/
#define CONFIG_MESH_ROUTER_PASSWD               "**ZDTW2*"				/*	Пароль точки доступа	*/
#define CONFIG_MESH_AP_AUTHMODE                 WIFI_AUTH_WPA2_PSK
#define CONFIG_MESH_AP_CONNECTIONS              6
#define CONFIG_MESH_NON_MESH_AP_CONNECTIONS     0
#define CONFIG_MESH_MAX_LAYER                   6
#define CONFIG_MESH_ROUTE_TABLE_SIZE            50
#define CONFIG_MESH_AP_PASSWD                   "123456789"
#define CONFIG_MESH_TOPOLOGY                    MESH_TOPO_TREE	/*	При желании поменять топологию с TREE на CHAIN поменять на _CHAIN	*/
/*	Задача реализующая удаленный доступ по MQTT. Принимает сообщение из mqtt_event_handler и обрабатывает его	*/   
void esp_mesh_rx_from_mqtt_and_tx_to_all_nodes(void *arg);
/*	Задача реализующая телеметрию. Отправляем данные в виде температуры или любые другие данные по желанию брокеру MQTT	*/
void esp_mesh_tx_to_mqtt(void *arg);
/*	Задача для приёма сообщений от брокера mqtt (удаленный доступ) 	*/
void esp_mesh_rx_from_nodes(void *arg);
/*	Функция создания всех нужных задач для функционирования сети на конкретном узле 	*/
esp_err_t esp_mesh_comm_p2p_start(void);
/*	Задача для работы с исполнительным механизмом. В данной реализации реле	*/
void relay_task(void *pvParametrs);
#endif