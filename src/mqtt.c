#include "mqtt.h"

static const char *TAG = "mqtt";

esp_mqtt_client_handle_t client;        
xQueueHandle mqtt_queue_rx;             
xQueueHandle mqtt_queue_tx;             
EventGroupHandle_t mqtt_state_event_group;

static bool mqtt_connected_flag = false;     
static TaskHandle_t xRecvMqttTask = NULL, xTxToMqttTask = NULL;

static void log_error_if_nonzero(const char *message, int error_code)
{
    if (error_code != 0) {
      ESP_LOGE(TAG, "Last error %s: 0x%x", message, error_code);
    }
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%d", base, event_id);
    esp_mqtt_event_handle_t event = event_data;       
    client = event->client;                             
    int msg_id;
    mqttPayload payload;            
    char buff[40];
    switch ((esp_mqtt_event_id_t)event_id) 
    {    
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
            mqtt_connected_flag = true;
            for (int i = 0; i < AMOUNT_OF_NODES; i++)  
            {
                sniprintf(buff, sizeof(buff), "user_5c1a8cfe/relay%c", (char )(i+65));   
                printf("mqtt topic done: %s\n", buff);    
                msg_id = esp_mqtt_client_subscribe(client, buff, QoS_1);     
                ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);
            }
            xTaskCreate(esp_mesh_rx_from_mqtt_and_tx_to_all_nodes, "esp_mesh_rx_from_mqtt_and_tx_to_all_nodes",
                                             3092, NULL, 6, &xRecvMqttTask);  
            xTaskCreate(esp_mesh_tx_to_mqtt, "esp_mesh_tx_to_mqtt", 3092, NULL, 6, &xTxToMqttTask);
            xEventGroupSetBits(mqtt_state_event_group, MQTT_EVT_CONNECTED);
            break;

        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED!");
            if( mqtt_connected_flag )
            {
                xEventGroupClearBits(mqtt_state_event_group, MQTT_EVT_CONNECTED);
                vTaskDelete(xRecvMqttTask);     
                vTaskDelete(xTxToMqttTask);
                mqtt_connected_flag = false; 
            }
            break;

        case MQTT_EVENT_SUBSCRIBED:
            ESP_LOGI(TAG, "SUBSCRIBED ON TOPIC=%.*s\r\n", event->topic_len, event->topic);
            break;
			
        case MQTT_EVENT_UNSUBSCRIBED:
            ESP_LOGI(TAG, "UBSCRIBED ON TOPIC=%.*s\r\n", event->topic_len, event->topic);
            break;
        case MQTT_EVENT_PUBLISHED:
            break;
			
        case MQTT_EVENT_DATA:
            ESP_LOGI(TAG, "MQTT_EVENT_DATA");
            printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
            snprintf(payload.str_topic, sizeof(payload.str_topic), "%.*s", event->topic_len, event->topic); 
            printf("data recieved: topic: %s\n", payload.str_topic);
            printf("DATA=%.*s\r\n", event->data_len, event->data);
            snprintf(payload.str_data, sizeof(payload.str_data), "%.*s", event->data_len, event->data);     
            xQueueSendToBack(mqtt_queue_rx, &payload, 0);      
            break;
			
        case MQTT_EVENT_BEFORE_CONNECT:
            break;
			
        case MQTT_EVENT_DELETED:
            break;
			
        case MQTT_EVENT_ERROR:
            ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
            if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
                log_error_if_nonzero("reported from esp-tls", event->error_handle->esp_tls_last_esp_err);
                log_error_if_nonzero("reported from tls stack", event->error_handle->esp_tls_stack_err);
                log_error_if_nonzero("captured as transport's socket errno",  event->error_handle->esp_transport_sock_errno);
                ESP_LOGI(TAG, "Last errno string (%s)", strerror(event->error_handle->esp_transport_sock_errno));
            }
            break;
			
        default:
            ESP_LOGI(TAG, "Other event id:%d", event->event_id);
            break;
    }
}

void mqtt_start(void)
{
    esp_mqtt_client_config_t mqtt_cfg = {   
      .uri = "mqtt://srv2.clusterfly.ru",
      .password = BROKER_PASSWORD,
      .username = BROKER_USERNAME,
      .port = 9991
    };
    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(client);
}