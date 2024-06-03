/*
 * WiFi.c
 *
 *  Created on: 02 giu 2024
 *      Author: Mattia Bonfanti
 */

// WiFi parameters

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "WiFi.h"
#include "esp_http_client.h"

#include "esp_http_server.h"

#include "lwip/err.h"
#include "lwip/sys.h"

#include "mdns.h"
#include "driver/gpio.h"

//#include "nvs.h"

int lun_ap;

static EventGroupHandle_t wifi_event_group;
const int CONNECTED_BIT0 = BIT0;

#define WIFI_SSID_AP     "MyESP32AP"
#define WIFI_PASS_AP     "password123"

#define MAXIMUM_AP   20

wifi_ap_record_t wifi_records[MAXIMUM_AP];

static const char *TAG = "wifi station";

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

extern const uint8_t favicon_ico_start[] asm("_binary_favicon_ico_start");
extern const uint8_t favicon_ico_end[] asm("_binary_favicon_ico_end");

extern const uint8_t app_css_start[] asm("_binary_app_css_start");
extern const uint8_t app_css_end[] asm("_binary_app_css_end");

void connectWifi();
void scanWifi();

esp_err_t err;
nvs_handle_t my_handle;

esp_err_t _nvs_init(void) {
	// Initialize NVS
	err = nvs_flash_init();
	if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
		// NVS partition was truncated and needs to be erased
		// Retry nvs_flash_init
		ESP_ERROR_CHECK(nvs_flash_erase());
		err = nvs_flash_init();
	}
	ESP_ERROR_CHECK(err);
	return err;
}

nvs_handle_t _nvs_open(void) {

	err = nvs_open("storage", NVS_READWRITE, &my_handle);
	if (err != ESP_OK) {
		printf("Error (%s) opening NVS handle!\n", esp_err_to_name(err));
	} else {
		printf("Done\n");
	}
	return my_handle;
}

esp_err_t _nvs_write_str(char *name, char *word) {
	err = nvs_set_str(my_handle, name, word);
	printf((err != ESP_OK) ? "Failed!\n" : "Done\n");
	printf("Committing updates in NVS ... ");
	err = nvs_commit(my_handle);
	printf((err != ESP_OK) ? "Failed!\n" : "Done\n");

	return err;
}

char* _nvs_read_str(char *_name) {

	size_t string_size;
	esp_err_t err = nvs_get_str(my_handle, _name, NULL, &string_size);
	char *value = malloc(string_size);
	err = nvs_get_str(my_handle, _name, value, &string_size);
	switch (err) {
	case ESP_OK:
		printf("Done\n");
		return value;
		break;
	case ESP_ERR_NVS_NOT_FOUND:
		printf("The value is not initialized yet!\n");
		return "";
		break;
	default:
		printf("Error (%s) reading!\n", esp_err_to_name(err));
		return "";
	}
	return NULL;
}

void _nvs_close(void) {
	nvs_close(my_handle);

}

static esp_err_t post_handler(httpd_req_t *req) {
	// Verifica se la richiesta ha un payload (dati inviati)
	if (req->content_len > 0) {
		// Alloca un buffer per leggere i dati
		char *buffer = (char*) malloc(req->content_len + 1);
		memset(buffer, 0, req->content_len + 1);

		// Leggi i dati inviati nel buffer
		int ret = httpd_req_recv(req, buffer, req->content_len);
		if (ret <= 0) {
			free(buffer);
			return ESP_FAIL;
		}

		// Analizza i dati inviati come parametri POST

		char str1[32];
		strcpy(str1, buffer);

		char c[2] = "&";
		char *str3 = strtok(str1, c);

		char *str2 = strstr(str3, "=");
		printf("SSID->%s\n", &str2[1]);

		char *ssid = &str2[1];

		// per estrarre pw
		char *strPw = strstr(buffer, "&");

		char *pw = strstr(strPw, "=");
		printf("Password->%s", &pw[1]);
		char *password = &pw[1];

		//esp_wifi_stop();

		_nvs_open();
		_nvs_write_str("SSID", ssid);
		_nvs_write_str("PW", password);
		_nvs_close();

		//connectWifi();

		// Deallocazione delle risorse
		free(buffer);
	}

	// Invia una risposta al client

	httpd_resp_set_type(req, "text/html");
	httpd_resp_send(req, "Fatto", -1);

	connectWifi();

	return ESP_OK;
}

