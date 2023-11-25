#include <stdio.h>
#include <driver/gpio.h>
#include <driver/twai.h>
#include <inttypes.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "driver/uart.h"
#include "esp_types.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nmea_parser.h"
#include <sys/time.h>
#include "nvs_flash.h"
#include "string.h"
#include <esp_http_server.h>

// #include "time.h"



// #include "esp_chip_info.h"
#include "esp_flash.h"
// #include <HardwareSerial.h>

#define BUF_SIZE (1024)
#define TXD_PIN (GPIO_NUM_23)
#define RXD_PIN (GPIO_NUM_19)

TaskHandle_t sendHandle = NULL;
TaskHandle_t recvHandle = NULL;
TaskHandle_t gpsrecvHandle = NULL;

static const char *TAG = "aSID";
/* Wifi */
static EventGroupHandle_t wifi_event_group;
const int CONNECTED_BIT = BIT0;

char g_strftime_buf[64] = {0};
int g_last_gps_echo_count = 0;

long TimeZoneCorrectionInSecons = 2 * 3600;
#define TIME_ZONE (0)   //GMP + offset
#define YEAR_BASE (2000) //date in GPS starts from 2000

#define NMEA_PARSER_CONFIG_aSid()       \
    {                                      \
        .uart = {                          \
            .uart_port = UART_NUM_1,       \
            .rx_pin = GPIO_NUM_19,         \
            .baud_rate = 9600,             \
            .data_bits = UART_DATA_8_BITS, \
            .parity = UART_PARITY_DISABLE, \
            .stop_bits = UART_STOP_BITS_1, \
            .event_queue_size = 16         \
        }                                  \
    }
#define CONFIG_AP_MAX_STA_CONN 4

#define CONFIG_AP_WIFI_CHANNEL 10
#define CONFIG_AP_WIFI_PASSWORD "system01" // Password must be 8 or more characters!!
#define CONFIG_AP_WIFI_SSID "aSID"

#define CONFIG_STA_WIFI_SSID  "bamsebo"
#define CONFIG_STA_WIFI_PASSWORD "system-B2"

// #define CONFIG_STA_WIFI_SSID  "EFK-wifi"
// #define CONFIG_STA_WIFI_PASSWORD "Runway0523"

static void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
            ESP_LOGI(TAG, "WIFI_EVENT_STA_DISCONNECTED");
            esp_wifi_connect();
            xEventGroupClearBits(wifi_event_group, CONNECTED_BIT);
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
            ESP_LOGI(TAG, "IP_EVENT_STA_GOT_IP");
            xEventGroupSetBits(wifi_event_group, CONNECTED_BIT);
    } else if(event_base == IP_EVENT && event_id == IP_EVENT_STA_LOST_IP) {
            ESP_LOGI(TAG, "IP_EVENT_STA_LOST_IP");
            esp_wifi_disconnect();
            esp_wifi_connect();
            xEventGroupSetBits(wifi_event_group, CONNECTED_BIT);
    }
    
}

void gpsTask()
{
    uint8_t *data = (uint8_t *)malloc(BUF_SIZE);
    while(1)
    {
        int len = uart_read_bytes(UART_NUM_1, data, BUF_SIZE, pdMS_TO_TICKS(10));
        if (len > 0)
        {
            // printf("UART read %d bytes: '%.*s'\n", len, len, data);
            printf("Read %d bytes: ", len);
            printf("'");
            for(int i = 0; i < len;i++)
            {
                // printf("%s", (char *)data[i]);
                if(data[i] > 31 && data[i] < 127 )
                {
                    printf("%c", data[i]);
                }
            }
            printf("'\n");
        }
        vTaskDelay(1);
    }
    free(data);
}
/**
 * @brief GPS Event Handler
 *
 * @param event_handler_arg handler specific arguments
 * @param event_base event base, here is fixed to ESP_NMEA_EVENT
 * @param event_id event id
 * @param event_data event specific arguments
 */
