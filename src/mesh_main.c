
/*
    This example demonstrates how to use the mesh APIs to set up a mesh network, send and receive messages over the mesh network and etc.

    Features Demonstrated:
- mesh initialization
- mesh configuration
- mesh start
- mesh event handler

- root send and receive
- other nodes receive

    !!!! Канал CONFIG_MESH_CHANNEL должен совпадать с каналом роутера, который можно посмотреть по адресу 192.168.1.1 в:
    Сеть -> слева WLAN и там номер канала.

    Пока что попробую с root ноды слать в роутер, т.е во внешнюю IP сеть инфу о себе и других участниках сети по TCP. Отслеживать буду 
    через netcat и WireShark

Прога работает так для ROOT ноды: 

    вначале идёт инциализация nvs хранилища, TCP/IP стека, wifi а затем уже mesh. Все нужные настрйоки роуетра мы вбили.
    Затем нода автоматоически сканирует пространство на роутер и находит его, начинает голосование, в котором все сканируют сигнальные
    фрэймы других и выбирают наилучший показатель RSSI (чем меньшее -db тем лучше) и после 10 туров голосования
    (задаём функцией esp_mesh_set_ap_assoc_expire(10)), выбирается корневая нода, затем подключается к роутеру, а все остальные подключаются 
    к ней. В логах можно ппрям увидеть, голосует нода за себя или за другой MAC. Если за себя, то так и написано "vote myself" и подробно
    отображается сколько всего голосов собрано, какой это тур и прочая полезная инфа.
        Более подробно: избранная голосванием нода, подключается к роуетеру, в логах отображается канал, aid, bssid(это по сути MAC роутера) и тд.
    Только после этого подключения происходит событие MESH_EVENT_PARENT_CONNECTED, о чём свидетльствуют логи. Отображается слой, на котором мы
    находимся и на котором находились (0-->1). Выводится MAC родителя и ID соединения (задали в #define). Затем возникает событие 
    MESH_EVENT_TODS_STATE, которое несет в себе инфу о том, есть ли возможность у корневой ноды получить доступ к внешней IP сети. Если да, то 
    state будет 0 (state.0).
        Далее происходит событие MESH_EVENT_ROOT_ADDRESS, которое означает, что адрес корневой ноды получен и присвоен (пока только MAC). Этот
    MAC адрес будет являтся MAC-ом его AP, который у меня записан в гугл заметках. Затем, так как мы запустили DHCP client на стадии, когда 
    подключились к родителю(к роутеру) прям там из хэндлера, мы приобретаем IP адрес в ходе DHCP запроса-ответа и попадаем в хэндлер ip события,
    где видим наш айпишник. И далее после этого мы подключаемся к нашему родителю - роутеру, имея свой айпи для взаимодействия с ним. В этой
    ветке switch case мы зажигаем светодиод, сигнализирующий об установленном соединении и вызываем функцию для создания задач на приём и 
    передачу.
        
    Для НЕ ROOT ноды: Всео почти тоже самое, и есть 2 случая. 1 - когда ноды включаются одновременно, начинается голосование, выбирается корневая
    нода, дальше же все как обычно, просто для не корневой ноды не запускается dhcp клиент и в задаче на приём кое-что отключено. И по ходу
    выполнения какой-либо задачи, эта самая нода постоянно мониторит на уровень rssi другие ноды, и если уровень выше, чем у самой этой ноды, то
    она осталвяет её в покое на какое-то время, затем снова проверяет на сигнальные фреймы. Таким образом реализуется самоорганизация сети с 
    наиболее выгодными показателями и любая нода может инициировать новый тур голосования, если корневая нода вдруг начала плохо принимать сигнал
    роутера и заимела rssi хуже, чем у какой-нибудь ноды.

       * Важно с какой ноды ноды мы смотрим route_table. Потому что таблица маршрутизации включает в себя только устрйоства которые лежат 
       по нисходящей и саму себя. Те, кто на более низком слое уже не войдут в таблицу маршрутизации *

    Подумать нужно ли удалять задачи, при отключении родителя. Обычная нода так и будет продолжать слать в никуда по идее, если не удалить задачу
    Корневая нода будет висеть и ждать таймаута пакета, а потом хз что будет, надо протестить. Выключить обычную ноду и смотреть. Затем когда
    произойдёт таймаут, включить обычную ноду и посмотреть будет ли задача как ни в чем не бывало принимать пакеты обычной ноды. Пока закоменчу
    эту функцию и её вызов (esp_mesh_comm_p2p_stop_and_delete()). А может просто удалять задачу из самих них, чтобы не париться с их
    выносными хэндлами? То есть где надо занулять флаг is_running и поднимать его при подключении. 

    Узнаем таблицу маршрутизации сети, выведя её на экран. Нужно затестить, что будет, если корневая нода слушает пустоту долгое время. И может
    тогда задачу удалять и не нужно, если в случае с переподключением потомка все снова заработает. 

                                    ТЕЗИСЫ

    1. Если нода стала корневой, то должна создаваться TX_FROMDS задача, в которой мы ждём инфу из очереди (либо mqtt таска либо tcp таска). Как только
    мы эту инфу получаем из очереди, допустим принудительное включение реле на ноде B, мы должны сразу отправить команду назначителю.
    2. Если нода не корневая то создается другая TX задача - передача инфы (температуры) корневой ноде.
    3. Задача по приёму создается на всех нодах. На ноде C код чуть отличается тем, что в условие if() помещено условие совпадения
    Мака ноды B и обработка температры того узла. 
    4. Функция TX ноды В также имеет немного другой вид. Все тоже самое - отправка корневой ноде своей температуры, но ещё и раз в 10 сек 
    (можно реализовать через send_counter == 10 т.к там в конце всегда задержка потока на 1 сек) отправлять ноде С с определённым
    МАКом свою температуру.
    5. У каждой ноды есть свой исполнительный механизм, соответственно каждая нода должна держать ушки на макушке и слушать запросы
    от корневой ноды. Эмуляцией этого исполнительного механизма может быть реле.

    То есть Нода 1 должнга ещё посылать помимо корневйо ноды свою температуру, ещё и ноде 2. У ноды 1 будет дополнительная отправка p2p
    У ноды 2 будет создана задача приёма и передачи. В задаче приёма будет происходить отдача семафора скорее всего для обработки
    принятого сигнала. Корневая нода будет иметь хз пока сколько задач.

    * Моделируем ситуацию, как будто потмоок отвалился и мы слушаем пустоту бесконечно. Вывалимся ли мы? Нужно ли в таком лучае удалять задачу или
    оставить если она не вывалится. По идее на ней самой будет датчик температуры висеть, поэтому одна задача будет работать все равно, должна
    Ответ: нет, не вывалимся, задачу по приёму на корневой ноде можно не удалять. *

    Для ноды 1, которая ещё и отправляет температуру ноде 2, в любом случае будет создаваться задача tx конкретному маку, но также там будет 
    стоять проверка на корневость, и если нода 1 - корневая, то мы дополнительно шлём по конкретному MAC-у температуру

    Где должна создаваться задача реле, и где должен создаваться семафор? Одно известно точно, что каждая нода должна быть готова принудительно
    включить или выключить реле по запросу из вне (из внешней айпи сети). Нода С отличается от нод А,В,D, т.к у неё ещё есть доп. ветки 
    с if которые  отслеживают температуру на ноде В.

    Задача esp_mesh_rx_from_mqtt_and_tx_to_all_nodes создается только на корневой ноде, она приоритенее всего, т.к она в основном заблокирована,
    то она не будет мешать другим. С другой же стороны если придёт запрос mqtt то из ветки ESP_EVENT_DATA в очередь передасться сообщение, 
    которое мы немедленно примем из-за высшего приоритета и отправим сразу всем а затем снова заблокируемся до следуюущего запроса. 

    Задача esp_mesh_tx_to_mqtt тоже имеет приоритет 6 и тоже создается только на корневой ноде, но тоже ждёт сообщения из очереди и когда его 
    получает, отправляет по MQTT.

    Задача esp_mesh_rx_from_nodes создается на всех нодах сети, принимает пакеты от других нод, парсит их на топик и дату и отправляет 
    в очередь в задачу esp_mesh_tx_to_mqtt

    Задача esp_mesh_p2p_tx_main создается на всех нодах и делает то, что измеряет температуру на своём датчике и в стандартном виде 
    присовокупляет к topic/

    Задача relay_task на всех нодах

    Группа битов, а точнее бит MQTT_EVT_CONNECTED нужны не для самих задач mqtt, а для задач на отправку и приём от mqtt чтобы предотвратить
    чтение или запись в из/в несуществующую очередь. При отключении от mqtt задачи на приём с mqtt и передачу туда удаляются. Очереди остаются
    дабы избежать бага, когда идёт чтение из очереди до того как задача заблочится на ожидание бита соединения. Группа битов создается в 
    esp_mesh_main_p2p_start при условии что мы корневая нода, там же создаются один раз на всю сессию и очереди в том же месте.

                      Этот код будет для ноды A.
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


static bool is_comm_p2p_started = false;        // Глобально будем отслеживать созданы ли наши задачи или удалены

xSemaphoreHandle relay_semaphore;


static uint8_t relay_on_or_off = RELAY_OFF;

void relay_task(void *pvParametrs)                  // таск для работы с реле, как приндутильное вкл/выкл, так и по температуре
{
    
    for( ;; )
    {
        printf("Relay task!!\n");
        xSemaphoreTake(relay_semaphore, portMAX_DELAY);     // Ждём пока нам не отдадут семафор
        if(relay_on_or_off == RELAY_OFF) 
        {
            gpio_set_level(relay_gpio, 0);  // Выключаем реле
        }
        else if (relay_on_or_off == RELAY_ON)
        {
            gpio_set_level(relay_gpio, 1);   // Включаем реле
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
        vTaskDelay(1200 / portTICK_RATE_MS);    // нельзя обращаться к брокеру чаще раза в секунду!!
    }
}

void esp_mesh_rx_from_mqtt_and_tx_to_all_nodes(void *arg)
{
    esp_err_t err;
    mesh_addr_t route_table[CONFIG_MESH_ROUTE_TABLE_SIZE];
    /*
        Инициализируем UNION, который хранит CONFIG_MESH_ROUTE_TABLE_SIZE(50) массивов типа uint8_t[6] с MAC адресами нод.
        Объединения(union) - это объект, позволяющий нескольким переменным различных типов занимать один участок памяти. Объявление объединения 
        похоже на объявление структуры. Для придания имени объединению можно использовать также typedef.
    */
    int route_table_size = 0;
    mesh_data_t data;
    char str[81];            // В сумме строка должна быть размером (sizeof(str_topic + str_data) + 1)
    char mac_str[18];

    data.size = sizeof(str);
    data.proto = MESH_PROTO_BIN;
    data.tos = MESH_TOS_P2P;
    is_running = true;

    mqttPayload payload;    // Структура для получения инфы из mqtt ивента с топиком и инфой

    while (is_running)
    {
        xQueueReceive(mqtt_queue_rx, &payload, portMAX_DELAY);     // Ждём команды с нашего сайта на включения реле какой-либо ноды
        esp_mesh_get_routing_table((mesh_addr_t *) &route_table,
                                   CONFIG_MESH_ROUTE_TABLE_SIZE * 6, &route_table_size);    // Получили размер таблицы маршрутизации сети

        printf("Route table SIZE: %d \n", route_table_size);
        for (int i = 0; i < route_table_size; i++)
        {
            printf("Route table consist of: %d. MAC: "MACSTR" \n", i, MAC2STR(route_table[i].addr));
        }

        // Обработка payload. нужно оттуда завернуть все в строку, а затем привести к типу uint8_t* а на приёмной стороне снова к типу char*
        snprintf(str, sizeof(str), "%s/%s", payload.str_topic, payload.str_data);
        /*  Объединяем два поля в одну строку для передачи в формате topic/data     */
        printf("Our message to broadcast: %s\n", str);        // Проверим как выглядит наше сообщение

        data.data = (uint8_t *)str;     

        if( strcmp((char *)data.data, "user_5c1a8cfe/relayA/0") == 0)   // Если запрос пришёл на самого себя на выключение реле корневой ноды
        {
            relay_on_or_off = RELAY_OFF;
            xSemaphoreGive(relay_semaphore);
            continue;
        }
        else if ( strcmp((char *)data.data, "user_5c1a8cfe/relayA/1") == 0) // Если запрос пришёл на самого себя на включение реле корневой ноды
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
            
            err = esp_mesh_send(&route_table[i], &data, MESH_DATA_P2P, NULL, 0);    // Отправляем другим нодам
            if (err) {
                ESP_LOGE(MESH_TAG,
                         "[ROOT-2-UNICAST][L:%d]parent:"MACSTR" to "MACSTR", heap:%d[err:0x%x, proto:%d, tos:%d]",
                            mesh_layer, MAC2STR(mesh_parent_addr.addr),
                         MAC2STR(route_table[i].addr), esp_get_minimum_free_heap_size(),
                         err, data.proto, data.tos);
            } 
        }
    }
    if( esp_mesh_is_root() )vQueueDelete(mqtt_queue_rx);
    vTaskDelete(NULL);
}

