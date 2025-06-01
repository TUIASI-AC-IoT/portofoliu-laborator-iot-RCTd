#include <stdio.h>  
#include <string.h>  
#include <sys/stat.h>  
#include "esp_http_server.h"  
#include "esp_spiffs.h"  
#include "esp_random.h"  
#include "esp_log.h"  
#include "esp_err.h"  
#include "freertos/FreeRTOS.h"  
#include "freertos/task.h"  
  
#define TAG "REST_IOT"  
#define MAX_SENSORS   4       
#define SENSOR_PERIOD 2000     
  
static float sensors[MAX_SENSORS];  
  
static bool file_exists(const char *path)  
{  
    struct stat st;  
    return (stat(path, &st) == 0);  
}  
  
static int sensor_id_from_uri(const char *uri, size_t uri_len, int level)  
{  
    int curr = 0, start = 0, seg = 0;  
    for (size_t i = 0; i < uri_len; ++i) {  
        if (uri[i] == '/' || i == uri_len - 1) {  
            if (seg == level) {  
                /* capturăm segmentul curent ca id */  
                int end = (uri[i] == '/') ? i : i + 1;  
                char tmp[8] = {0};  
                memcpy(tmp, &uri[start], end - start);  
                return atoi(tmp);  
            }  
            seg++;  
            start = i + 1;  
        }  
    }  
    return -1;  
}  
    
static esp_err_t get_sensor_handler(httpd_req_t *req)  
{  
    int id = sensor_id_from_uri(req->uri, req->uri_len, 4);  
    if (id < 0 || id >= MAX_SENSORS) {  
        const char *msg = "{\"error\":\"sensor id invalid\"}";  
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, msg);  
        return ESP_OK;  
    }  
  
    char payload[64];  
    snprintf(payload, sizeof(payload),  
             "{\"sensor\":%d,\"value\":%.2f}", id, sensors[id]);  
  
    httpd_resp_set_type(req, "application/json");  
    httpd_resp_sendstr(req, payload);  
    return ESP_OK;  
}  
  
static esp_err_t post_config_handler(httpd_req_t *req)  
{  
    int id = sensor_id_from_uri(req->uri, req->uri_len, 4);  
    if (id < 0 || id >= MAX_SENSORS) {  
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,  
                                   "{\"error\":\"sensor id invalid\"}");  
    }  
  
    char path[32];  
    snprintf(path, sizeof(path), "/spiffs/s%d/scale.json", id);  
  
    if (file_exists(path)) {  
        return httpd_resp_send_err(req, HTTPD_409_CONFLICT,  
                                   "{\"error\":\"config already exists\"}");  
    }  
  
    char dir[16];  
    snprintf(dir, sizeof(dir), "/spiffs/s%d", id);  
    mkdir(dir, 0777);  
   
    FILE *f = fopen(path, "w");  
    if (!f) {  
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,  
                                   "{\"error\":\"cannot create file\"}");  
    }  
    fputs("{\"scale\":1.0}", f);  
    fclose(f);  
  
    httpd_resp_set_status(req, HTTPD_201);  
    httpd_resp_set_type(req, "application/json");  
    httpd_resp_set_hdr(req, "Location", path);     
    httpd_resp_sendstr(req, "{\"ok\":true}");  
    return ESP_OK;  
}  
  
static esp_err_t put_config_handler(httpd_req_t *req)  
{  
    int id = sensor_id_from_uri(req->uri, req->uri_len, 4);  
    if (id < 0 || id >= MAX_SENSORS) {  
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,  
                                   "{\"error\":\"sensor id invalid\"}");  
    }  
	
    const char *name = strrchr(req->uri, '/');  
    if (!name || strlen(name) < 2) {  
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,  
                                   "{\"error\":\"file name missing\"}");  
    }  
    name++;                                     
    char path[40];  
    snprintf(path, sizeof(path), "/spiffs/s%d/%s", id, name);  
  
    if (!file_exists(path)) {  
        return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND,  
                                   "{\"error\":\"config not found\"}");  
    }  
   
    char buf[512];  
    int received = httpd_req_recv(req, buf, sizeof(buf));  
    if (received <= 0) {  
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,  
                                   "{\"error\":\"empty body\"}");  
    }  
  
    FILE *f = fopen(path, "w");  
    if (!f) {  
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,  
                                   "{\"error\":\"cannot open file\"}");  
    }  
    fwrite(buf, 1, received, f);  
    fclose(f);  
  
    httpd_resp_set_status(req, HTTPD_204);  
    httpd_resp_sendstr(req, NULL);  // fără corp  
    return ESP_OK;  
}  
  
static httpd_handle_t start_webserver(void)  
{  
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();  
    config.uri_match_fn = httpd_uri_match_wildcard;  
  
    httpd_handle_t server = NULL;  
    if (httpd_start(&server, &config) == ESP_OK) {  
  
        httpd_uri_t get_uri = {  
            .uri      = "/api/v1/sensors/*", 
            .method   = HTTP_GET,  
            .handler  = get_sensor_handler,  
            .user_ctx = NULL  
        };  
        httpd_register_uri_handler(server, &get_uri);  
  
        httpd_uri_t post_uri = {  
            .uri      = "/api/v1/sensors/*/config",  
            .method   = HTTP_POST,  
            .handler  = post_config_handler,  
            .user_ctx = NULL  
        };  
        httpd_register_uri_handler(server, &post_uri);  
  
        httpd_uri_t put_uri = {  
            .uri      = "/api/v1/sensors/*/config/*",  
            .method   = HTTP_PUT,  
            .handler  = put_config_handler,  
            .user_ctx = NULL  
        };  
        httpd_register_uri_handler(server, &put_uri);  
  
        ESP_LOGI(TAG, "HTTP server started");  
    }  
    return server;  
}  
   
static void sensor_task(void *arg)  
{  
    while (1) {  
        for (int i = 0; i < MAX_SENSORS; i++) {  
            sensors[i] = (float)(esp_random() % 100);  
        }  
        vTaskDelay(pdMS_TO_TICKS(SENSOR_PERIOD));  
    }  
}  
  
void app_main(void)  
{  
    esp_vfs_spiffs_conf_t cfg = {  
        .base_path = "/spiffs",  
        .partition_label = NULL,  
        .max_files = 8,  
        .format_if_mount_failed = true  
    };  
    ESP_ERROR_CHECK(esp_vfs_spiffs_register(&cfg));  
  
    start_webserver();  
  
    xTaskCreate(sensor_task, "sensor_task", 2048, NULL, 5, NULL);  
}  