static void gps_event_handler(void *event_handler_arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    gps_t *gps = NULL;
    ESP_LOGD(TAG,"GPS Event ");
    
    switch (event_id) {
    case GPS_UPDATE:
        gps = (gps_t *)event_data;
        if(gps->valid)
        {
            //char * Stat[5] = (char) gps->valid ? "true" : "false";
            /* print information parsed from GPS statements */
            /*
            ESP_LOGI(TAG, "%d/%d/%d %02d:%02d:%02d => "
                    "\tlatitude   = %.05f °N\r\n"
                    "\t\t\t\t\t\tlongitude  = %.05f °E\r\n"
                    "\t\t\t\t\t\taltitude   = %.02f m\r\n"
                    "\t\t\t\t\t\tspeed      = %f m/s \r\n"
                    "\t\t\t\t\t\tSatellites = %d \r\n"
                    "\t\t\t\t\t\t%s",
                    gps->date.year + YEAR_BASE, gps->date.month, gps->date.day,
                    gps->tim.hour + TIME_ZONE, gps->tim.minute, gps->tim.second,
                    gps->latitude, gps->longitude, gps->altitude, gps->speed, 
                    gps->sats_in_use, gps->valid ? "Valid fix" : "No fix");
            */
            /* print information parsed from GPS statements */
            // ESP_LOGI(TAG,"Satellites: %d, Valid fix: %s ", gps->sats_in_use, gps->valid ? "true" : "false");
            /* set time */
            struct tm t = {0}; 
            struct timeval now = {0};

            /* get time */
            time_t now1 = {0};
            char strftime_buf[64] = {0};
            struct tm timeinfo= {0};

            /* ------------------------- */
            t.tm_year = gps->date.year + YEAR_BASE - 1900;
            t.tm_mon = gps->date.month - 1;
            t.tm_mday = gps->date.day;
            t.tm_hour = gps->tim.hour;
            t.tm_min = gps->tim.minute;
            t.tm_sec = gps->tim.second;

            time_t timeSinceEpoch = mktime(&t);
            timeSinceEpoch += TimeZoneCorrectionInSecons;
            // printf("timestamp:%ld\n",timeSinceEpoch);

            now.tv_sec = timeSinceEpoch;
            settimeofday(&now, NULL);

            /* -------------------------- */
            time(&now1);
            localtime_r(&now1, &timeinfo);
            strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
            strftime(g_strftime_buf, sizeof(g_strftime_buf), "%c", &timeinfo);
            // printf("gettime: %d %d %d %d %d %d\n",timeinfo.tm_year + 1900,timeinfo.tm_mon,timeinfo.tm_mday,timeinfo.tm_hour,timeinfo.tm_min,timeinfo.tm_sec);
            
            struct tm timeinfo1;
            //char strftime_buf[64];
            localtime_r(&now1, &timeinfo1);
            strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo1);
            g_last_gps_echo_count +=1;
            if(g_last_gps_echo_count > 9)
            {
                ESP_LOGI(TAG, "Oslo time is: %s\r", strftime_buf);
                g_last_gps_echo_count = 0;
            }
            
        }
        else
        {
           printf("Valid GPS: %s Sats: %d \r\n", "No fix", gps->sats_in_use);
        }
        break;
    case GPS_UNKNOWN:
        /* print unknown statements */
        ESP_LOGW(TAG, "Unknown statement:%s", (char *)event_data);
        
        break;
    default:
        break;
    }
}

void sendTask(void *arg)
{
    uint8_t cnt = 0;
    while (1)
    {
        // Configure message to transmit
        twai_message_t message;
        message.identifier = 0x24F;
        message.extd = 0;
        message.data_length_code = 4;

        message.data[0] = 0x02;
        message.data[1] = 0x1a;
        message.data[2] = 0x4d;
        message.data[3] = cnt++;

        // Queue message for transmission
        if (twai_transmit(&message, pdMS_TO_TICKS(1000)) == ESP_OK)
        {
            printf("Message queued for transmission\n");
        }
        else
        {
            printf("Failed to queue message for transmission\n");
        }
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}
void reconnectTask(void *arg)
{
    // WIFI_EVENT_STA_DISCONNECTED
    // IP_EVENT_STA_GOT_IP
//    unsigned long currentMillis = millis();
//   // if WiFi is down, try reconnecting every CHECK_WIFI_TIME seconds
//   if ((WiFi.status() != WL_CONNECTED) && (currentMillis - previousMillis >=interval)) {
//     Serial.print(millis());
//     Serial.println("Reconnecting to WiFi...");
//     WiFi.disconnect();
//     WiFi.reconnect();
//     previousMillis = currentMillis;
//   }
}
void recvTask(void *arg)
{
    twai_message_t message;
    while (1)
    {
        // Queue message for transmission
        esp_err_t ret = twai_receive(&message, pdMS_TO_TICKS(1000));
        if (ret == ESP_ERR_TIMEOUT)
        {
            printf("Timeout recive..");
            continue;
        }

        if (ret != ESP_OK)
        {
            printf("failed to receive message: %s\n", esp_err_to_name(ret));
            continue;
        }

        // printf("ID: %x : ",  message.identifier);
        for (size_t i = 0; i < message.data_length_code; i++)
        {
           // printf("%#1x ", message.data[i]);
        }
        //printf(" Len: %x\n", message.data_length_code);
    }
}

static void initialise_wifi(void)
{
        esp_log_level_set("wifi", ESP_LOG_WARN);
        // esp_log_level_set("wifi", ESP_LOG_DEBUG);
        static bool initialized = false;
        if (initialized) {
            return;
        }
        ESP_ERROR_CHECK(esp_netif_init());
        wifi_event_group = xEventGroupCreate();
        ESP_ERROR_CHECK(esp_event_loop_create_default());
        esp_netif_t *ap_netif = esp_netif_create_default_wifi_ap();

        
        assert(ap_netif);
        esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();

        // Set the hostname for the network interface
        esp_netif_set_hostname(sta_netif, "aSid");

        assert(sta_netif);
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        ESP_ERROR_CHECK( esp_wifi_init(&cfg) );
        ESP_ERROR_CHECK( esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &event_handler, NULL) );
        ESP_ERROR_CHECK( esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL) );
        ESP_ERROR_CHECK( esp_event_handler_register(IP_EVENT, IP_EVENT_STA_LOST_IP, &event_handler, NULL) );

        ESP_ERROR_CHECK( esp_wifi_set_storage(WIFI_STORAGE_RAM) );
        ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_NULL) );
        ESP_ERROR_CHECK( esp_wifi_start() );

        initialized = true;
}

