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
#define NODE_C_STA_MAC_STR     "ac:67:b2:3c:2e:1c"          // определить позже
#define NODE_D_STA_MAC_STR     "e0:5a:1b:76:9f:90"          // также

#define NODES_AMOUNT            4           // Количество НОД в сети. Менять по мере увеличения или уменьшения участников

//---------------------------------------------------UDP_DEFINITIONS---------------------------------------------------------------------------
#define CONFIG_CLIENT_PORT                      3333
#define CONFIG_SERVER_PORT                      4444
#define CONFIG_SERVER_IP                        "192.168.1.4"

//--------------------------------------------------MESH_DEFINITIONS---------------------------------------------------------------------------
#define CONFIG_MESH_CHANNEL                     1       
#define CONFIG_MESH_ROUTER_SSID                 "TATTELECOM_D143E0"
#define CONFIG_MESH_ROUTER_PASSWD               "36ZDTW2K"
#define CONFIG_MESH_AP_AUTHMODE                 WIFI_AUTH_WPA2_PSK
#define CONFIG_MESH_AP_CONNECTIONS              6
#define CONFIG_MESH_NON_MESH_AP_CONNECTIONS     0
#define CONFIG_MESH_MAX_LAYER                   6
#define CONFIG_MESH_ROUTE_TABLE_SIZE            50
//#define CONFIG_MESH_USE_GLOBAL_DNS_IP
#define CONFIG_MESH_AP_PASSWD                   "123456789"
#define CONFIG_MESH_TOPOLOGY                    MESH_TOPO_TREE

#define AMOUNT_OF_NODES                         4   // Макрос, который определяет на сколько топиков нужно подписываться

void esp_mesh_rx_from_mqtt_and_tx_to_all_nodes(void *arg);
void esp_mesh_tx_to_mqtt(void *arg);
void esp_mesh_rx_from_nodes(void *arg);
esp_err_t esp_mesh_comm_p2p_start(void);
void relay_task(void *pvParametrs);

#endif