esp_err_t scan_handler(httpd_req_t *req) {

	scanWifi();

	char *apList = (char*) malloc(sizeof(char) * 1024);

	strcpy(apList, " ");

	for (int i = 0; i < lun_ap; i++) {
		strcat(apList, (char*) wifi_records[i].ssid);
		strcat(apList, "\n");

	}

	httpd_resp_set_type(req, "text/html");

	char page[] =
			"<!DOCTYPE html>"
					"<html>"
					"<head>"
					"<meta name=\"viewport\"  content=\"width=device-width, initial-scale=1, user-scalable=no\" >"
					"<link rel=\"icon\"  type=\"image/x-icon\"  href=\"favicon.ico\" >"
					"<link rel=\"stylesheet\" href=\"app.css\">"
					"</head>"
					"<body>"
					"<div class=\"container\">"
					"<div class=\"top\">"
					"<h5><i> ESP32 WiFiManager</i></h5>"
					"</div>"
					"<div class=\"title\">"
					"<h4>Seleziona SSID a cui desideri collegarti</h4>"
					"</div>"

					"<form action=\"/post\"  method=\"post\"  >"

					"<label for=\"SSID\" >SSID:</label>"
					"<select name=\"ssid\"  id=\"ssid\" >";

	char option[100] = "\0";
	char options[1000] = "\0";

	if (lun_ap > 10)
		lun_ap = 10;

	for (int a = 0; a < lun_ap; a++) {
		sprintf(option, "<option value=\"%s\">%s</option>",
				wifi_records[a].ssid, wifi_records[a].ssid);
		strcat(options, option);

	}

	strcat(page, options);

	char page2[] =
			"</select>"
					"<br><br>"
					"<label for=\"pw\" >Password:</label>"

					"<input type=\"text\"  name=\"pw\"   placeholder=\"password\"  /> <br>"

					"<input class=\"button\" type=\"submit\"  value=\"Conferma\"  /> "
					"</form>"

					"</div>"
					"</body>"

					"</html>";

	strcat(page, page2);

	char *webDoc = (char*) malloc(sizeof(char) * 4098);
	strcpy(webDoc, page);

	httpd_resp_set_type(req, "text/html");
	httpd_resp_send(req, webDoc, -1);

	return ESP_OK;
}

static int s_retry_num = 0;

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;

static void event_handler(void *arg, esp_event_base_t event_base,
		int32_t event_id, void *event_data) {

	//ap
	if (event_id == WIFI_EVENT_AP_STACONNECTED) {
		wifi_event_ap_staconnected_t *event =
				(wifi_event_ap_staconnected_t*) event_data;
		ESP_LOGI(TAG, "station "MACSTR" join, AID=%d", MAC2STR(event->mac),
				event->aid);
	} else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
		wifi_event_ap_stadisconnected_t *event =
				(wifi_event_ap_stadisconnected_t*) event_data;
		ESP_LOGI(TAG, "station "MACSTR" leave, AID=%d", MAC2STR(event->mac),
				event->aid);
	}

	//sta
	if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
		esp_wifi_connect();
	} else if (event_base == WIFI_EVENT
			&& event_id == WIFI_EVENT_STA_DISCONNECTED) {
		if (s_retry_num < 5) {
			esp_wifi_connect();
			s_retry_num++;
			ESP_LOGI(TAG, "retry to connect to the AP");
			gpio_set_direction(35, GPIO_MODE_OUTPUT);
			gpio_set_level(35, 0);
		} else {
			xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
		}
		ESP_LOGI(TAG, "connect to the AP fail");
	} else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
		ip_event_got_ip_t *event = (ip_event_got_ip_t*) event_data;
		ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
		s_retry_num = 0;
		xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
		gpio_set_direction(35, GPIO_MODE_OUTPUT);
		gpio_set_level(35, 1);
	}
}

//To show favicon
static esp_err_t http_server_favicon_ico_handler(httpd_req_t *req) {
	ESP_LOGI(TAG, "favicon.ico requested");

	httpd_resp_set_type(req, "image/x-icon");
	httpd_resp_send(req, (const char*) favicon_ico_start,
			favicon_ico_end - favicon_ico_start);

	return ESP_OK;
}

static esp_err_t http_server_app_css_handler(httpd_req_t *req) {
	ESP_LOGI(TAG, "app.css requested");

	httpd_resp_set_type(req, "text/css");
	httpd_resp_send(req, (const char*) app_css_start,
			app_css_end - app_css_start);

	return ESP_OK;
}