// static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
// {
//     if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
//         esp_wifi_connect();
//         ESP_LOGI(TAG,"Reconnecting");
//     }
// }

static bool wifi_apsta(int timeout_ms)
{
    // EventGroupHandle_t s_wifi_event_group = xEventGroupCreate();
	wifi_config_t ap_config = { 0 };
	strcpy((char *)ap_config.ap.ssid,CONFIG_AP_WIFI_SSID);
	strcpy((char *)ap_config.ap.password, CONFIG_AP_WIFI_PASSWORD);
    
	ap_config.ap.authmode = WIFI_AUTH_WPA_WPA2_PSK;
	ap_config.ap.ssid_len = strlen(CONFIG_AP_WIFI_SSID);
	ap_config.ap.max_connection = CONFIG_AP_MAX_STA_CONN;
	ap_config.ap.channel = CONFIG_AP_WIFI_CHANNEL;

	if (strlen(CONFIG_AP_WIFI_PASSWORD) == 0) {
		ap_config.ap.authmode = WIFI_AUTH_OPEN;
	}

	wifi_config_t sta_config = { 0 };
	strcpy((char *)sta_config.sta.ssid, CONFIG_STA_WIFI_SSID);
	strcpy((char *)sta_config.sta.password, CONFIG_STA_WIFI_PASSWORD);

	// ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_APSTA) );
	/*
    ESP_ERROR_CHECK( esp_wifi_set_config(ESP_IF_WIFI_AP, &ap_config) );
	ESP_ERROR_CHECK( esp_wifi_set_config(ESP_IF_WIFI_STA, &sta_config) );
    
    */
    esp_wifi_set_mode(WIFI_MODE_APSTA);
    esp_wifi_set_config(ESP_IF_WIFI_AP, &ap_config);
    esp_wifi_set_config(ESP_IF_WIFI_STA, &sta_config);

    ESP_ERROR_CHECK( esp_wifi_start() );
    
	ESP_LOGI(TAG, "WIFI_MODE_AP started. SSID:%s password:%s channel:%d",
			 CONFIG_AP_WIFI_SSID, CONFIG_AP_WIFI_PASSWORD, CONFIG_AP_WIFI_CHANNEL);
    
    ESP_LOGI(TAG, "Starting softAP");
	// ESP_ERROR_CHECK( esp_wifi_connect() );
    esp_wifi_connect(); 
	int bits = xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT, pdFALSE, pdTRUE, timeout_ms / portTICK_PERIOD_MS);
	ESP_LOGI(TAG, "bits=%x", bits);
	if (bits) {
		ESP_LOGI(TAG, "WIFI_MODE_STA connected. SSID:%s password:%s",
			 CONFIG_STA_WIFI_SSID, CONFIG_STA_WIFI_PASSWORD);
	} else {
		ESP_LOGI(TAG, "WIFI_MODE_STA can't connected. SSID:%s password:%s",
			 CONFIG_STA_WIFI_SSID, CONFIG_STA_WIFI_PASSWORD);
	}
	return (bits & CONNECTED_BIT) != 0;
}

