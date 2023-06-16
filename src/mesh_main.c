
/*
=======================================================================================================
				#####	Для чего нужен этот код? Особенности	####
=======================================================================================================

- Код нужен для построения wifi-mesh сети по древовидной или chain топологии на микроконтроллерах ESP32.

- Программно назначенных ролей у узлов в сети нет, все определяется динамически в ходе "выборов", которые
  реализованы в API esp_wifi_mesh, когда устройства, регулярно обмениваясь маячковыми фреймами, следят
  чтобы к роутеру был подключен узел с самым сильным уровнем сигнала.
  
- Реализован удаленный доступ к каждому узлу в сети посредством протокола MQTT и брокера clusterfly.ru

- Реализована телеметрия также через MQTT(в данном коде только заранее известные числа).

- Код универсален для каждого устройства в сети. Чтобы перенести его на новое устройство, необходимо
  изменить все строки, в которых участвует строка с топиком (например relayA при переносе кода 
  на новое устройство заменить на relayB, также с tempA -> tempB и т.д по алфавиту, смотря сколько
  устройств в данный момент было использовано). 
  
  Также необходимо добавить MAC адрес добавляемого устройства в хедер mesh_main, назвав её 
  NODE_X_STA_MAC_STR, где X - буква добавляемого устройства (например если их было 4, то 5-ый узел
  будет иметь букву E согласно алфавиту), а также увеличить AMOUNT_OF_NODES на 1.
  
  При необходимости работы с конкретными устройствами или датчиками добавить задачи в код и обеспечить
  межпотоковое взаимодействие этих задач с esp_mesh_p2p_tx_main и esp_mesh_p2p_rx_main
   
- У узлов есть возможность работать сети в в энергосберегающем режиме, определив CONFIG_MESH_ENABLE_PS

- Чтобы изменить топологию с древовидной на chain нужно изменить в хедере mesh_main CONFIG_MESH_TOPOLOGY
  на MESH_TOPOLOGY_CHAIN
 
*/
#include "mesh_main.h"

/*******************************************************
 *                Variable Definitions
 *******************************************************/
static const char *MESH_TAG = "mesh_main";

static const uint8_t MESH_ID[6] = { 0x77, 0x77, 0x77, 0x77, 0x77, 0x77};
static bool is_running = true;
static bool is_mesh_connected = false;
static mesh_addr_t mesh_parent_addr;
static int mesh_layer = -1;
static esp_netif_t *netif_sta = NULL;

static bool parent_is_connected = false;
static bool is_comm_p2p_started = false;        

xSemaphoreHandle relay_semaphore;

static uint8_t relay_on_or_off = RELAY_OFF;

void relay_task(void *pvParametrs)                  
{
    for( ;; )
    {
        xSemaphoreTake(relay_semaphore, portMAX_DELAY);     
		printf("Took relay semaphore!!\n");
        if(relay_on_or_off == RELAY_OFF) 
        {
            gpio_set_level(relay_gpio, 0);  
        }
        else if (relay_on_or_off == RELAY_ON)
        {
            gpio_set_level(relay_gpio, 1);   
        }
    }
}

void esp_mesh_tx_to_mqtt(void *arg)
{
    mqttPayload msg;
    int msg_id;
    while (is_running)
    {
        xQueueReceive(mqtt_queue_tx, &msg, portMAX_DELAY);
        printf("recv from mqtt_queue_tx: %s/%s\n", msg.str_topic, msg.str_data);
        msg_id = esp_mqtt_client_publish(client, msg.str_topic, msg.str_data, 0, 1, 0);
        ESP_LOGI(MESH_TAG, "sent msg successful, msg_id=%d", msg_id);
        vTaskDelay(1200 / portTICK_RATE_MS);    
    }
}