void esp_mesh_p2p_tx_main(void *arg)
{
    esp_err_t err;
    mesh_addr_t route_table[CONFIG_MESH_ROUTE_TABLE_SIZE];
    /*
        Инициализируем UNION, который хранит CONFIG_MESH_ROUTE_TABLE_SIZE(50) массивов типа uint8_t[6] с MAC адресами нод.
        Объединения(union) - это объект, позволяющий нескольким переменным различных типов занимать один участок памяти. Объявление объединения 
        похоже на объявление структуры. Для придания имени объединению можно использовать также typedef.
    */
    int route_table_size = 0;
    mesh_data_t data;
    mqttPayload payload;

    char str_to_mqtt[81];    // Тестовый топик шлём
    char *str_topic = "user_5c1a8cfe/tempA";
    char *str_data = heap_caps_malloc(sizeof(str_data), MALLOC_CAP_8BIT);
    int temperature = 20;

    data.data = (uint8_t *)str_to_mqtt;       // Привели к типу
    data.size = sizeof(str_to_mqtt);
    data.proto = MESH_PROTO_BIN;
    data.tos = MESH_TOS_P2P;
    is_running = true;

    //xSemaphoreTake(allow_tx_semaphore, portMAX_DELAY);  

    while (is_running) {
        
        /* Здесь будет обработкат температуры и отправка её в формате topic/data вне условий ! */
        
        if(esp_mesh_is_root())
        {
            EventBits_t bits = xEventGroupWaitBits(mqtt_state_event_group,  // Ждём фалага коннекта mqtt 
                    MQTT_EVT_CONNECTED,
                    pdFALSE,
                    pdTRUE,
                    portMAX_DELAY);

            esp_mesh_get_routing_table((mesh_addr_t *) &route_table,
                                   CONFIG_MESH_ROUTE_TABLE_SIZE * 6, &route_table_size);

            snprintf(payload.str_topic, sizeof(payload.str_topic), str_topic);  // Завернули в пэйлоад топик температуры ноды А
            printf("payload.str_topic is: %s\n", payload.str_topic);
            
            itoa(temperature, str_data, 10);    // Преобразовали температуру в строку и засунули её в str_data
            
            snprintf(payload.str_data, sizeof(payload.str_data), str_data);  // Завернули в пэйлоад температуру ноды А
            printf("payload.str_data is: %s\n", payload.str_data);

            /*  почему он заходит сюда?? */
            xQueueSendToBack(mqtt_queue_tx, &payload, portMAX_DELAY);        // И отправили прямиком в mqtt т.к мы корневая нода
            vTaskDelay(2000/ portTICK_RATE_MS);                              // Раз в 2 сек отправляем
        }
        else 
        {
            esp_mesh_get_routing_table((mesh_addr_t *) &route_table,
                                   CONFIG_MESH_ROUTE_TABLE_SIZE * 6, &route_table_size);    // Чтобы понять нужна ли задержка

            itoa(temperature, str_data, 10);    // Преобразовали температуру в строку и засунули её в str_data
            snprintf(str_to_mqtt, sizeof(str_to_mqtt), "%s/%s", str_topic, str_data);
            printf("str_to_mqtt is: %s\n", str_to_mqtt);

            data.data = (uint8_t *)str_to_mqtt;       // Привели к типу

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
    if(esp_mesh_is_root()) vQueueDelete(mqtt_queue_tx);
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
    int i = 0, j = 0;              // Для парсинга итератор
    
    char str[81];           // DATA.SIZE приёмный должен быть БОЛЬШЕ ИЛИ РАВЕН DATA.SIZE отправителя
    char str_topic[40];
    char str_data[40];

    data.data = (uint8_t *)str;    // На приёмной стороне тоже обязательно инициализирвоать эти поля !!!!
    data.size = sizeof(str);       // Также и размер!!!
    is_running = true;
    
    mqttPayload payload;
    //char buff[80];
    //uint8_t *pointer = heap_caps_malloc(80, MALLOC_CAP_8BIT);
    char *buff;     //heap_caps_malloc(80, MALLOC_CAP_8BIT);
   // memset(buff, 0, 80);

    while (is_running) {

        if(esp_mesh_is_root())
        {
            EventBits_t bits = xEventGroupWaitBits(mqtt_state_event_group,  // Ждём фалага коннекта mqtt 
                    MQTT_EVT_CONNECTED,
                    pdFALSE,
                    pdTRUE,
                    portMAX_DELAY);

            esp_mesh_get_routing_table((mesh_addr_t *) &route_table,
                                   CONFIG_MESH_ROUTE_TABLE_SIZE * 6, &route_table_size);
            
            err = esp_mesh_recv(&from, &data, portMAX_DELAY, &flag, NULL, 0);   // Принимаем сразу указатель на строку формата topic/data
            if (err != ESP_OK || !data.size) {
                ESP_LOGE(MESH_TAG, "err:0x%x, size:%d", err, data.size);
                continue;
            }
            printf("recv msg from node: %s\n", (char *)data.data );
            
//----------------------------------Парсим полученную информацию, разибвая её на topic и data---------------------------------------------------
            buff = (char *)data.data;

            strcpy(str_topic, "user_5c1a8cfe/");    // Сразу доабвим заранее известный префикс
            i = strlen(str_topic);                  // Начинаем итерироваться минуя / так как strlen выдаст на 1 больше чем sizeof

            while (i < 80)        
            {
                if(buff[i+1] >= 48 && buff[i+1] <= 57)  // Пределы АСКИ цифр 0-9
                {
                    str_topic[i] = '\0';    // Так как слеш нам не нужно добавлять, а нужно добавить вместо него нуль-терминатор
                    i++;    // Чтобы дальше сразу начать писать число
                    break;  // Если в строке дальше идёт цифра, то послелний слэш не дописываем, т.к он не пишется и выходим из цикла
                }
                str_topic[i] = buff[i] ;
                i++;
            }
            printf("Parsed TOPIC is: %s\n", (char *)str_topic);  

            j = 0;      // Обнуляем итератор данных

            while (buff[i] != '\0')
            {
                str_data[j] = buff[i];
                i++;
                j++;
            }   
            str_data[j] = '\0';
            printf("Parsed DATA is: %s\n", (char *)str_data);
//-----------------------------------------------------------------------------------------------------------------------------------------------
            sniprintf(payload.str_topic, sizeof(payload.str_topic), str_topic);     // Завернули в пэйлоад наш топик
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
            //str = (char *)data.data;

            if( strcmp((char *)data.data, "user_5c1a8cfe/relayA/0") == 0)       // Работает!!
            {
                printf("Relay A OFF!!\n");
                relay_on_or_off = RELAY_OFF;
                xSemaphoreGive(relay_semaphore);
            }
            else if(strcmp((char *)data.data, "user_5c1a8cfe/relayA/1") == 0)
            {
                printf("Relay A OFF!!\n");
                relay_on_or_off = RELAY_ON;
                xSemaphoreGive(relay_semaphore);
            }
            else
            {
                printf("Str didn't match!!!\n");
            }
            /* 
                Код для ноды В и А и даже D будет отличаться в этой ветке, когда мы не ворневая нода, только тем, что нужно будет 
                убрать ветку с strcmp, т.к только ноде С важно какая температура на ноде В, остальным все равно и этой ветки просто не будет.
                Также не будет ветки с if(temperature > 50). 
            */
        }
        /*
        snprintf(mac_str, sizeof(mac_str), MACSTR,  // ЗАвернули МАК адрес от кого в нашу строку 
            from.addr[0],
            from.addr[1],
            from.addr[2],
            from.addr[3],
            from.addr[4],
            from.addr[5]);
            */
    }
    if(esp_mesh_is_root()) vQueueDelete(mqtt_queue_tx);
    vTaskDelete(NULL);
}

esp_err_t esp_mesh_comm_p2p_start(void)
{
    if (!is_comm_p2p_started) {
        is_comm_p2p_started = true;
        
        if(esp_mesh_is_root())      // Для корневой ноды создадим mqtt семафоры
        {
			mqtt_state_event_group = xEventGroupCreate();           // Создали ивент
			mqtt_queue_rx = xQueueCreate(10, sizeof(mqttPayload)); // Создали очередь на приём 
			mqtt_queue_tx = xQueueCreate(10, sizeof(mqttPayload)); // Создали очередь на передачу 
        } 
        
		vSemaphoreCreateBinary(relay_semaphore); 
        vTaskDelay(50/ portTICK_RATE_MS);
        xTaskCreate(esp_mesh_rx_from_nodes, "MPRX", 3072, NULL, 5, NULL);  // Задача на приём создается при любом раскладе
        xTaskCreate(esp_mesh_p2p_tx_main, "p2p_tx_task", 3072, NULL, 5, NULL);
        xTaskCreate(relay_task, "relay_task", 3072, NULL, 6, NULL);
        
        /*  Задачи создаются на данном этапе одни и те же. Но для корневой ноды будут создаваться ещё и mqtt таски на приём и передачу  */
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
    /* 
        TODO handler for the failure. Для root ноды может означать, что были введены неправильные параметры роутера.
    */
    break;
    case MESH_EVENT_PARENT_CONNECTED: {
        mesh_event_connected_t *connected = (mesh_event_connected_t *)event_data;
        esp_mesh_get_id(&id);
        mesh_layer = connected->self_layer;
        memcpy(&mesh_parent_addr.addr, connected->connected.bssid, 6);
        /*
            Здесь мы взяли из входных данных MAC адрес нашего родителя и скопировали его в нашу глобально объявленную структуру, чтобы
            использовать в дальнейшем при приёме, когда выводим в терминал инфу о родителе
        */
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
            /*
                Если мы в результате голосвания оказались корневой нодой, то требование API для mesh таково, что нода должна заиметь IP адрес.
                *ESP-WIFI-MESH requires a root node to be connected with a router. Therefore, in the event that a node becomes the root, the 
                corresponding handler must start the DHCP client service and immediately obtain an IP address.*
            */
        }
        gpio_set_level(led_gpio, 1);
        esp_mesh_comm_p2p_start();
    }
    break;
    case MESH_EVENT_PARENT_DISCONNECTED: {
        mesh_event_disconnected_t *disconnected = (mesh_event_disconnected_t *)event_data;
        ESP_LOGI(MESH_TAG,
                 "<MESH_EVENT_PARENT_DISCONNECTED>reason:%d",
                 disconnected->reason);
        is_mesh_connected = false;
        mesh_layer = esp_mesh_get_layer();
        /*
            Здесь могут быть какие-либо ответные действия, сеть является самовосстанавливающейся, поэтому без понятия пока что
            какая должна быть реакция
        */
       gpio_set_level(led_gpio, 0);  

       is_running = false;          // Чтобы задачи вышли из бесконечного цикла и удалили себя
       is_comm_p2p_started = false; // Чтобы, когда мы снова подключимся к родителю вызвали функцию создания задач и зашли по флагу 

       if( esp_mesh_is_root() ) 
       {
            vEventGroupDelete(mqtt_state_event_group);  // Удаляем, так как мы каждый коннект заново создаем очередь
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
        /* new root */
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
    case MESH_EVENT_PS_PARENT_DUTY: {
        mesh_event_ps_duty_t *ps_duty = (mesh_event_ps_duty_t *)event_data;
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_PS_PARENT_DUTY>duty:%d", ps_duty->duty);
    }
    break;
    case MESH_EVENT_PS_CHILD_DUTY: {
        mesh_event_ps_duty_t *ps_duty = (mesh_event_ps_duty_t *)event_data;
        ESP_LOGI(MESH_TAG, "<MESH_EVENT_PS_CHILD_DUTY>cidx:%d, "MACSTR", duty:%d", ps_duty->child_connected.aid-1,
                MAC2STR(ps_duty->child_connected.mac), ps_duty->duty);
    }
    break;
    default:
        ESP_LOGI(MESH_TAG, "unknown id:%d", event_id);
        break;
    }
}

void ip_event_handler(void *arg, esp_event_base_t event_base,
                      int32_t event_id, void *event_data)
{
    ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;
    ESP_LOGI(MESH_TAG, "<IP_EVENT_STA_GOT_IP>IP:" IPSTR, IP2STR(&event->ip_info.ip));
    mqtt_start();   // Там создаются все задачи с mqtt при соединении с бркоером 
}

void app_main(void)
{
    gpio_reset_pin(led_gpio);
    gpio_reset_pin(relay_gpio);
    gpio_set_direction(led_gpio, GPIO_MODE_OUTPUT);     // Индикация подключения к мэш сети
    gpio_set_direction(relay_gpio, GPIO_MODE_OUTPUT);   // Реле 
    gpio_set_level(led_gpio, 0);
    gpio_set_level(relay_gpio, 0);

    ESP_ERROR_CHECK(nvs_flash_init());
    /*  tcpip initialization */
    ESP_ERROR_CHECK(esp_netif_init());
    /*  event initialization */
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    /*  create network interfaces for mesh (only station instance saved for further manipulation, soft AP instance ignored */
    ESP_ERROR_CHECK(esp_netif_create_default_wifi_mesh_netifs(&netif_sta, NULL));
    /*  wifi initialization */
    wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&config));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &ip_event_handler, NULL));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_FLASH));
    ESP_ERROR_CHECK(esp_wifi_start());
    /*  mesh initialization */
    ESP_ERROR_CHECK(esp_mesh_init());
    ESP_ERROR_CHECK(esp_event_handler_register(MESH_EVENT, ESP_EVENT_ANY_ID, &mesh_event_handler, NULL));
    /*  set mesh topology */
    ESP_ERROR_CHECK(esp_mesh_set_topology(CONFIG_MESH_TOPOLOGY));
    /*  set mesh max layer according to the topology */
    ESP_ERROR_CHECK(esp_mesh_set_max_layer(CONFIG_MESH_MAX_LAYER));
    ESP_ERROR_CHECK(esp_mesh_set_vote_percentage(1));
    ESP_ERROR_CHECK(esp_mesh_set_xon_qsize(128));
