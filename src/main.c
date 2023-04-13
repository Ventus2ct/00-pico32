#include <stdio.h>
#include <driver/gpio.h>
#include <driver/twai.h>
#include <inttypes.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "nmea_parser.h"
#include <sys/time.h>

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

static const char *TAG = "gps_aSID";
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
    switch (event_id) {
    case GPS_UPDATE:
        gps = (gps_t *)event_data;
        /* print information parsed from GPS statements */
        ESP_LOGI(TAG, "%d/%d/%d %02d:%02d:%02d => "
                 "\tlatitude   = %.05f °N\r\n"
                 "\t\t\t\t\t\tlongitude  = %.05f °E\r\n"
                 "\t\t\t\t\t\taltitude   = %.02f m\r\n"
                 "\t\t\t\t\t\tspeed      = %f m/s",
                 gps->date.year + YEAR_BASE, gps->date.month, gps->date.day,
                 gps->tim.hour + TIME_ZONE, gps->tim.minute, gps->tim.second,
                 gps->latitude, gps->longitude, gps->altitude, gps->speed);
        
        break;
    case GPS_UNKNOWN:
        /* print unknown statements */
        ESP_LOGW(TAG, "Unknown statement:%s", (char *)event_data);
        break;
    default:
        break;
    }
    
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
    timeSinceEpoch += 3600 * 2;
    // printf("timestamp:%ld\n",timeSinceEpoch);

    now.tv_sec = timeSinceEpoch;
    settimeofday(&now, NULL);

    /* -------------------------- */
    time(&now1);
    localtime_r(&now1, &timeinfo);
    strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
    // printf("gettime: %d %d %d %d %d %d\n",timeinfo.tm_year + 1900,timeinfo.tm_mon,timeinfo.tm_mday,timeinfo.tm_hour,timeinfo.tm_min,timeinfo.tm_sec);
    
    struct tm timeinfo1;
    //char strftime_buf[64];
    localtime_r(&now1, &timeinfo1);
    strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo1);
    ESP_LOGI(TAG, "The current date/time in Oslo is: %s\n\r", strftime_buf);


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

void app_main() 
{
    printf("Initialize TWAI configuration!\n");
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
        printf("Driver installed\n");
    }
    else
    {
        printf("Failed to install driver\n");
        return;
    }

    // Start TWAI driver
    if (twai_start() == ESP_OK)
    {
        printf("Driver started\n");
    }
    else
    {
        printf("Failed to start driver\n");
        return;
    }
    // NMEA_PARSER_CONFIG_aSid()


    /* NMEA parser configuration */
    nmea_parser_config_t config = NMEA_PARSER_CONFIG_aSid();
    //nmea_parser_config_t config = NMEA_PARSER_CONFIG_DEFAULT();
    
    /* init NMEA parser library */
    nmea_parser_handle_t nmea_hdl = nmea_parser_init(&config);

    /* register event handler for NMEA parser library */
    nmea_parser_add_handler(nmea_hdl, gps_event_handler, NULL);
    

    vTaskDelay(10000 / portTICK_PERIOD_MS);

    // ESP_ERROR_CHECK(uart_param_config(UART_NUM_1, &uart_config));
    // ESP_ERROR_CHECK(uart_set_pin(UART_NUM_1, TXD_PIN, RXD_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    // ESP_ERROR_CHECK(uart_driver_install(UART_NUM_1, BUF_SIZE * 2, 256, 0, NULL, 0));

    // xTaskCreate(sendTask, "Send Task", 4096, NULL, 10, &sendHandle);
    xTaskCreate(recvTask, "Recv Task", 4096, NULL, 10, &recvHandle);
    //xTaskCreate(gpsTask, "GPS Task", 4096, NULL, 10, &gpsrecvHandle);
    // gpsrecvHandle gpsTask
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