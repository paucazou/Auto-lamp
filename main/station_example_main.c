/* WiFi station Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <time.h>
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "nvs_flash.h"
//#include "protocol_examples_common.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>



/* The examples use WiFi configuration that you can set via project configuration menu

   If you'd rather not, just change the below entries to strings with
   the config you want - ie #define EXAMPLE_WIFI_SSID "mywifissid"
*/
#define EXAMPLE_ESP_WIFI_SSID      CONFIG_ESP_WIFI_SSID
#define EXAMPLE_ESP_WIFI_PASS      CONFIG_ESP_WIFI_PASSWORD
#define EXAMPLE_ESP_MAXIMUM_RETRY  CONFIG_ESP_MAXIMUM_RETRY

#if CONFIG_ESP_WIFI_AUTH_OPEN
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_OPEN
#elif CONFIG_ESP_WIFI_AUTH_WEP
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WEP
#elif CONFIG_ESP_WIFI_AUTH_WPA_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA2_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA_WPA2_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA_WPA2_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA3_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA3_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA2_WPA3_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_WPA3_PSK
#elif CONFIG_ESP_WIFI_AUTH_WAPI_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WAPI_PSK
#endif

struct Period {
  int start_h;
  int start_m;
  int end_h;
  int end_m;
};

bool create_period(struct Period* period, char * array) {
  if (period == NULL || array == NULL) {
    return false;
  }

  period->start_h = array[0];
  period->start_m = array[1];
  period->end_h = array[2];
  period->end_m = array[3];

  if (period->start_h < 0 || period->start_h > 23 || period->end_h < 0 || period->end_h > 23 ||
      period->start_m < 0 || period->start_m > 59 || period->end_m < 0 || period->end_m > 59) {
    return false;
  }

  return true;
}



QueueHandle_t period_queue = NULL;
QueueHandle_t bool_queue = NULL;


#define LED_PIN 2
#define TRANSISTOR_PIN 21
#define TEST_PIN 19

static void led_light(bool val) {
  gpio_set_level(LED_PIN, val);
}
/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;

/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static const char *TAG = "Automatic lamp";

void print_period(const struct Period *period) {
  if (period == NULL) {
    ESP_LOGE(TAG, "Error: NULL period pointer");
    return;
  }

  if (period->start_h < 0 || period->start_h > 23 || period->start_m < 0 || period->start_m > 59 ||
      period->end_h < 0 || period->end_h > 23 || period->end_m < 0 || period->end_m > 59) {
    ESP_LOGE(TAG, "Error: invalid start or end time");
    return;
  }

  ESP_LOGI(TAG, "Period -> %dh%02d - %dh%02d", period->start_h, period->start_m, period->end_h, period->end_m);
}


/* UDP server stuff
 */

#define PORT CONFIG_EXAMPLE_PORT


