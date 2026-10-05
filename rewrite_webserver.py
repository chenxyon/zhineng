#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Rewrite wifi_webserver.c with WiFiManager-style Chinese HTML page."""

import os

HTML = r"""<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>WiFi配置</title>
<style>
* { margin:0; padding:0; box-sizing:border-box; }
body { font-family: "Microsoft YaHei",sans-serif; background: linear-gradient(135deg,#667eea,#764ba2); min-height:100vh; display:flex; align-items:center; justify-content:center; }
.card { background:#fff; border-radius:12px; padding:24px; width:90%; max-width:420px; box-shadow:0 8px 32px rgba(0,0,0,.18); }
h1 { text-align:center; color:#333; margin-bottom:20px; }
.btn-scan { width:100%; padding:12px; background:#28a745; color:#fff; border:none; border-radius:8px; font-size:16px; cursor:pointer; margin-bottom:12px; }
.btn-scan:hover { background:#218838; }
.btn-save { width:100%; padding:12px; background:#007bff; color:#fff; border:none; border-radius:8px; font-size:16px; cursor:pointer; margin-bottom:10px; }
.btn-save:hover { background:#0056b3; }
label { display:block; margin:10px 0 4px; color:#555; font-size:14px; }
input[type=text],input[type=password] { width:100%; padding:10px 12px; border:1px solid #ccc; border-radius:6px; font-size:15px; }
.wifi-list { max-height:220px; overflow-y:auto; margin-bottom:12px; }
.wifi-item { padding:10px 12px; border:1px solid #ddd; border-radius:6px; margin-bottom:6px; cursor:pointer; }
.wifi-item:hover { background:#e9f5ff; }
.wifi-item.selected { background:#cce5ff; border-color:#007bff; }
.wifi-name { font-weight:bold; }
.wifi-info { font-size:12px; color:#888; margin-top:2px; }
.status { margin-top:12px; padding:10px; border-radius:6px; text-align:center; display:none; font-size:14px; }
.status.success { background:#d4edda; color:#155724; display:block; }
.status.error { background:#f8d7da; color:#721c24; display:block; }
.scanning { background:#fff3cd; color:#856404; padding:10px; border-radius:6px; text-align:center; display:none; margin-bottom:12px; }
</style>
</head>
<body>
<div class="card">
<h1>&#x2736; WiFi &#x914D;&#x7F6E;</h1>
<div id="wifi-list" class="wifi-list"></div>
<div id="scanning" class="scanning">&#x6B63;&#x5728;&#x626B;&#x63CF;...</div>
<form id="configForm">
<label for="ssid">WiFi &#x540D;&#x79F0;</label>
<input type="text" id="ssid" name="ssid" required>
<label for="password">&#x5BC6;&#x7801;</label>
<input type="password" id="password" name="password">
<button type="submit" class="btn-save">&#x4FDD;&#x5B58;&#x5E76;&#x8FDE;&#x63A5;</button>
</form>
<button class="btn-scan" onclick="doScan()">&#x626B;&#x63CF;&#x53EF;&#x7528; WiFi</button>
<div id="status" class="status"></div>
</div>
<script>
var sel='',scanIv=null,scanTry=0,MAX=15;
function doScan(){
  document.getElementById('scanning').style.display='block';
  document.getElementById('wifi-list').innerHTML='';
  if(scanIv) clearInterval(scanIv);
  scanTry=0; fetchScan();
}
function fetchScan(){
  scanTry++;
  if(scanTry>MAX){
    document.getElementById('scanning').style.display='none';
    if(scanIv) clearInterval(scanIv);
    document.getElementById('wifi-list').innerHTML='<div style="text-align:center;color:red">&#x626B;&#x63CF;&#x8D85;&#x65F6;&#xFF0C;&#x8BF7;&#x91CD;&#x8BD5;</div>';
    return;
  }
  fetch('/api/scan').then(function(r){return r.json();})
  .then(function(d){
    if(d.success&&d.aps){
      document.getElementById('scanning').style.display='none';
      if(scanIv) clearInterval(scanIv);
      if(d.aps.length>0){
        var h='';
        d.aps.forEach(function(a){
          h+='<div class="wifi-item" onclick="pick(\''+a.ssid+'\')">';
          h+='<div class="wifi-name">'+a.ssid+'</div>';
          h+='<div class="wifi-info">&#x4FE1;&#x53F7;:'+a.rssi+' dBm | '+a.auth+'</div>';
          h+='</div>';
        });
        document.getElementById('wifi-list').innerHTML=h;
      } else {
        document.getElementById('wifi-list').innerHTML='<div style="text-align:center;color:#888">&#x672A;&#x626B;&#x63CF;&#x5230;&#x53EF;&#x7528;&#x7684;WiFi</div>';
      }
    } else if(d.scanning){
      scanIv=setTimeout(fetchScan,1000);
    } else {
      document.getElementById('scanning').style.display='none';
      if(scanIv) clearInterval(scanIv);
      document.getElementById('wifi-list').innerHTML='<div style="text-align:center;color:red">&#x626B;&#x63CF;&#x5931;&#x8D25;</div>';
    }
  }).catch(function(){
    document.getElementById('scanning').style.display='none';
    if(scanIv) clearInterval(scanIv);
    document.getElementById('wifi-list').innerHTML='<div style="text-align:center;color:red">&#x8BF7;&#x6C42;&#x5931;&#x8D25;</div>';
  });
}
function pick(ssid){
  sel=ssid;
  document.getElementById('ssid').value=ssid;
  document.querySelectorAll('.wifi-item').forEach(function(el){
    el.classList.toggle('selected', el.querySelector('.wifi-name').textContent===ssid);
  });
}
document.getElementById('configForm').addEventListener('submit',function(e){
  e.preventDefault();
  var ssid=document.getElementById('ssid').value.trim();
  var pwd=document.getElementById('password').value;
  if(!ssid){ alert('\u8BF7\u8F93\u5165WiFi\u540D\u79F0'); return; }
  fetch('/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({ssid:ssid,password:pwd})})
  .then(function(r){return r.json();})
  .then(function(d){
    var s=document.getElementById('status');
    if(d.success){
      s.className='status success';
      s.textContent='\u914D\u7F6E\u5DF2\u4FDD\u5B58\uFF0C\u6B63\u5728\u8FDE\u63A5...';
      if(scanIv) clearInterval(scanIv);
    } else {
      s.className='status error';
      s.textContent='\u4FDD\u5B58\u5931\u8D25:'+d.error;
    }
  }).catch(function(err){
    var s=document.getElementById('status');
    s.className='status error';
    s.textContent='\u8BF7\u6C42\u5931\u8D25:'+err;
  });
});
fetchScan();
</script>
</body>
</html>"""