void esp_mesh_rx_from_mqtt_and_tx_to_all_nodes(void *arg)
{
    esp_err_t err;
    mesh_addr_t route_table[CONFIG_MESH_ROUTE_TABLE_SIZE];
    int route_table_size = 0;
    mesh_data_t data;
    char str[81];            
    char mac_str[18];
    data.size = sizeof(str);
    data.proto = MESH_PROTO_BIN;
    data.tos = MESH_TOS_P2P;
    is_running = true;
    mqttPayload payload;    
    {
        xQueueReceive(mqtt_queue_rx, &payload, portMAX_DELAY);     
        esp_mesh_get_routing_table((mesh_addr_t *) &route_table,
                                   CONFIG_MESH_ROUTE_TABLE_SIZE * 6, &route_table_size); 
        printf("Route table SIZE: %d \n", route_table_size);
        for (int i = 0; i < route_table_size; i++)
        {
            printf("Route table consist of: %d. MAC: "MACSTR" \n", i, MAC2STR(route_table[i].addr));
        }
        snprintf(str, sizeof(str), "%s/%s", payload.str_topic, payload.str_data);
        printf("Our message to broadcast: %s\n", str);        
        data.data = (uint8_t *)str;     
        if( strcmp((char *)data.data, "user_f8c55df1/relayA/0") == 0)   
        {
            relay_on_or_off = RELAY_OFF;
            xSemaphoreGive(relay_semaphore);
            continue;
        }
        else if ( strcmp((char *)data.data, "user_f8c55df1/relayA/1") == 0) 
        {
            relay_on_or_off = RELAY_ON;
            xSemaphoreGive(relay_semaphore);
            continue;
        }
        for (int i = 0; i < route_table_size; i++) 
        {
            snprintf(mac_str, sizeof(mac_str) , MACSTR,
            route_table[i].addr[0],
            route_table[i].addr[1],
            route_table[i].addr[2],
            route_table[i].addr[3],
            route_table[i].addr[4],
            route_table[i].addr[5]
            );
            printf("mac to send: %s\n", mac_str);
            if( strcmp(mac_str, NODE_A_STA_MAC_STR) == 0)
            {
                ESP_LOGE(MESH_TAG, "That was me, skipping!");
                continue;
            }
            err = esp_mesh_send(&route_table[i], &data, MESH_DATA_P2P, NULL, 0);    
            if (err) {
                ESP_LOGE(MESH_TAG,
                         "[ROOT-2-UNICAST][L:%d]parent:"MACSTR" to "MACSTR", heap:%d[err:0x%x, proto:%d, tos:%d]",
                            mesh_layer, MAC2STR(mesh_parent_addr.addr),
                         MAC2STR(route_table[i].addr), esp_get_minimum_free_heap_size(),
                         err, data.proto, data.tos);
            } 
        }
    }
    if( esp_mesh_is_root() ) vQueueDelete(mqtt_queue_rx);
    vTaskDelete(NULL);
}