static void udp_server_task(void *pvParameters)
{
    char rx_buffer[10];
    char addr_str[128];
    int addr_family = (int)pvParameters;
    int ip_protocol = 0;
    struct sockaddr_in6 dest_addr;

    while (1) {

        if (addr_family == AF_INET) {
            struct sockaddr_in *dest_addr_ip4 = (struct sockaddr_in *)&dest_addr;
            dest_addr_ip4->sin_addr.s_addr = htonl(INADDR_ANY);
            dest_addr_ip4->sin_family = AF_INET;
            dest_addr_ip4->sin_port = htons(PORT);
            ip_protocol = IPPROTO_IP;
        } else if (addr_family == AF_INET6) {
            bzero(&dest_addr.sin6_addr.un, sizeof(dest_addr.sin6_addr.un));
            dest_addr.sin6_family = AF_INET6;
            dest_addr.sin6_port = htons(PORT);
            ip_protocol = IPPROTO_IPV6;
        }

        int sock = socket(addr_family, SOCK_DGRAM, ip_protocol);
        if (sock < 0) {
            ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
            break;
        }
        ESP_LOGI(TAG, "Socket created");

#if defined(CONFIG_LWIP_NETBUF_RECVINFO) && !defined(CONFIG_EXAMPLE_IPV6)
        int enable = 1;
        lwip_setsockopt(sock, IPPROTO_IP, IP_PKTINFO, &enable, sizeof(enable));
#endif

#if defined(CONFIG_EXAMPLE_IPV4) && defined(CONFIG_EXAMPLE_IPV6)
        if (addr_family == AF_INET6) {
            // Note that by default IPV6 binds to both protocols, it is must be disabled
            // if both protocols used at the same time (used in CI)
            int opt = 1;
            setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
            setsockopt(sock, IPPROTO_IPV6, IPV6_V6ONLY, &opt, sizeof(opt));
        }
#endif
        // Set timeout
        struct timeval timeout;
        timeout.tv_sec = 10;
        timeout.tv_usec = 0;
        setsockopt (sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout);

        int err = bind(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
        if (err < 0) {
            ESP_LOGE(TAG, "Socket unable to bind: errno %d", errno);
        }
        ESP_LOGI(TAG, "Socket bound, port %d", PORT);

        struct sockaddr_storage source_addr; // Large enough for both IPv4 or IPv6
        socklen_t socklen = sizeof(source_addr);

#if defined(CONFIG_LWIP_NETBUF_RECVINFO) && !defined(CONFIG_EXAMPLE_IPV6)
        struct iovec iov;
        struct msghdr msg;
        struct cmsghdr *cmsgtmp;
        u8_t cmsg_buf[CMSG_SPACE(sizeof(struct in_pktinfo))];

        iov.iov_base = rx_buffer;
        iov.iov_len = sizeof(rx_buffer);
        msg.msg_control = cmsg_buf;
        msg.msg_controllen = sizeof(cmsg_buf);
        msg.msg_flags = 0;
        msg.msg_iov = &iov;
        msg.msg_iovlen = 1;
        msg.msg_name = (struct sockaddr *)&source_addr;
        msg.msg_namelen = socklen;
#endif

        while (1) {
            ESP_LOGI(TAG, "Waiting for data");
#if defined(CONFIG_LWIP_NETBUF_RECVINFO) && !defined(CONFIG_EXAMPLE_IPV6)
            int len = recvmsg(sock, &msg, 0);
#else
            int len = recvfrom(sock, rx_buffer, sizeof(rx_buffer) - 1, 0, (struct sockaddr *)&source_addr, &socklen);
#endif
            // Error occurred during receiving
            if (len < 0) {
                ESP_LOGE(TAG, "recvfrom failed: errno %d", errno);
                break;
            }
            // Data received
            else {
                // Get the sender's ip address as string
                if (source_addr.ss_family == PF_INET) {
                    inet_ntoa_r(((struct sockaddr_in *)&source_addr)->sin_addr, addr_str, sizeof(addr_str) - 1);
#if defined(CONFIG_LWIP_NETBUF_RECVINFO) && !defined(CONFIG_EXAMPLE_IPV6)
                    for ( cmsgtmp = CMSG_FIRSTHDR(&msg); cmsgtmp != NULL; cmsgtmp = CMSG_NXTHDR(&msg, cmsgtmp) ) {
                        if ( cmsgtmp->cmsg_level == IPPROTO_IP && cmsgtmp->cmsg_type == IP_PKTINFO ) {
                            struct in_pktinfo *pktinfo;
                            pktinfo = (struct in_pktinfo*)CMSG_DATA(cmsgtmp);
                            ESP_LOGI(TAG, "dest ip: %s\n", inet_ntoa(pktinfo->ipi_addr));
                        }
                    }
#endif
                } else if (source_addr.ss_family == PF_INET6) {
                    inet6_ntoa_r(((struct sockaddr_in6 *)&source_addr)->sin6_addr, addr_str, sizeof(addr_str) - 1);
                }
                int err;
                if (len != 5) {
                    ESP_LOGE(TAG, "Incomplete message. %d bytes received",len);
                    err = sendto(sock, "invalid", 7, 0, (struct sockaddr *)&source_addr, sizeof(source_addr));
                    break;
                }

                ESP_LOGI(TAG, "Received %d bytes from %s:", len, addr_str);
                ESP_LOGI(TAG, "Msg decoded: %d %d:%d %d:%d",
                        rx_buffer[0],
                        rx_buffer[1],
                        rx_buffer[2],
                        rx_buffer[3],
                        rx_buffer[4]);
                struct Period p;
                if (create_period(&p, &rx_buffer[1])) {
                    xQueueSend(bool_queue, &rx_buffer[0], 0);
                    xQueueSend(period_queue, &p, 0);
                    err = sendto(sock, rx_buffer, len, 0, (struct sockaddr *)&source_addr, sizeof(source_addr));
                }
                else {
                    err = sendto(sock, "invalid", 7, 0, (struct sockaddr *)&source_addr, sizeof(source_addr));
                }

                if (err < 0) {
                    ESP_LOGE(TAG, "Error occurred during sending: errno %d", errno);
                    break;
                }
            }
        }

        if (sock != -1) {
            ESP_LOGE(TAG, "Shutting down socket and restarting...");
            shutdown(sock, 0);
            close(sock);
        }
    }
    vTaskDelete(NULL);
}

//static int s_retry_num = 0;


static void event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        led_light(false);
        esp_wifi_connect();
        ESP_LOGI(TAG, "retry to connect to the AP");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        led_light(true);
    }
}