esp_err_t test_handler(httpd_req_t *req)
{
    // char sPage[]="300";
    // char sHead[120]="<!doctype html>\r\n";
    // //char sBody[120]="<html><head><title>aSid</title><meta http-equiv=""refresh"" content=""10""></head><body><h2> ";
    // char sBody[120]="<html><head><title>aSid</title></head><body><h2> ";
    // char sFoot[120]="</h2></body></html>";
    // sprintf(sPage,"%s %s %s %s", sHead,sBody, g_strftime_buf, sFoot);
    // httpd_resp_send(req, sPage, HTTPD_RESP_USE_STRLEN);

    char on_resp[] = "<!DOCTYPE html><html><head><style type=\"text/css\">html "
                     "{ font-family: Arial;  display: inline-block;  margin: 0px auto;"
                     "  text-align: center;}h1{  color: #070812;  padding: 2vh;}.button {  display: inline-block;"
                     "  background-color: #b30000; //red color  border: none;  border-radius: 4px;  color: white;  " 
                     " padding: 16px 40px;  text-decoration: none;  font-size: 30px;  margin: 2px;  cursor: pointer;}.button2 "
                     " {  background-color: #364cf4; //blue color}.content {   padding: 50px;}.card-grid {  max-width: 800px;  margin: 0 auto;  "
                     " display: grid;  grid-gap: 2rem;  grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));}"
                     ".card {  background-color: white;  box-shadow: 2px 2px 12px 1px rgba(140,140,140,.5);}"
                     ".card-title {  font-size: 1.2rem;  font-weight: bold;  color: #034078}</style>"
                     "  <title>ESP32 WEB SERVER</title>  "
                     "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">  "
                     "<link rel=\"icon\" href=\"data:,\">  <link rel=\"stylesheet\" "
                     "href=\"https://use.fontawesome.com/releases/v5.7.2/css/all.css\"    "
                     "integrity=\"sha384-fnmOCqbTlWIlj8LyTjo7mOUStjsKC4pOpQbqyi7RrhN7udi9RwhKkMHpvLbHG9Sr\" "
                     "crossorigin=\"anonymous\">  <link rel=\"stylesheet\" type=\"text/css\" ></head>"
                     "<body>  <h2>ESP32 WEB SERVER</h2>  <div class=\"content\">    <div class=\"card-grid\">      "
                     "<div class=\"card\">        <p><i class=\"fas fa-lightbulb fa-2x\" style=\"color:#c81919;\"></i>    "
                     " <strong>GPIO2</strong></p>        <p>GPIO state: <strong> ON</strong></p>        <p>          "
                     "<a href=\"/led2on\"><button class=\"button\">ON</button></a>          "
                     "<a href=\"/led2off\"><button class=\"button button2\">OFF</button></a>        "
                     "</p>      </div>    </div>  </div></body></html>";

    httpd_resp_send(req, g_strftime_buf, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

void WebServerSetup()
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t server = NULL;
    if (httpd_start(&server, &config) == ESP_OK) {
        // Do something
    }
    // esp_err_t httpd_start(httpd_handle_t *handle, const httpd_config_t *config);
    
  
    httpd_uri_t test_uri = {
        .uri      = "/test",
        .method   = HTTP_GET,
        .handler  = test_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &test_uri);

    // esp_err_t httpd_resp_send(httpd_req_t *r, const char *buf, ssize_t buf_len);

}


void app_main() 
{

    
    ESP_LOGW(TAG,"Initialize TWAI configuration!\n");

    vTaskDelay(10000 / portTICK_PERIOD_MS);

    // Initialize configuration structures using macro initializers
    twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(GPIO_NUM_21, GPIO_NUM_22, TWAI_MODE_NORMAL);
    // for 33.3 kHz
    // twai_timing_config_t t_config = {.brp = 104, .tseg_1 = 14, .tseg_2 = 8, .sjw = 4, .triple_sampling = false};
    // https://www.kvaser.com/support/calculators/bit-timing-calculator/
    /*
    80mhz iirc 
    the CAN controller is clocked with APB clock

typedef struct {
    uint32_t brp;                   *< Baudrate prescaler (i.e., APB clock divider). Any even number from 2 to 128 for ESP32, 2 to 32768 for ESP32S2.
                                       For ESP32 Rev 2 or later, multiples of 4 from 132 to 256 are also supported 
    uint8_t tseg_1;                 *< Timing segment 1 (Number of time quanta, between 1 to 16) 
    uint8_t tseg_2;                 *< Timing segment 2 (Number of time quanta, 1 to 8) 
    uint8_t sjw;                    *< Synchronization Jump Width (Max time quanta jump for synchronize from 1 to 4) 
    bool triple_sampling;           *< Enables triple sampling when the TWAI controller samples a bit 
} twai_timing_config_t;

    */
    // 47.619 kHz 16 MHz
    twai_timing_config_t t_config = {.brp = 80, .tseg_1 = 12, .tseg_2 = 8, .sjw = 4, .triple_sampling = false};

    // twai_timing_config_t t_config = TWAI_TIMING_CONFIG_500KBITS();
    twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    // Install TWAI driver
    if (twai_driver_install(&g_config, &t_config, &f_config) == ESP_OK)
    {
        printf("TWAI Driver installed\n");
    }
    else
    {
        printf("Failed to install driver\n");
        return;
    }

    // Start TWAI driver
    if (twai_start() == ESP_OK)
    {
        printf("TWAI Driver started\n");
    }
    else
    {
        printf("Failed to start driver\n");
        return;
    }
    
    /* NMEA parser configuration */
    nmea_parser_config_t config = NMEA_PARSER_CONFIG_aSid();
    //nmea_parser_config_t config = NMEA_PARSER_CONFIG_DEFAULT();
    
    /* init NMEA parser library */
    nmea_parser_handle_t nmea_hdl = nmea_parser_init(&config);
    printf("NMEA parser initsalized\n");
    
    /* register event handler for NMEA parser library */
    esp_err_t nmea_err = nmea_parser_add_handler(nmea_hdl, gps_event_handler, NULL);
    if(nmea_err == ESP_OK)
    {
        ESP_LOGI(TAG, "NMEA parser handler setup");
    }
    else
    {
        ESP_LOGI(TAG, "MEA parser handler setup FAILED!\n");
    }
    

    vTaskDelay(10000 / portTICK_PERIOD_MS);

    // ESP_ERROR_CHECK(uart_param_config(UART_NUM_1, &uart_config));
    // ESP_ERROR_CHECK(uart_set_pin(UART_NUM_1, TXD_PIN, RXD_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    // ESP_ERROR_CHECK(uart_driver_install(UART_NUM_1, BUF_SIZE * 2, 256, 0, NULL, 0));

    // xTaskCreate(sendTask, "Send Task", 4096, NULL, 10, &sendHandle);
    // Only GPS  xTaskCreate(recvTask, "Recv Task", 4096, NULL, 10, &recvHandle);
    // ESP_LOGI(TAG, "Starting GPS Task for uart read..");
    // xTaskCreate(gpsTask, "GPS Task", 4096, NULL, 10, &gpsrecvHandle);
    // gpsrecvHandle gpsTask


    // Wifi
    // Reset:
    esp_err_t wifi_prov_mgr_reset_sm_state_on_failure(void);

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
            ESP_ERROR_CHECK( nvs_flash_erase() );
            err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    initialise_wifi();

    ESP_LOGW(TAG, "Start APSTA Mode");
    wifi_apsta(5000);
    wifi_config_t wifi_cfg;
    esp_wifi_get_config(ESP_IF_WIFI_AP, &wifi_cfg);
     if (strlen((const char*) wifi_cfg.ap.ssid)) {
        ESP_LOGI(TAG, "Found ssid %s",     (const char*) wifi_cfg.ap.ssid);
        ESP_LOGI(TAG, "Found password %s", (const char*) wifi_cfg.ap.password);
    }
    
    WebServerSetup();

}

void first_main() 
{
    printf("Hello world!\n");
     /* Print chip information */
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    printf("This is %s chip with %d CPU core(s), WiFi%s%s, ",
           CONFIG_IDF_TARGET,
           chip_info.cores,
           (chip_info.features & CHIP_FEATURE_BT) ? "/BT" : "",
           (chip_info.features & CHIP_FEATURE_BLE) ? "/BLE" : "");

    printf("silicon revision %d, ", chip_info.revision);

    // printf("%uMB %s flash\n", spi_flash_get_chip_size() / (1024 * 1024),
    //        (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "embedded" : "external");


    printf("Minimum free heap size: %d bytes\n", esp_get_minimum_free_heap_size());

    for (int i = 10; i >= 0; i--) {
        printf("Restarting in %d seconds...\n", i);
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
    printf("Restarting now.\n");
    fflush(stdout);
    esp_restart();
}