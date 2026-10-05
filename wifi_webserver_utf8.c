/**
 * @file wifi_webserver.c
 * @brief WIFI Web配置服务器模块
 * 
 * 实现基于HTTP服务器的WIFI配置页面：
 * - 提供WIFI配置Web界面
 * - 处理配置表单提交
 * - 扫描结果显示
 */

#include "wifi_manager.h"
#include <esp_http_server.h>
#include <esp_log.h>
#include <string.h>
#include <stdlib.h>

#include "wifi_webserver_internal.h"
#include "captive_portal.h"

/* 日志标签 */
static const char *TAG = "wifi_webserver";

/* HTTP服务器句柄 */
static httpd_handle_t s_server = NULL;

/**
 * @brief 获取HTTP服务器句柄（组件内部接口）
 *
 * 功能：供 captive_portal.c 注册 302 重定向通配处理器
 * 修改：2026-10-03 新增
 *
 * @return 服务器句柄，未启动时返回 NULL
 */
httpd_handle_t wifi_webserver_handle(void)
{
    return s_server;
}

static void wifi_connect_wrapper(void *arg) {
    (void)arg;
    wifi_connect();
    vTaskDelete(NULL);
}

/**
 * @brief Web配置页面HTML
 */
static const char *WEB_CONFIG_PAGE =
    "<!DOCTYPE html>"
    "<html>"
    "<head>"
    "<meta charset=\"UTF-8\">"
    "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">"
    "<title>WIFI配置</title>"
    "<style>"
    "body{font-family:Arial,sans-serif;margin:20px;background:#f0f0f0}"
    ".container{max-width:400px;margin:0 auto;background:#fff;padding:20px;border-radius:8px;box-shadow:0 2px 10px rgba(0,0,0,0.1)}"
    "h1{text-align:center;color:#333}"
    ".form-group{margin-bottom:15px}"
    "label{display:block;margin-bottom:5px;color:#555}"
    "input[type=text],input[type=password]{width:100%;padding:10px;border:1px solid #ddd;border-radius:4px;box-sizing:border-box}"
    "button{width:100%;padding:12px;background:#007bff;color:#fff;border:none;border-radius:4px;cursor:pointer;font-size:16px;margin-bottom:10px}"
    "button:hover{background:#0056b3}"
    "button.scan-btn{background:#28a745}"
    "button.scan-btn:hover{background:#218838}"
    ".wifi-list{margin-bottom:15px;max-height:200px;overflow-y:auto}"
    ".wifi-item{padding:10px;border:1px solid #ddd;border-radius:4px;margin-bottom:5px;cursor:pointer}"
    ".wifi-item:hover{background:#e9f5ff}"
    ".wifi-item.selected{background:#cce5ff;border-color:#007bff}"
    ".wifi-name{font-weight:bold}"
    ".wifi-info{font-size:12px;color:#666;margin-top:2px}"
    ".status{margin-top:15px;padding:10px;border-radius:4px;text-align:center;display:none}"
    ".status.success{background:#d4edda;color:#155724;display:block}"
    ".status.error{background:#f8d7da;color:#721c24;display:block}"
    ".scanning{background:#fff3cd;color:#856404;padding:10px;border-radius:4px;text-align:center}"
    "</style>"
    "</head>"
    "<body>"
    "<div class=\"container\">"
    "<h1>WIFI配置</h1>"
    "<div id=\"wifi-list\" class=\"wifi-list\"></div>"
    "<form id=\"configForm\">"
    "<div class=\"form-group\">"
    "<label for=\"ssid\">WIFI名称:</label>"
    "<input type=\"text\" id=\"ssid\" name=\"ssid\" required>"
    "</div>"
    "<div class=\"form-group\">"
    "<label for=\"password\">密码:</label>"
    "<input type=\"password\" id=\"password\" name=\"password\">"
    "</div>"
    "<button type=\"submit\">保存并连接</button>"
    "</form>"
    "<button class=\"scan-btn\" onclick=\"scanWifi()\">扫描可用WIFI</button>"
    "<div id=\"scanning\" style=\"display:none\" class=\"scanning\">正在扫描...</div>"
    "<div id=\"status\" class=\"status\"></div>"
    "</div>"
    "<script>"
    "var selectedSsid = '';"
    "var scanInterval = null;"
    "var scanAttempts = 0;"
    "var SCAN_MAX_ATTEMPTS = 15;"
    "function scanWifi(){"
    "document.getElementById('scanning').style.display='block';"
    "document.getElementById('wifi-list').innerHTML='';"
    "if(scanInterval) clearInterval(scanInterval);"
    "scanAttempts = 0;"
    "fetchScan();"
    "}"
    "function fetchScan(){"
    "scanAttempts++;"
    "if(scanAttempts > SCAN_MAX_ATTEMPTS){"
    "document.getElementById('scanning').style.display='none';"
    "if(scanInterval) clearInterval(scanInterval);"
    "document.getElementById('wifi-list').innerHTML='<div style=\"text-align:center;color:red\">扫描超时，请重试</div>';"
    "return;"
    "}"
    "fetch('/api/scan')"
    ".then(function(response){return response.json();})"
    ".then(function(data){"
    "if(data.success && data.aps){"
    "document.getElementById('scanning').style.display='none';"
    "if(scanInterval) clearInterval(scanInterval);"
    "if(data.aps.length>0){"
    "var html='';"
    "data.aps.forEach(function(ap){"
    "html+='<div class=\"wifi-item\" onclick=\"selectWifi(ap.ssid)\">';"
    "html+='<div class=\"wifi-name\">'+ap.ssid+'</div>';"
    "html+='<div class=\"wifi-info\">信号: '+ap.rssi+' dBm | 加密: '+ap.auth+'</div>';"
    "html+='</div>';"
    "});"
    "document.getElementById('wifi-list').innerHTML=html;"
    "}else{"
    "document.getElementById('wifi-list').innerHTML='<div style=\"text-align:center;color:#666\">未扫描到可用WIFI</div>';"
    "}"
    "}else if(data.scanning){"
    "scanInterval=setTimeout(fetchScan,1000);"
    "}else{"
    "document.getElementById('scanning').style.display='none';"
    "if(scanInterval) clearInterval(scanInterval);"
    "document.getElementById('wifi-list').innerHTML='<div style=\"text-align:center;color:red\">扫描失败</div>';"
    "}"
    "})"
    ".catch(function(error){"
    "document.getElementById('scanning').style.display='none';"
    "if(scanInterval) clearInterval(scanInterval);"
    "document.getElementById('wifi-list').innerHTML='<div style=\"text-align:center;color:red\">请求失败: '+error+'</div>';"
    "});"
    "}"
    "function selectWifi(ssid){"
    "selectedSsid=ssid;"
    "document.getElementById('ssid').value=ssid;"
    "var items=document.querySelectorAll('.wifi-item');"
    "items.forEach(function(item){"
    "if(item.querySelector('.wifi-name').textContent===ssid){"
    "item.classList.add('selected');"
    "}else{"
    "item.classList.remove('selected');"
    "}"
    "});"
    "}"
    "document.getElementById('configForm').addEventListener('submit',function(e){"
    "e.preventDefault();"
    "var ssid=document.getElementById('ssid').value;"
    "var password=document.getElementById('password').value;"
    "fetch('/config',{"
    "method:'POST',"
    "headers:{'Content-Type':'application/json'},"
    "body:JSON.stringify({ssid:ssid,password:password})"
    "})"
    ".then(function(response){return response.json();})"
    ".then(function(data){"
    "var status=document.getElementById('status');"
    "if(data.success){"
    "status.className='status success';"
    "status.textContent='配置已保存，正在尝试连接...';"
    "// 停止轮询，等待连接结果（AP 可能随时关闭）"
    "if(scanInterval) clearInterval(scanInterval);"
    "}else{"
    "status.className='status error';"
    "status.textContent='保存失败:'+data.error;"
    "}"
    "})"
    ".catch(function(error){"
    "var status=document.getElementById('status');"
    "status.className='status error';"
    "status.textContent='请求失败:'+error;"
    "});"
    "});"
    "</script>"
    "</body>"
    "</html>";