void wifi_init_sta(void)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = EXAMPLE_ESP_WIFI_SSID,
            .password = EXAMPLE_ESP_WIFI_PASS,
            /* Authmode threshold resets to WPA2 as default if password matches WPA2 standards (pasword len => 8).
             * If you want to connect the device to deprecated WEP/WPA networks, Please set the threshold value
             * to WIFI_AUTH_WEP/WIFI_AUTH_WPA_PSK and set the password with length and format matching to
	     * WIFI_AUTH_WEP/WIFI_AUTH_WPA_PSK standards.
             */
            .threshold.authmode = ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD,
            .sae_pwe_h2e = WPA3_SAE_PWE_BOTH,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
    ESP_ERROR_CHECK(esp_wifi_start() );

    ESP_LOGI(TAG, "wifi_init_sta finished.");

    /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
     * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);

    /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
     * happened. */
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "connected to ap SSID:%s password:%s",
                 EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s",
                 EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
    } else {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
    }
}

void time_test() {
       time_t rawtime;
   struct tm *info;
   char buffer[80];

   time( &rawtime );

   info = localtime( &rawtime );

   strftime(buffer,80,"%x - %I:%M%p", info);
   printf("Formatted date & time : |%s|\n", buffer );
}



static void transistor_switch(bool val) {
    // I don't know why to reverve val, but well...
    // maybe a problem with the relay
    gpio_set_level(TRANSISTOR_PIN,!val);
    ESP_LOGI(TAG,"Open? %d",gpio_get_level(TEST_PIN));
}

static bool is_light_on() {
    // true if light is on
    return !gpio_get_level(TEST_PIN);
}

static void pin_init() {
// led
  esp_rom_gpio_pad_select_gpio(LED_PIN);
  gpio_set_direction(LED_PIN, GPIO_MODE_OUTPUT);

// transistor
  esp_rom_gpio_pad_select_gpio(TRANSISTOR_PIN);
  gpio_set_direction(TRANSISTOR_PIN, GPIO_MODE_OUTPUT);
  transistor_switch(false);
  // test3d
  esp_rom_gpio_pad_select_gpio(TEST_PIN);
  gpio_set_direction(TEST_PIN,GPIO_MODE_INPUT);
}