CHEAD = r"""/**
 * @file wifi_webserver.c
 * @brief WIFI Web配置服务器模块
 *
 * 实现基于HTTP服务器的WIFI配置页面：
 * - 提供WiFiManager风格中文配置界面
 * - 处理配置表单提交（/config POST）
 * - 扫描结果显示（/api/scan GET）
 */

#include "wifi_manager.h"
#include <esp_http_server.h>
#include <esp_log.h>
#include <string.h>
#include <stdlib.h>

#include "wifi_webserver_internal.h"
#include "captive_portal.h"

static const char *TAG = "wifi_webserver";
static httpd_handle_t s_server = NULL;

httpd_handle_t wifi_webserver_handle(void) { return s_server; }

static void wifi_connect_wrapper(void *arg) {
    (void)arg;
    wifi_connect();
    vTaskDelete(NULL);
}

"""

# Read original handlers
with open(r'd:\ESP32S3\zhineng\wifi_webserver_utf8.c', 'r', encoding='utf-8') as f:
    orig = f.read()

idx = orig.find('static const char *auth_type_str')
if idx < 0:
    print('ERROR: auth_type_str not found')
    exit(1)
handlers = orig[idx:]

# Build output
parts = []
parts.append(CHEAD)
parts.append('/* WiFiManager\u98CE\u683C\u4E2D\u6587\u914D\u7F6E\u9875 */\n')
parts.append('static const char WEB_CONFIG_PAGE[] = R"(\n')
parts.append(HTML)
parts.append(')";\n\n')
parts.append(handlers)

result = ''.join(parts)
with open(r'd:\components\wifi_manager\src\wifi_webserver.c', 'w', encoding='utf-8') as f:
    f.write(result)

print(f'Done. Total length: {len(result)} bytes')
print(f'Handlers length: {len(handlers)} bytes')