static const char *auth_type_str[] = {
    "OPEN", "WEP", "WPA_PSK", "WPA2_PSK", "WPA_WPA2_PSK", 
    "WPA2_ENTERPRISE", "WPA3_PSK", "WPA2_WPA3_PSK", "WAPI_PSK",
    "MAX"
};

static void set_json_response_headers(httpd_req_t *req) {
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
}

static esp_err_t scan_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "GET /api/scan received (content_len=%d)", req->content_len);
    set_json_response_headers(req);

    wifi_ap_record_t ap_list[20];
    uint16_t ap_count = 0;
    esp_err_t err = wifi_scan_get_ap_results(ap_list, 20, &ap_count);

    if (err == ESP_ERR_NOT_FINISHED) {
        /* 扫描进行中，返回 scanning:true 让 JS 继续轮询 */
        httpd_resp_send(req, "{\"success\":false,\"scanning\":true}", 43);
        return ESP_OK;
    }

    if (err == ESP_ERR_NOT_FOUND) {
        /* 尚未启动过扫描，或上次扫描已结束但结果已被消费，启动新的非阻塞扫描 */
        esp_err_t start_err = wifi_scan_start_nonblocking();
        if (start_err != ESP_OK) {
            /* 扫描已在进行或其他错误：如果 s_scanning=true，说明上一次启动成功了，
               但本条件块只在 NOT_FOUND 时进入（s_scanning==false && s_scan_completed==false），
               所以这里大概率是 scan already in progress，返回 scanning:true 让 JS 继续轮询 */
            ESP_LOGW(TAG, "Scan start failed: %s, will retry on next poll", esp_err_to_name(start_err));
            httpd_resp_send(req, "{\"success\":false,\"scanning\":true}", 43);
            return ESP_OK;
        }
        httpd_resp_send(req, "{\"success\":false,\"scanning\":true}", 43);
        return ESP_OK;
    }

    if (err != ESP_OK) {
        httpd_resp_send(req, "{\"success\":false,\"error\":\"Scan failed\"}", 42);
        return ESP_OK;
    }

    /* 扫描完成，返回结果 */
    char response[2048] = "{\"success\":true,\"aps\":[";
    for (int i = 0; i < ap_count; i++) {
        if (i > 0) strcat(response, ",");

        int auth_idx = ap_list[i].authmode < 9 ? ap_list[i].authmode : 8;
        char ap_json[200];
        snprintf(ap_json, sizeof(ap_json),
                 "{\"ssid\":\"%s\",\"rssi\":%d,\"auth\":\"%s\",\"channel\":%d}",
                 ap_list[i].ssid, ap_list[i].rssi,
                 auth_type_str[auth_idx], ap_list[i].primary);
        strcat(response, ap_json);
    }
    strcat(response, "]}");

    httpd_resp_send(req, response, strlen(response));
    return ESP_OK;
}