struct tm* fill_time() {
    time_t rawtime;
    time(&rawtime);
    return localtime( &rawtime);
}

bool is_time_in(struct tm* current_time, struct Period* period) {
    // Convert the start and end times to minutes since midnight
    int start_time_minutes = period->start_h* 60 + period->start_m;
    int end_time_minutes = period->end_h * 60 + period->end_m;

    // Convert the current time to minutes since midnight
    int current_time_minutes = current_time->tm_hour * 60 + current_time->tm_min;

    // Check if the current time is within the given period
    if (start_time_minutes <= end_time_minutes) {
        // Start time is before or equal to end time
        return start_time_minutes <= current_time_minutes && current_time_minutes < end_time_minutes;
    } else {
        // Start time is after end time
        return start_time_minutes <= current_time_minutes || current_time_minutes < end_time_minutes;
    }
}


#if 0

bool is_time_in(struct tm* current_time, struct Period* period) {
    // thanks to chat gpt
    // Convert the start and end times to minutes since midnight
    int start_time_minutes = period->start_h* 60 + period->start_m;
    int end_time_minutes = period->end_h * 60 + period->end_m;

    // Convert the current time to minutes since midnight
    int current_time_minutes = current_time->tm_hour * 60 + current_time->tm_min;

    // Check if the current time is within the given period
    return start_time_minutes <= current_time_minutes && current_time_minutes < end_time_minutes;
}
#endif

#if 0
void light_manager() {
    bool is_open = false;
    transistor_switch(false);
    ESP_LOGE(TAG, "Supposed to be closed");
    while (1) {
        sleep (5);
        transistor_switch(is_open);
        ESP_LOGE(TAG,"Open in light_manager? %d", is_open);
        is_open = !is_open;
    }

}
#else

#define START_H 7
#define END_H  9
void light_manager(void *pvParameter) {
    struct Period morning = { 7, 0, 9, 0};
    struct Period evening = { 19, 0, 22, 0 };
    while (1) {
        // check that new value has arrived
        if (uxQueueMessagesWaiting(bool_queue) != 0) {
            // only one by loop, I'm too lazy
            bool is_evening;
            xQueueReceive(bool_queue, &is_evening, 0);
            xQueueReceive(period_queue, (is_evening? &evening : &morning), 0);
            ESP_LOGI(TAG, "New period set for %s", (is_evening? "evening":"morning"));
            print_period((is_evening? &evening : &morning));
        }
        struct tm* time = fill_time();
        if (time->tm_year == 70) {
            ESP_LOGE(TAG, "Time not yet updated");
        } else {
            if ( is_time_in(time, &morning) || is_time_in(time, &evening)) {
                if ( !is_light_on() ) {
                    transistor_switch(true);
                    ESP_LOGI(TAG, "Light on");
                }
            } else if ( is_light_on() ) {
                    transistor_switch(false);
                    ESP_LOGI(TAG, "Light off");
            }
        }
        sleep(1);
    }


}
#endif



void app_main(void)
{
    //Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "ESP_WIFI_MODE_STA");
    wifi_init_sta();

    // local time
    setenv("TZ","UTC-1",1);
    tzset();

    // update time
    ESP_LOGI(TAG, "Set SNTP update");
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "pool.ntp.org");
    sntp_init();
    // pin init
    pin_init();

    // queue to transmit messages
    period_queue = xQueueCreate(5, sizeof(struct Period));
    bool_queue = xQueueCreate(5, sizeof(bool));

    // udp server
#ifdef CONFIG_EXAMPLE_IPV4
    xTaskCreate(udp_server_task, "udp_server", 4096, (void*)AF_INET, 5, NULL);
#endif
#ifdef CONFIG_EXAMPLE_IPV6
    xTaskCreate(udp_server_task, "udp_server", 4096, (void*)AF_INET6, 5, NULL);
#endif

    xTaskCreate(light_manager, "light_manager", 4096, NULL, 5, NULL);



}