void esp_mesh_p2p_tx_main(void *arg)
{
    esp_err_t err;
    mesh_addr_t route_table[CONFIG_MESH_ROUTE_TABLE_SIZE];
    int route_table_size = 0;
    mesh_data_t data;
    mqttPayload payload;
    char str_to_mqtt[81];    
    char *str_topic = "user_f8c55df1/tempA";
    char *str_data = heap_caps_malloc(sizeof(str_data), MALLOC_CAP_8BIT);
    int temperature = 24;
    data.data = (uint8_t *)str_to_mqtt;       
    data.size = sizeof(str_to_mqtt);
    data.proto = MESH_PROTO_BIN;
    data.tos = MESH_TOS_P2P;
    is_running = true;
    while (is_running) {    
        if(esp_mesh_is_root())
        {
            EventBits_t bits = xEventGroupWaitBits(mqtt_state_event_group,  
                    MQTT_EVT_CONNECTED,
                    pdFALSE,
                    pdTRUE,
                    portMAX_DELAY);
            esp_mesh_get_routing_table((mesh_addr_t *) &route_table,
                                   CONFIG_MESH_ROUTE_TABLE_SIZE * 6, &route_table_size);
            snprintf(payload.str_topic, sizeof(payload.str_topic), str_topic);  
            printf("payload.str_topic is: %s\n", payload.str_topic);  
            itoa(temperature, str_data, 10);   
            snprintf(payload.str_data, sizeof(payload.str_data), str_data);  
            printf("payload.str_data is: %s\n", payload.str_data);
            xQueueSendToBack(mqtt_queue_tx, &payload, portMAX_DELAY);        
            vTaskDelay(2000/ portTICK_RATE_MS);                              
        }
        else 
        {
            esp_mesh_get_routing_table((mesh_addr_t *) &route_table,
                                   CONFIG_MESH_ROUTE_TABLE_SIZE * 6, &route_table_size);    
            itoa(temperature, str_data, 10);    
            snprintf(str_to_mqtt, sizeof(str_to_mqtt), "%s/%s", str_topic, str_data);
            printf("str_to_mqtt is: %s\n", str_to_mqtt);
            data.data = (uint8_t *)str_to_mqtt;      
            err = esp_mesh_send(NULL, &data, MESH_DATA_TODS, NULL, 0);   
            if (err) {
                    ESP_LOGE(MESH_TAG,
                            "[ROOT-2-UNICAST][L:%d]parent:"MACSTR" to n, heap:%d[err:0x%x, proto:%d, tos:%d]",
                            mesh_layer, MAC2STR(mesh_parent_addr.addr),
                            esp_get_minimum_free_heap_size(),
                            err, data.proto, data.tos);
            }
            if (route_table_size < 10) {
                vTaskDelay(1 * 1000 / portTICK_RATE_MS);
            }  
        } 
    }
    if( esp_mesh_is_root() ) vQueueDelete(mqtt_queue_tx);
    vTaskDelete(NULL);
}

void esp_mesh_rx_from_nodes(void *arg)
{
    mesh_addr_t route_table[CONFIG_MESH_ROUTE_TABLE_SIZE];
    int route_table_size = 0;
    esp_err_t err;
    mesh_addr_t from;
    mesh_data_t data;
    int flag = 0;
    int i = 0, j = 0;            
    char str_topic[40];
    char str_data[40];
    data.data = (uint8_t *)str;   
    data.size = sizeof(str);       
    is_running = true;
    mqttPayload payload;
    char *buff; 
    while (is_running) {

        if(esp_mesh_is_root())
        {
            EventBits_t bits = xEventGroupWaitBits(mqtt_state_event_group, 
                    MQTT_EVT_CONNECTED,
                    pdFALSE,
                    pdTRUE,
                    portMAX_DELAY);
            esp_mesh_get_routing_table((mesh_addr_t *) &route_table,
                                   CONFIG_MESH_ROUTE_TABLE_SIZE * 6, &route_table_size);
            err = esp_mesh_recv(&from, &data, portMAX_DELAY, &flag, NULL, 0);   
            if (err != ESP_OK || !data.size) {
                ESP_LOGE(MESH_TAG, "err:0x%x, size:%d", err, data.size);
                continue;
            }
            printf("recv msg from node: %s\n", (char *)data.data );
            buff = (char *)data.data;
            strcpy(str_topic, "user_f8c55df1/");   
            i = strlen(str_topic);                  
            while (i < 80)        
            {
                if(buff[i+1] >= 48 && buff[i+1] <= 57)  
                {
                    str_topic[i] = '\0';    
                    i++;    
                    break;  
                }
                str_topic[i] = buff[i];
                i++;
            }
            printf("Parsed TOPIC is: %s\n", (char *)str_topic);  
            j = 0;      
            while (buff[i] != '\0')
            {
                str_data[j] = buff[i];
                i++;
                j++;
            }   
            str_data[j] = '\0';   
            printf("Parsed DATA is: %s\n", (char *)str_data);
            sniprintf(payload.str_topic, sizeof(payload.str_topic), str_topic);     
            sniprintf(payload.str_data, sizeof(payload.str_data), str_data);
            xQueueSendToBack(mqtt_queue_tx, &payload, 500 / portTICK_RATE_MS);
        }
        else 
        {    
            printf("WE are waiting to the packet!!\n");
            err = esp_mesh_recv(&from, &data, portMAX_DELAY, &flag, NULL, 0);
            if (err != ESP_OK || !data.size) {
                ESP_LOGE(MESH_TAG, "err:0x%x, size:%d", err, data.size);
                continue;
            }
            if( strcmp((char *)data.data, "user_f8c55df1/relayA/0") == 0)      
            {
                printf("Relay A OFF!!\n");
                relay_on_or_off = RELAY_OFF;
                xSemaphoreGive(relay_semaphore);
            }
            else if(strcmp((char *)data.data, "user_f8c55df1/relayA/1") == 0)
            {
                printf("Relay A OFF!!\n");
                relay_on_or_off = RELAY_ON;
                xSemaphoreGive(relay_semaphore);
            }
            else
            {
                printf("Str didn't match!!!\n");
            }
        }
    }
    if( esp_mesh_is_root() ) vQueueDelete(mqtt_queue_tx);
    vTaskDelete(NULL);
}