/**
 * @brief 根页面请求处理器
 * 
 * @param req HTTP请求句柄
 * @return esp_err_t ESP_OK表示成功
 */
static esp_err_t root_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, WEB_CONFIG_PAGE, strlen(WEB_CONFIG_PAGE));
    return ESP_OK;
}

/**
 * @brief 配置提交请求处理器
 * 
 * @param req HTTP请求句柄
 * @return esp_err_t ESP_OK表示成功
 * 
 * 处理流程：
 * 1. 读取请求体
 * 2. 解析JSON数据
 * 3. 提取SSID和密码
 * 4. 保存配置
 * 5. 返回响应
 */
static esp_err_t config_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "POST /config received (content_len=%d)", req->content_len);
    set_json_response_headers(req);

    char buf[256];
    int ret, remaining = req->content_len;

    /* 读取请求体 */
    if (remaining >= sizeof(buf)) {
        httpd_resp_send(req, "{\"error\":\"Request too large\"}", 30);
        return ESP_OK;
    }

    ret = httpd_req_recv(req, buf, remaining);
    if (ret <= 0) {
        httpd_resp_send(req, "{\"error\":\"Read failed\"}", 24);
        return ESP_OK;
    }
    buf[ret] = '\0';

    /* 解析SSID和密码（简单解析，假设格式为 {"ssid":"xxx","password":"yyy"}） */
    char ssid[32] = {0};
    char password[64] = {0};
    
    /* 查找SSID */
    char *ssid_start = strstr(buf, "\"ssid\"");
    if (ssid_start) {
        ssid_start = strchr(ssid_start, ':');
        if (ssid_start) {
            ssid_start = strchr(ssid_start, '"');
            if (ssid_start) {
                ssid_start++;
                char *ssid_end = strchr(ssid_start, '"');
                if (ssid_end) {
                    int len = ssid_end - ssid_start;
                    if (len < sizeof(ssid)) {
                        strncpy(ssid, ssid_start, len);
                        ssid[len] = '\0';
                    }
                }
            }
        }
    }

    /* 查找密码 */
    char *password_start = strstr(buf, "\"password\"");
    if (password_start) {
        password_start = strchr(password_start, ':');
        if (password_start) {
            password_start = strchr(password_start, '"');
            if (password_start) {
                password_start++;
                char *password_end = strchr(password_start, '"');
                if (password_end) {
                    int len = password_end - password_start;
                    if (len < sizeof(password)) {
                        strncpy(password, password_start, len);
                        password[len] = '\0';
                    }
                }
            }
        }
    }

    /* 检查SSID是否为空 */
    if (strlen(ssid) == 0) {
        httpd_resp_send(req, "{\"error\":\"SSID required\"}", 27);
        return ESP_OK;
    }

    /* 构建WIFI配置结构体 */
    wifi_saved_config_t config = {
        .saved = true,
    };
    strncpy(config.ssid, ssid, sizeof(config.ssid) - 1);
    strncpy(config.password, password, sizeof(config.password) - 1);

    /* 保存WIFI配置 */
    esp_err_t err = wifi_config_save(&config);
    if (err != ESP_OK) {
        httpd_resp_send(req, "{\"error\":\"Save failed\"}", 24);
        return ESP_OK;
    }

    /* 返回成功响应 */
    httpd_resp_send(req, "{\"success\":true}", 16);

    /* 在新任务中尝试连接WIFI */
    xTaskCreatePinnedToCore((void(*)(void*))wifi_connect_wrapper, "wifi_connect_task", 
                            4096, NULL, 5, NULL, 0);

    return ESP_OK;
}

