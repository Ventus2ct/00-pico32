#include <stdio.h>
#include <driver/gpio.h>
#include <driver/twai.h>
#include <inttypes.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
// #include "esp_chip_info.h"
#include "esp_flash.h"

TaskHandle_t sendHandle = NULL;
TaskHandle_t recvHandle = NULL;

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

        printf("received: %x : ",  message.identifier);
        for (size_t i = 0; i < message.data_length_code; i++)
        {
            printf("%x", message.data[i]);
        }
        printf("\n");
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
    80mhz iirc the CAN controller is clocked with APB clock

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

    // xTaskCreate(sendTask, "Send Task", 4096, NULL, 10, &sendHandle);
    xTaskCreate(recvTask, "Recv Task", 4096, NULL, 10, &recvHandle);
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