#ifdef CONFIG_MESH_ENABLE_PS
    /* Enable mesh PS function */
    ESP_ERROR_CHECK(esp_mesh_enable_ps());
    /* better to increase the associate expired time, if a small duty cycle is set. */
    ESP_ERROR_CHECK(esp_mesh_set_ap_assoc_expire(60));
    /* better to increase the announce interval to avoid too much management traffic, if a small duty cycle is set. */
    ESP_ERROR_CHECK(esp_mesh_set_announce_interval(600, 3300));
#else
    /* Disable mesh PS function */
    ESP_ERROR_CHECK(esp_mesh_disable_ps());                 // выключили энергосбережение
    ESP_ERROR_CHECK(esp_mesh_set_ap_assoc_expire(10));      // здесь задали количество туров голосования [SCAN:0-10]
#endif
    mesh_cfg_t cfg = MESH_INIT_CONFIG_DEFAULT();
    /* mesh ID */
    memcpy((uint8_t *) &cfg.mesh_id, MESH_ID, 6);
    /* router */
    
    //cfg.channel = CONFIG_MESH_CHANNEL;  // Вот это дает фиксированный канал, что нам не нужно
    cfg.allow_channel_switch = true;

    cfg.router.ssid_len = strlen(CONFIG_MESH_ROUTER_SSID);
    memcpy((uint8_t *) &cfg.router.ssid, CONFIG_MESH_ROUTER_SSID, cfg.router.ssid_len);
    memcpy((uint8_t *) &cfg.router.password, CONFIG_MESH_ROUTER_PASSWD,
           strlen(CONFIG_MESH_ROUTER_PASSWD));
    /* mesh softAP */
    ESP_ERROR_CHECK(esp_mesh_set_ap_authmode(CONFIG_MESH_AP_AUTHMODE));
    cfg.mesh_ap.max_connection = CONFIG_MESH_AP_CONNECTIONS;
    cfg.mesh_ap.nonmesh_max_connection = CONFIG_MESH_NON_MESH_AP_CONNECTIONS;
    memcpy((uint8_t *) &cfg.mesh_ap.password, CONFIG_MESH_AP_PASSWD,
           strlen(CONFIG_MESH_AP_PASSWD));
    ESP_ERROR_CHECK(esp_mesh_set_config(&cfg));
    /* mesh start */
    ESP_ERROR_CHECK(esp_mesh_start());
#ifdef CONFIG_MESH_ENABLE_PS
    /* set the device active duty cycle. (default:10, MESH_PS_DEVICE_DUTY_REQUEST) */
    ESP_ERROR_CHECK(esp_mesh_set_active_duty_cycle(CONFIG_MESH_PS_DEV_DUTY, CONFIG_MESH_PS_DEV_DUTY_TYPE));
    /* set the network active duty cycle. (default:10, -1, MESH_PS_NETWORK_DUTY_APPLIED_ENTIRE) */
    ESP_ERROR_CHECK(esp_mesh_set_network_duty_cycle(CONFIG_MESH_PS_NWK_DUTY, CONFIG_MESH_PS_NWK_DUTY_DURATION, CONFIG_MESH_PS_NWK_DUTY_RULE));
#endif
    ESP_LOGI(MESH_TAG, "mesh starts successfully, heap:%d, %s<%d>%s, ps:%d\n",  esp_get_minimum_free_heap_size(),
             esp_mesh_is_root_fixed() ? "root fixed" : "root not fixed",
             esp_mesh_get_topology(), esp_mesh_get_topology() ? "(chain)":"(tree)", esp_mesh_is_ps_enabled());
}