esp_err_t esp_mesh_comm_p2p_start(void)
{
    if (!is_comm_p2p_started) {
        is_comm_p2p_started = true;    
        if(esp_mesh_is_root())     
        {
            mqtt_state_event_group = xEventGroupCreate();          
            mqtt_queue_rx = xQueueCreate(10, sizeof(mqttPayload));
            mqtt_queue_tx = xQueueCreate(10, sizeof(mqttPayload)); 
        } 
        
        vSemaphoreCreateBinary(relay_semaphore);
        vTaskDelay(50/ portTICK_RATE_MS);
        xTaskCreate(esp_mesh_rx_from_nodes, "MPRX", 3072, NULL, 5, NULL);  
        xTaskCreate(esp_mesh_p2p_tx_main, "p2p_tx_task", 3072, NULL, 5, NULL);
        xTaskCreate(relay_task, "relay_task", 3072, NULL, 6, NULL);
    }
    return ESP_OK;
}

void mesh_event_handler(void *arg, esp_event_base_t event_base,
                        int32_t event_id, void *event_data)
{
    mesh_addr_t id = {0,};
    static uint16_t last_layer = 0;

    switch (event_id) {
    case MESH_EVENT_STARTED: {
        esp_mesh_get_id(&id);
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_MESH_STARTED>ID:"MACSTR"", MAC2STR(id.addr));
        is_mesh_connected = false;
        mesh_layer = esp_mesh_get_layer();
    }
    break;
    case MESH_EVENT_STOPPED: {
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_STOPPED>");
        is_mesh_connected = false;
        mesh_layer = esp_mesh_get_layer();
    }
    break;
    case MESH_EVENT_CHILD_CONNECTED: {
        mesh_event_child_connected_t *child_connected = (mesh_event_child_connected_t *)event_data;
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_CHILD_CONNECTED>aid:%d, "MACSTR"",
                 child_connected->aid,
                 MAC2STR(child_connected->mac));
    }
    break;
    case MESH_EVENT_CHILD_DISCONNECTED: {
        mesh_event_child_disconnected_t *child_disconnected = (mesh_event_child_disconnected_t *)event_data;
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_CHILD_DISCONNECTED>aid:%d, "MACSTR"",
                 child_disconnected->aid,
                 MAC2STR(child_disconnected->mac));
    }
    break;
    case MESH_EVENT_ROUTING_TABLE_ADD: {
        mesh_event_routing_table_change_t *routing_table = (mesh_event_routing_table_change_t *)event_data;
        ESP_LOGW(MESH_TAG, "<MESH_EVENT_ROUTING_TABLE_ADD>add %d, new:%d, layer:%d",
                 routing_table->rt_size_change,
                 routing_table->rt_size_new, mesh_layer);
    }
    break;
    case MESH_EVENT_ROUTING_TABLE_REMOVE: {
        mesh_event_routing_table_change_t *routing_table = (mesh_event_routing_table_change_t *)event_data;
        ESP_LOGW(MESH_TAG, "<MESH_EVENT_ROUTING_TABLE_REMOVE>remove %d, new:%d, layer:%d",
                 routing_table->rt_size_change,
                 routing_table->rt_size_new, mesh_layer);
    }
    break;
    case MESH_EVENT_NO_PARENT_FOUND: {
        mesh_event_no_parent_found_t *no_parent = (mesh_event_no_parent_found_t *)event_data;
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_NO_PARENT_FOUND>scan times:%d",
                 no_parent->scan_times);
    }
    break;
    case MESH_EVENT_PARENT_CONNECTED: {
        mesh_event_connected_t *connected = (mesh_event_connected_t *)event_data;
        esp_mesh_get_id(&id);
        mesh_layer = connected->self_layer;
        memcpy(&mesh_parent_addr.addr, connected->connected.bssid, 6);
        ESP_LOGI(MESH_TAG,
                 "<MESH_EVENT_PARENT_CONNECTED>layer:%d-->%d, parent:"MACSTR"%s, ID:"MACSTR", duty:%d",
                 last_layer, mesh_layer, MAC2STR(mesh_parent_addr.addr),
                 esp_mesh_is_root() ? "<ROOT>" :
                 (mesh_layer == 2) ? "<layer2>" : "", MAC2STR(id.addr), connected->duty);
        last_layer = mesh_layer;
        is_mesh_connected = true;
        is_running = true;
        if (esp_mesh_is_root()) {
            esp_netif_dhcpc_stop(netif_sta);
            esp_netif_dhcpc_start(netif_sta);
        }
        gpio_set_level(led_gpio, 1);
        parent_is_connected = true;

        esp_mesh_comm_p2p_start();
    }
    break;
    case MESH_EVENT_PARENT_DISCONNECTED: {
        mesh_event_disconnected_t *disconnected = (mesh_event_disconnected_t *)event_data;
        ESP_LOGI(MESH_TAG,
                 "<MESH_EVENT_PARENT_DISCONNECTED>reason:%d",
                 disconnected->reason);
        mesh_layer = esp_mesh_get_layer();
        gpio_set_level(led_gpio, 0); 

        if( esp_mesh_is_root() && is_mesh_connected ) 
        {
            esp_mqtt_client_stop(client);       
            esp_mqtt_client_destroy(client);
            xEventGroupClearBits(mqtt_state_event_group, MQTT_EVT_CONNECTED);
            is_mesh_connected = false;
        }
    }
    break;
    case MESH_EVENT_LAYER_CHANGE: {
        mesh_event_layer_change_t *layer_change = (mesh_event_layer_change_t *)event_data;
        mesh_layer = layer_change->new_layer;
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_LAYER_CHANGE>layer:%d-->%d%s",
                 last_layer, mesh_layer,
                 esp_mesh_is_root() ? "<ROOT>" :
                 (mesh_layer == 2) ? "<layer2>" : "");
        last_layer = mesh_layer;
    }
    break;
    case MESH_EVENT_ROOT_ADDRESS: {
        mesh_event_root_address_t *root_addr = (mesh_event_root_address_t *)event_data;
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_ROOT_ADDRESS>root address:"MACSTR"",
                 MAC2STR(root_addr->addr));
    }
    break;
    case MESH_EVENT_VOTE_STARTED: {
        mesh_event_vote_started_t *vote_started = (mesh_event_vote_started_t *)event_data;
        ESP_LOGI(MESH_TAG,
                 "<MESH_EVENT_VOTE_STARTED>attempts:%d, reason:%d, rc_addr:"MACSTR"",
                 vote_started->attempts,
                 vote_started->reason,
                 MAC2STR(vote_started->rc_addr.addr));
    }
    break;
    case MESH_EVENT_VOTE_STOPPED: {
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_VOTE_STOPPED>");
        break;
    }
    case MESH_EVENT_ROOT_SWITCH_REQ: {
        mesh_event_root_switch_req_t *switch_req = (mesh_event_root_switch_req_t *)event_data;
        ESP_LOGI(MESH_TAG,
                 "<MESH_EVENT_ROOT_SWITCH_REQ>reason:%d, rc_addr:"MACSTR"",
                 switch_req->reason,
                 MAC2STR( switch_req->rc_addr.addr));
    }
    break;
    case MESH_EVENT_ROOT_SWITCH_ACK: {
        mesh_layer = esp_mesh_get_layer();
        esp_mesh_get_parent_bssid(&mesh_parent_addr);
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_ROOT_SWITCH_ACK>layer:%d, parent:"MACSTR"", mesh_layer, MAC2STR(mesh_parent_addr.addr));
    }
    break;
    case MESH_EVENT_TODS_STATE: {
        mesh_event_toDS_state_t *toDs_state = (mesh_event_toDS_state_t *)event_data;
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_TODS_REACHABLE>state:%d", *toDs_state);
    }
    break;
    case MESH_EVENT_ROOT_FIXED: {
        mesh_event_root_fixed_t *root_fixed = (mesh_event_root_fixed_t *)event_data;
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_ROOT_FIXED>%s",
                 root_fixed->is_fixed ? "fixed" : "not fixed");
    }
    break;
    case MESH_EVENT_ROOT_ASKED_YIELD: {
        mesh_event_root_conflict_t *root_conflict = (mesh_event_root_conflict_t *)event_data;
        ESP_LOGI(MESH_TAG,
                 "<MESH_EVENT_ROOT_ASKED_YIELD>"MACSTR", rssi:%d, capacity:%d",
                 MAC2STR(root_conflict->addr),
                 root_conflict->rssi,
                 root_conflict->capacity);
    }
    break;
    case MESH_EVENT_CHANNEL_SWITCH: {
        mesh_event_channel_switch_t *channel_switch = (mesh_event_channel_switch_t *)event_data;
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_CHANNEL_SWITCH>new channel:%d", channel_switch->channel);
    }
    break;
    case MESH_EVENT_SCAN_DONE: {
        mesh_event_scan_done_t *scan_done = (mesh_event_scan_done_t *)event_data;
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_SCAN_DONE>number:%d",
                 scan_done->number);
    }
    break;
    case MESH_EVENT_NETWORK_STATE: {
        mesh_event_network_state_t *network_state = (mesh_event_network_state_t *)event_data;
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_NETWORK_STATE>is_rootless:%d",
                 network_state->is_rootless);
    }
    break;
    case MESH_EVENT_STOP_RECONNECTION: {
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_STOP_RECONNECTION>");
    }
    break;
    case MESH_EVENT_FIND_NETWORK: {
        mesh_event_find_network_t *find_network = (mesh_event_find_network_t *)event_data;
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_FIND_NETWORK>new channel:%d, router BSSID:"MACSTR"",
                 find_network->channel, MAC2STR(find_network->router_bssid));
    }
    break;
    case MESH_EVENT_ROUTER_SWITCH: {
        mesh_event_router_switch_t *router_switch = (mesh_event_router_switch_t *)event_data;
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_ROUTER_SWITCH>new router:%s, channel:%d, "MACSTR"",
                 router_switch->ssid, router_switch->channel, MAC2STR(router_switch->bssid));
    }
    break;
}