/**
 * @brief URI处理器定义
 */
static const httpd_uri_t root_uri = {
    .uri = "/",
    .method = HTTP_GET,
    .handler = root_handler,
    .user_ctx = NULL
};

static const httpd_uri_t config_uri = {
    .uri = "/config",
    .method = HTTP_POST,
    .handler = config_handler,
    .user_ctx = NULL
};

static const httpd_uri_t scan_uri = {
    .uri = "/api/scan",
    .method = HTTP_GET,
    .handler = scan_handler,
    .user_ctx = NULL
};

static esp_err_t options_handler(httpd_req_t *req) {
    set_json_response_headers(req);
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static const httpd_uri_t options_config_uri = {
    .uri = "/config",
    .method = HTTP_OPTIONS,
    .handler = options_handler,
    .user_ctx = NULL
};

static const httpd_uri_t options_scan_uri = {
    .uri = "/api/scan",
    .method = HTTP_OPTIONS,
    .handler = options_handler,
    .user_ctx = NULL
};

/**
 * @brief 启动Web配置服务器
 * 
 * @return esp_err_t ESP_OK表示成功，其他表示失败
 */
esp_err_t wifi_webserver_start(void) {
    if (s_server != NULL) {
        ESP_LOGW(TAG, "Web server already running");
        return ESP_OK;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.ctrl_port = 32768;
    config.max_uri_handlers = 16;   /* 默认 8 个槽位不够（webserver 4 + captive_portal 9），扩到 16 */

    esp_err_t err = httpd_start(&s_server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start web server: %s", esp_err_to_name(err));
        return err;
    }

    /* 注册URI处理器 */
    httpd_register_uri_handler(s_server, &root_uri);
    httpd_register_uri_handler(s_server, &config_uri);
    httpd_register_uri_handler(s_server, &scan_uri);
    httpd_register_uri_handler(s_server, &options_config_uri);
    httpd_register_uri_handler(s_server, &options_scan_uri);

    ESP_LOGI(TAG, "Web server started on port 80");

    /* 启动 Captive Portal：手机/电脑连上热点后自动弹出配网页，无需手动访问 IP。
       AP 网关固定为 192.168.4.1（esp_wifi 内部默认网段） */
    esp_err_t ap_err = captive_portal_start("192.168.4.1");
    if (ap_err != ESP_OK) {
        ESP_LOGW(TAG, "Captive Portal 启动失败：%s", esp_err_to_name(ap_err));
    }

    return ESP_OK;
}

/**
 * @brief 停止Web配置服务器
 *
 * @return esp_err_t ESP_OK表示成功，其他表示失败
 */
esp_err_t wifi_webserver_stop(void) {
    if (s_server == NULL) {
        return ESP_OK;
    }

    /* 先停 Captive Portal 再停 HTTP：DNS 任务需在服务器停止前注销通配处理器 */
    captive_portal_stop();

    httpd_stop(s_server);
    s_server = NULL;

    ESP_LOGI(TAG, "Web server stopped");
    return ESP_OK;
}