httpd_handle_t start_webserver(void) {
	httpd_config_t config = HTTPD_DEFAULT_CONFIG();

	httpd_handle_t server = NULL;
	if (httpd_start(&server, &config) == ESP_OK) {
		httpd_uri_t root_uri = { .uri = "/", .method = HTTP_GET, .handler =
				scan_handler, .user_ctx = NULL };

		httpd_uri_t post_uri = { .uri = "/post", .method = HTTP_POST, .handler =
				post_handler, .user_ctx = NULL };

		// register favicon.ico handler
		httpd_uri_t favicon_ico = { .uri = "/favicon.ico", .method = HTTP_GET,
				.handler = http_server_favicon_ico_handler, .user_ctx = NULL };
		httpd_register_uri_handler(server, &favicon_ico);

		// register app.css handler
		httpd_uri_t app_css = { .uri = "/app.css", .method = HTTP_GET,
				.handler = http_server_app_css_handler, .user_ctx = NULL };
		httpd_register_uri_handler(server, &app_css);

		httpd_register_uri_handler(server, &root_uri);
		httpd_register_uri_handler(server, &post_uri);

	}

	return server;
}

void stop_webserver(httpd_handle_t server) {
	if (server) {
		httpd_stop(server);
	}
}

void connectWifi() {

	//_nvs_init();
	_nvs_open();

	char *_ssid = _nvs_read_str("SSID");
	char *_pw = _nvs_read_str("PW");

	_nvs_close();

	s_wifi_event_group = xEventGroupCreate();

	ESP_ERROR_CHECK(esp_netif_init());

	ESP_ERROR_CHECK(esp_event_loop_create_default());
	esp_netif_create_default_wifi_ap();
	esp_netif_create_default_wifi_sta();

	wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
	ESP_ERROR_CHECK(esp_wifi_init(&cfg));

	esp_event_handler_instance_t instance_any_id;
	esp_event_handler_instance_t instance_got_ip;
	ESP_ERROR_CHECK(
			esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, &instance_any_id));
	ESP_ERROR_CHECK(
			esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, &instance_got_ip));

	wifi_config_t wifi_config = { .sta = {
	//     .ssid = SSID,
	//   .password = PW,

			.threshold.authmode = WIFI_AUTH_WPA2_PSK,

			.pmf_cfg = { .capable = true, .required = false }, }, };

	strcpy((char*) wifi_config.sta.ssid, _ssid);
	strcpy((char*) wifi_config.sta.password, _pw);

	wifi_config_t ap_config = { .ap = { .ssid = WIFI_SSID_AP, .ssid_len =
			strlen(WIFI_SSID_AP), .password = WIFI_PASS_AP, .max_connection = 4,
			.authmode = WIFI_AUTH_OPEN, }, };

	esp_wifi_set_mode(WIFI_MODE_APSTA);
	esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
	esp_wifi_set_config(WIFI_IF_AP, &ap_config);

	esp_err_t err = mdns_init();
	if (err) {
		printf("MDNS Init failed: %d\n", err);
		return;
	}

	//set hostname
	mdns_hostname_set("Esp32");
	//set default instance
	mdns_instance_name_set("Esp32");

	esp_wifi_start();
	esp_wifi_connect();
	start_webserver();

	ESP_LOGI(TAG, "wifi_init_sta finished.");

	EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
	WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
	pdFALSE,
	pdFALSE,
	portMAX_DELAY);

	if (bits & WIFI_CONNECTED_BIT) {
		ESP_LOGI(TAG, "connected");
	} else if (bits & WIFI_FAIL_BIT) {
		ESP_LOGI(TAG, "Failed to connect");
	} else {
		ESP_LOGE(TAG, "UNEXPECTED EVENT");
	}

	ESP_ERROR_CHECK(
			esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP,
					instance_got_ip));
	ESP_ERROR_CHECK(
			esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, instance_any_id));
	vEventGroupDelete(s_wifi_event_group);
}

void scanWifi() {
	wifi_scan_config_t scan_config = { .ssid = 0, .bssid = 0, .channel = 0,
			.show_hidden = false };

	ESP_ERROR_CHECK(esp_wifi_scan_start(&scan_config,true));

	uint16_t max_records = MAXIMUM_AP;
	ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&max_records, wifi_records));
	//numero di reti trovate
	esp_wifi_scan_get_ap_num(&lun_ap);
	//  wifi_ap_record_t wifi_records[MAXIMUM_AP];

	printf("n.scan %d\n", lun_ap);

	/*
	 // print the list
	 printf("Found %d access points:\n", max_records);
	 printf("\n");
	 printf("               SSID              | Channel | RSSI |   Auth Mode \n");
	 printf("----------------------------------------------------------------\n");
	 for(int i = 0; i < max_records; i++)
	 printf("%d %s \n", i, wifi_records[i].ssid);
	 printf("----------------------------------------------------------------\n");
	 */
}

void startWifi() {
	_nvs_init();
	connectWifi();
}