void ip_event_handler(void *arg, esp_event_base_t event_base,
                      int32_t event_id, void *event_data)
{
    ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;
    ESP_LOGI(MESH_TAG, "<IP_EVENT_STA_GOT_IP>IP:" IPSTR, IP2STR(&event->ip_info.ip));
    mqtt_start();   
}

void app_main(void)
{
    gpio_reset_pin(led_gpio);
    gpio_reset_pin(relay_gpio);
    gpio_set_direction(led_gpio, GPIO_MODE_OUTPUT);    
    gpio_set_direction(relay_gpio, GPIO_MODE_OUTPUT);   
    gpio_set_level(led_gpio, 0);
    gpio_set_level(relay_gpio, 0);
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(esp_netif_create_default_wifi_mesh_netifs(&netif_sta, NULL));
    wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&config));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &ip_event_handler, NULL));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_FLASH));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_mesh_init());
    ESP_ERROR_CHECK(esp_event_handler_register(MESH_EVENT, ESP_EVENT_ANY_ID, &mesh_event_handler, NULL));
    ESP_ERROR_CHECK(esp_mesh_set_topology(CONFIG_MESH_TOPOLOGY));
    ESP_ERROR_CHECK(esp_mesh_set_max_layer(CONFIG_MESH_MAX_LAYER));
    ESP_ERROR_CHECK(esp_mesh_set_vote_percentage(1));
    ESP_ERROR_CHECK(esp_mesh_set_xon_qsize(128));
#ifdef CONFIG_MESH_ENABLE_PS
    ESP_ERROR_CHECK(esp_mesh_enable_ps());
    ESP_ERROR_CHECK(esp_mesh_set_ap_assoc_expire(60));
    ESP_ERROR_CHECK(esp_mesh_set_announce_interval(600, 3300));
#else
    ESP_ERROR_CHECK(esp_mesh_disable_ps());                 
    ESP_ERROR_CHECK(esp_mesh_set_ap_assoc_expire(10));      
#endif
    mesh_cfg_t cfg = MESH_INIT_CONFIG_DEFAULT();
    memcpy((uint8_t *) &cfg.mesh_id, MESH_ID, 6);
    cfg.allow_channel_switch = true;
    cfg.router.ssid_len = strlen(CONFIG_MESH_ROUTER_SSID);
    memcpy((uint8_t *) &cfg.router.ssid, CONFIG_MESH_ROUTER_SSID, cfg.router.ssid_len);
    memcpy((uint8_t *) &cfg.router.password, CONFIG_MESH_ROUTER_PASSWD,
           strlen(CONFIG_MESH_ROUTER_PASSWD));
    ESP_ERROR_CHECK(esp_mesh_set_ap_authmode(CONFIG_MESH_AP_AUTHMODE));
    cfg.mesh_ap.max_connection = CONFIG_MESH_AP_CONNECTIONS;
    cfg.mesh_ap.nonmesh_max_connection = CONFIG_MESH_NON_MESH_AP_CONNECTIONS;
    memcpy((uint8_t *) &cfg.mesh_ap.password, CONFIG_MESH_AP_PASSWD,
           strlen(CONFIG_MESH_AP_PASSWD));
    ESP_ERROR_CHECK(esp_mesh_set_config(&cfg));
    ESP_ERROR_CHECK(esp_mesh_start());
#ifdef CONFIG_MESH_ENABLE_PS
    ESP_ERROR_CHECK(esp_mesh_set_active_duty_cycle(CONFIG_MESH_PS_DEV_DUTY, CONFIG_MESH_PS_DEV_DUTY_TYPE));
    ESP_ERROR_CHECK(esp_mesh_set_network_duty_cycle(CONFIG_MESH_PS_NWK_DUTY, CONFIG_MESH_PS_NWK_DUTY_DURATION, CONFIG_MESH_PS_NWK_DUTY_RULE));
#endif
    ESP_LOGI(MESH_TAG, "mesh starts successfully, heap:%d, %s<%d>%s, ps:%d\n",  esp_get_minimum_free_heap_size(),
             esp_mesh_is_root_fixed() ? "root fixed" : "root not fixed",
             esp_mesh_get_topology(), esp_mesh_get_topology() ? "(chain)":"(tree)", esp_mesh_is_ps_enabled());
}
