/**
 * HTTP Webserver for Korvo1 Device Control
 * 
 * Provides:
 * - Device status dashboard
 * - Action control (LED, volume, pause/play)
 * - REST API endpoints for actions
 */

#include "webserver.h"
#include "action_manager.h"
#include "wifi_manager.h"
#include "voice_assistant.h"
#include "sensor_integration.h"
#include "audio_file_manager.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>

#define TAG "webserver"
#define DEFAULT_PORT 80

// Log buffer configuration
#define LOG_BUFFER_SIZE (64 * 1024)  // 64KB log buffer in PSRAM
#define MAX_LOG_ENTRIES 1000
#define MAX_LOG_LINE_LENGTH 512

// Log entry structure
typedef struct {
    uint64_t timestamp_ms;
    esp_log_level_t level;
    char tag[16];
    char message[MAX_LOG_LINE_LENGTH];
} log_entry_t;

// Log buffer structure
typedef struct {
    log_entry_t *entries;
    size_t capacity;
    size_t count;
    size_t write_index;
    SemaphoreHandle_t mutex;
    bool initialized;
} log_buffer_t;

static log_buffer_t s_log_buffer = {0};
static vprintf_like_t s_original_vprintf = NULL;

// Complete webserver structure definition
struct webserver {
    webserver_config_t config;
    httpd_handle_t server;
    bool running;
};

// Forward declarations
static esp_err_t root_handler(httpd_req_t *req);
static esp_err_t api_status_handler(httpd_req_t *req);
static esp_err_t api_action_handler(httpd_req_t *req);
static esp_err_t api_state_handler(httpd_req_t *req);
static esp_err_t api_logs_handler(httpd_req_t *req);
static esp_err_t api_sensors_handler(httpd_req_t *req);
static esp_err_t api_audio_list_handler(httpd_req_t *req);
static esp_err_t api_audio_play_handler(httpd_req_t *req);
static esp_err_t api_audio_upload_handler(httpd_req_t *req);
static int log_vprintf(const char *fmt, va_list args);

// HTML dashboard
static const char* html_dashboard = 
"<!DOCTYPE html>"
"<html><head>"
"<meta charset='UTF-8'>"
"<meta name='viewport' content='width=device-width, initial-scale=1.0'>"
"<title>Korvo1 Control Dashboard</title>"
"<style>"
"* { margin: 0; padding: 0; box-sizing: border-box; }"
"body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif; background: #1a1a1a; color: #e0e0e0; padding: 20px; }"
".container { max-width: 1200px; margin: 0 auto; }"
"h1 { color: #4CAF50; margin-bottom: 20px; }"
".card { background: #2a2a2a; border-radius: 8px; padding: 20px; margin-bottom: 20px; box-shadow: 0 2px 8px rgba(0,0,0,0.3); }"
".card h2 { color: #64B5F6; margin-bottom: 15px; font-size: 1.2em; }"
".status-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(200px, 1fr)); gap: 15px; }"
".status-item { padding: 10px; background: #333; border-radius: 4px; }"
".status-item label { display: block; font-size: 0.9em; color: #aaa; margin-bottom: 5px; }"
".status-item .value { font-size: 1.1em; font-weight: bold; }"
".status-on { color: #4CAF50; }"
".status-off { color: #f44336; }"
".button { background: #4CAF50; color: white; border: none; padding: 10px 20px; border-radius: 4px; cursor: pointer; font-size: 1em; margin: 5px; }"
".button:hover { background: #45a049; }"
".button-danger { background: #f44336; }"
".button-danger:hover { background: #da190b; }"
".button-primary { background: #2196F3; }"
".button-primary:hover { background: #1976D2; }"
"input[type='text'], input[type='number'], input[type='color'], input[type='range'], select { padding: 8px; border: 1px solid #555; border-radius: 4px; background: #333; color: #e0e0e0; width: 100%; margin: 5px 0; }"
".control-group { display: flex; gap: 10px; align-items: center; flex-wrap: wrap; margin: 10px 0; }"
".control-group label { min-width: 100px; }"
".control-group input[type='range'] { flex: 1; min-width: 150px; }"
".json-viewer { background: #1e1e1e; padding: 15px; border-radius: 4px; overflow-x: auto; font-family: 'Courier New', monospace; font-size: 0.9em; white-space: pre-wrap; }"
".refresh-btn { position: fixed; bottom: 20px; right: 20px; background: #2196F3; padding: 15px; border-radius: 50%; width: 60px; height: 60px; box-shadow: 0 4px 12px rgba(0,0,0,0.4); }"
"</style>"
"</head><body>"
"<div class='container'>"
"<h1>🎤 Korvo1 Control Dashboard</h1>"
"<p style='color: #aaa; margin-bottom: 20px;'>Accessible at <a href='http://nap.local' style='color: #64B5F6;'>nap.local</a></p>"
"<div class='card'><h2>Device Status</h2>"
"<div class='status-grid' id='status-grid'></div>"
"</div>"
"<div class='card'><h2>LED Control</h2>"
"<div class='control-group'>"
"<button class='button' onclick='setLEDPattern(\"rainbow\")'>Rainbow</button>"
"<button class='button' onclick='setLEDPattern(\"clear\")'>Clear</button>"
"</div>"
"<div class='control-group'>"
"<label>Color:</label>"
"<input type='color' id='led-color' value='#ff8800'>"
"<button class='button' onclick='setLEDColor()'>Set Color</button>"
"</div>"
"<div class='control-group'>"
"<label>Intensity:</label>"
"<input type='range' id='led-intensity' min='0' max='100' value='30'>"
"<span id='led-intensity-value'>30%</span>"
"<button class='button' onclick='setLEDIntensity()'>Set Intensity</button>"
"</div>"
"</div>"
"<div class='card'><h2>Audio Control</h2>"
"<div class='control-group'>"
"<label>Volume:</label>"
"<input type='range' id='volume' min='0' max='100' value='100'>"
"<span id='volume-value'>100%</span>"
"<button class='button' onclick='setVolume()'>Set Volume</button>"
"</div>"
"<div class='control-group'>"
"<button class='button button-danger' onclick='pauseDevice()'>Pause</button>"
"<button class='button button-primary' onclick='resumeDevice()'>Resume</button>"
"</div>"
"</div>"
"<div class='card'><h2>MP3 Player</h2>"
"<div class='control-group'>"
"<label>Select Track:</label>"
"<select id='audio-track' style='flex: 1; min-width: 200px;'>"
"<option value=''>Loading tracks...</option>"
"</select>"
"<button class='button button-primary' onclick='playAudio()'>Play</button>"
"<button class='button button-danger' onclick='stopAudio()'>Stop</button>"
"</div>"
"<div id='audio-status' style='margin-top: 10px; color: #aaa;'>Ready</div>"
"</div>"
"<div class='card'><h2>Upload MP3 Files</h2>"
"<div id='upload-area' style='border: 2px dashed #555; border-radius: 8px; padding: 40px; text-align: center; background: #222; cursor: pointer; transition: all 0.3s;'>"
"<div style='font-size: 48px; margin-bottom: 10px;'>📁</div>"
"<div style='font-size: 18px; margin-bottom: 5px;'>Drag & Drop MP3 files here</div>"
"<div style='color: #888; font-size: 14px;'>or click to select files</div>"
"<input type='file' id='file-input' multiple accept='.mp3,audio/mpeg' style='display: none;'>"
"</div>"
"<div id='upload-progress' style='margin-top: 15px; display: none;'>"
"<div style='background: #333; border-radius: 4px; padding: 10px; margin-bottom: 10px;'>"
"<div style='display: flex; justify-content: space-between; margin-bottom: 5px;'>"
"<span id='upload-filename'></span>"
"<span id='upload-percent'>0%</span>"
"</div>"
"<div style='background: #111; border-radius: 2px; height: 8px; overflow: hidden;'>"
"<div id='upload-bar' style='background: #2196F3; height: 100%; width: 0%; transition: width 0.3s;'></div>"
"</div>"
"</div>"
"<div id='upload-status' style='color: #aaa; font-size: 14px;'></div>"
"</div>"
"</div>"
"<div class='card'><h2>Device State</h2>"
"<button class='button' onclick='loadState()'>Refresh State</button>"
"<div class='json-viewer' id='state-viewer'>Loading...</div>"
"</div>"
"<div class='card'><h2>Feature Status</h2>"
"<div id='feature-checklist' style='display: grid; grid-template-columns: repeat(auto-fit, minmax(250px, 1fr)); gap: 15px; margin-bottom: 20px;'>"
"<div class='feature-item' data-feature='spiffs'><span class='feature-check'>❌</span> SPIFFS Storage</div>"
"<div class='feature-item' data-feature='sdcard'><span class='feature-check'>❌</span> SD Card</div>"
"<div class='feature-item' data-feature='led'><span class='feature-check'>❌</span> LED Strip</div>"
"<div class='feature-item' data-feature='audio'><span class='feature-check'>❌</span> Audio Player</div>"
"<div class='feature-item' data-feature='ble'><span class='feature-check'>❌</span> BLE Service</div>"
"<div class='feature-item' data-feature='wifi'><span class='feature-check'>❌</span> WiFi Connection</div>"
"<div class='feature-item' data-feature='mdns'><span class='feature-check'>❌</span> mDNS (nap.local)</div>"
"<div class='feature-item' data-feature='webserver'><span class='feature-check'>❌</span> Web Server</div>"
"<div class='feature-item' data-feature='sensors'><span class='feature-check'>❌</span> Sensor Integration</div>"
"<div class='feature-item' data-feature='voice'><span class='feature-check'>❌</span> Voice Assistant</div>"
"<div class='feature-item' data-feature='wakeword'><span class='feature-check'>❌</span> Wake Word Detection</div>"
"<div class='feature-item' data-feature='audiofiles'><span class='feature-check'>❌</span> Audio File Manager</div>"
"</div>"
"<style>"
".feature-item { padding: 10px; background: #2a2a2a; border-radius: 5px; display: flex; align-items: center; gap: 10px; transition: all 0.3s; }"
".feature-item .feature-check { font-size: 1.2em; min-width: 20px; }"
".feature-item.working { background: #1b5e20; border: 2px solid #4CAF50; }"
".feature-item.working .feature-check { color: #4CAF50; }"
".feature-item.failed { background: #b71c1c; border: 2px solid #f44336; }"
".feature-item.failed .feature-check { color: #f44336; }"
"</style>"
"</div>"
"<div class='card'><h2>Console Logs</h2>"
"<div style='display: flex; gap: 10px; margin-bottom: 10px;'>"
"<button class='button' onclick='loadLogs()'>Refresh Logs</button>"
"<button class='button' onclick='clearLogs()'>Clear</button>"
"<label><input type='checkbox' id='auto-refresh-logs' checked onchange='toggleAutoRefresh()'> Auto-refresh (5s)</label>"
"</div>"
"<div id='logs-container' style='max-height: 500px; overflow-y: auto; background: #1e1e1e; padding: 15px; border-radius: 4px; font-family: monospace; font-size: 0.85em; line-height: 1.4;'>"
"<div style='color: #888;'>Loading logs...</div>"
"</div>"
"</div>"
"</div>"
"<button class='button refresh-btn' onclick='refreshAll()' title='Refresh All'>🔄</button>"
"<script>"
"function refreshAll() { loadStatus(); loadState(); }"
"function loadStatus() {"
"  fetch('/api/status').then(r=>r.json()).then(data=>{"
"    const grid = document.getElementById('status-grid');"
"    grid.innerHTML = '';"
"    const items = ["
"      {label:'WiFi', value: data.wifi_connected ? 'Connected: ' + (data.wifi_ssid || 'N/A') : 'Disconnected', status: data.wifi_connected},"
"      {label:'IP Address', value: data.ip_address || 'N/A'},"
"      {label:'Voice Assistant', value: data.voice_assistant_active ? 'Active' : 'Inactive', status: data.voice_assistant_active},"
"      {label:'Firmware', value: data.firmware_version || '0.1'},"
"      {label:'Free Heap', value: (data.free_heap / 1024).toFixed(1) + ' KB'},"
"      {label:'Uptime', value: (data.uptime_seconds / 60).toFixed(1) + ' min'}"
"    ];"
"    items.forEach(item=>{"
"      const div = document.createElement('div');"
"      div.className = 'status-item';"
"      div.innerHTML = `<label>${item.label}</label><div class='value ${item.status !== undefined ? (item.status ? 'status-on' : 'status-off') : ''}'>${item.value}</div>`;"
"      grid.appendChild(div);"
"    });"
"  }).catch(e=>console.error('Status error:', e));"
"}"
"function setLEDColor() {"
"  const color = document.getElementById('led-color').value;"
"  const r = parseInt(color.substr(1,2), 16);"
"  const g = parseInt(color.substr(3,2), 16);"
"  const b = parseInt(color.substr(5,2), 16);"
"  executeAction({Action: 'LED', Data: {color: [r, g, b]}});"
"}"
"function setLEDPattern(pattern) {"
"  executeAction({Action: 'LED', Data: {pattern: pattern}});"
"}"
"function setLEDIntensity() {"
"  const intensity = parseInt(document.getElementById('led-intensity').value) / 100;"
"  executeAction({Action: 'SetLEDIntensity', Data: {Intensity: intensity}});"
"}"
"function setVolume() {"
"  const volume = parseInt(document.getElementById('volume').value) / 100;"
"  executeAction({Action: 'SetVolume', Data: {Volume: volume}});"
"}"
"function pauseDevice() {"
"  executeAction({Action: 'Pause', Data: {}});"
"}"
"function resumeDevice() {"
"  executeAction({Action: 'Play', Data: {}});"
"}"
"function executeAction(action) {"
"  fetch('/api/action', {"
"    method: 'POST',"
"    headers: {'Content-Type': 'application/json'},"
"    body: JSON.stringify(action)"
"  }).then(r=>r.json()).then(data=>{"
"    if (data.success) {"
"      alert('Action executed successfully!');"
"      loadState();"
"    } else {"
"      alert('Error: ' + (data.error || 'Unknown error'));"
"    }"
"  }).catch(e=>alert('Error: ' + e));"
"}"
"function loadState() {"
"  fetch('/api/state').then(r=>r.json()).then(data=>{"
"    document.getElementById('state-viewer').textContent = JSON.stringify(data, null, 2);"
"    if (data.volume !== undefined) {"
"      document.getElementById('volume').value = data.volume * 100;"
"      document.getElementById('volume-value').textContent = Math.round(data.volume * 100) + '%';"
"    }"
"    if (data.led_intensity !== undefined) {"
"      document.getElementById('led-intensity').value = data.led_intensity * 100;"
"      document.getElementById('led-intensity-value').textContent = Math.round(data.led_intensity * 100) + '%';"
"    }"
"  }).catch(e=>document.getElementById('state-viewer').textContent = 'Error: ' + e);"
"}"
"document.getElementById('led-intensity').addEventListener('input', function(e) {"
"  document.getElementById('led-intensity-value').textContent = e.target.value + '%';"
"});"
"document.getElementById('volume').addEventListener('input', function(e) {"
"  document.getElementById('volume-value').textContent = e.target.value + '%';"
"});"
"let autoRefreshLogs = true;"
"function toggleAutoRefresh() { autoRefreshLogs = document.getElementById('auto-refresh-logs').checked; }"
"const featurePatterns = {"
"  spiffs: { success: /SPIFFS mounted|spiffs.*mounted/i, fail: /SPIFFS.*fail|Failed to.*SPIFFS/i },"
"  sdcard: { success: /SD card mounted|✅ SD card/i, fail: /SD.*not available|SD.*fail/i },"
"  led: { success: /LED strip initialized|✅.*LED/i, fail: /Failed.*LED/i },"
"  audio: { success: /Audio player initialized|✅.*audio/i, fail: /Failed.*audio player/i },"
"  ble: { success: /BLE service started|✅.*BLE/i, fail: /Failed.*BLE|BLE.*disabled/i },"
"  wifi: { success: /WiFi connected|✅.*WiFi/i, fail: /Failed.*WiFi|WiFi.*not configured/i },"
"  mdns: { success: /mDNS.*advertised|nap.local|✅.*mDNS/i, fail: /Failed.*mDNS/i },"
"  webserver: { success: /Webserver started|✅.*Webserver/i, fail: /Failed.*webserver/i },"
"  sensors: { success: /Sensor integration started|✅.*Sensor/i, fail: /Failed.*sensor/i },"
"  voice: { success: /Voice assistant.*started|✅.*Voice assistant/i, fail: /Failed.*voice assistant/i },"
"  wakeword: { success: /Wake word.*started|Wake word.*active|✅.*Wake word/i, fail: /Failed.*wake word/i },"
"  audiofiles: { success: /Audio file manager initialized|✅.*Audio file manager/i, fail: /Failed.*audio file manager/i }"
"};"
"function updateFeatureStatus(logMessage) {"
"  for (const [feature, patterns] of Object.entries(featurePatterns)) {"
"    const item = document.querySelector(`[data-feature='${feature}']`);"
"    if (!item) continue;"
"    const check = item.querySelector('.feature-check');"
"    if (patterns.success.test(logMessage) && !item.classList.contains('working')) {"
"      item.classList.add('working');"
"      item.classList.remove('failed');"
"      check.textContent = '✅';"
"    } else if (patterns.fail.test(logMessage) && !item.classList.contains('failed')) {"
"      item.classList.add('failed');"
"      item.classList.remove('working');"
"      check.textContent = '❌';"
"    }"
"  }"
"}"
"function loadLogs() {"
"  fetch('/api/logs').then(r=>r.json()).then(data=>{"
"    const container = document.getElementById('logs-container');"
"    if (data.logs && data.logs.length > 0) {"
"      container.innerHTML = data.logs.map(log=>{"
"        const levelColors = {"
"          'ERROR': '#f44336',"
"          'WARN': '#ff9800',"
"          'INFO': '#4CAF50',"
"          'DEBUG': '#2196F3'"
"        };"
"        const color = levelColors[log.level] || '#e0e0e0';"
"        const time = new Date(log.timestamp_ms).toLocaleTimeString();"
"        const logLine = `[${time}] [${log.tag}] ${escapeHtml(log.message)}`;"
"        // Update feature status based on log message"
"        updateFeatureStatus(log.message);"
"        return `<div style='color: ${color}; margin-bottom: 2px;'>${logLine}</div>`;"
"      }).join('');"
"      container.scrollTop = container.scrollHeight;"
"    } else {"
"      container.innerHTML = '<div style=\"color: #888;\">No logs available</div>';"
"    }"
"  }).catch(e=>{"
"    document.getElementById('logs-container').innerHTML = '<div style=\"color: #f44336;\">Error loading logs: ' + e + '</div>';"
"  });"
"}"
"function escapeHtml(text) {"
"  const div = document.createElement('div');"
"  div.textContent = text;"
"  return div.innerHTML;"
"}"
"function clearLogs() {"
"  document.getElementById('logs-container').innerHTML = '<div style=\"color: #888;\">Logs cleared</div>';"
"}"
"function loadAudioList() {"
"  fetch('/api/audio/list').then(r=>r.json()).then(data=>{"
"    const select = document.getElementById('audio-track');"
"    select.innerHTML = '<option value=\"\">Select a track...</option>';"
"    if (data.tracks && data.tracks.length > 0) {"
"      data.tracks.forEach(track=>{"
"        const opt = document.createElement('option');"
"        opt.value = track.name;"
"        opt.textContent = track.display_name || track.name;"
"        select.appendChild(opt);"
"      });"
"    } else {"
"      select.innerHTML = '<option value=\"\">No tracks available</option>';"
"    }"
"  }).catch(e=>{"
"    document.getElementById('audio-status').textContent = 'Error loading tracks: ' + e;"
"  });"
"}"
"function playAudio() {"
"  const select = document.getElementById('audio-track');"
"  const trackName = select.value;"
"  if (!trackName) {"
"    alert('Please select a track');"
"    return;"
"  }"
"  document.getElementById('audio-status').textContent = 'Playing: ' + select.options[select.selectedIndex].textContent;"
"  fetch('/api/audio/play', {"
"    method: 'POST',"
"    headers: {'Content-Type': 'application/json'},"
"    body: JSON.stringify({name: trackName, volume: 1.0})"
"  }).then(r=>r.json()).then(data=>{"
"    if (data.success) {"
"      document.getElementById('audio-status').textContent = 'Playing: ' + select.options[select.selectedIndex].textContent;"
"    } else {"
"      document.getElementById('audio-status').textContent = 'Error: ' + (data.error || 'Unknown error');"
"    }"
"  }).catch(e=>{"
"    document.getElementById('audio-status').textContent = 'Error: ' + e;"
"  });"
"}"
"function stopAudio() {"
"  fetch('/api/action', {"
"    method: 'POST',"
"    headers: {'Content-Type': 'application/json'},"
"    body: JSON.stringify({Action: 'Pause', Data: {}})"
"  }).then(r=>r.json()).then(data=>{"
"    document.getElementById('audio-status').textContent = 'Stopped';"
"  }).catch(e=>{"
"    document.getElementById('audio-status').textContent = 'Error: ' + e;"
"  });"
"}"
"const uploadArea = document.getElementById('upload-area');"
"const fileInput = document.getElementById('file-input');"
"const uploadProgress = document.getElementById('upload-progress');"
"const uploadBar = document.getElementById('upload-bar');"
"const uploadPercent = document.getElementById('upload-percent');"
"const uploadFilename = document.getElementById('upload-filename');"
"const uploadStatus = document.getElementById('upload-status');"
"uploadArea.addEventListener('click', ()=>fileInput.click());"
"uploadArea.addEventListener('dragover', (e)=>{ e.preventDefault(); uploadArea.style.borderColor='#2196F3'; uploadArea.style.background='#2a2a2a'; });"
"uploadArea.addEventListener('dragleave', ()=>{ uploadArea.style.borderColor='#555'; uploadArea.style.background='#222'; });"
"uploadArea.addEventListener('drop', (e)=>{ e.preventDefault(); uploadArea.style.borderColor='#555'; uploadArea.style.background='#222'; handleFiles(e.dataTransfer.files); });"
"fileInput.addEventListener('change', (e)=>handleFiles(e.target.files));"
"function handleFiles(files) {"
"  if (files.length === 0) return;"
"  Array.from(files).forEach((file, index)=>setTimeout(()=>uploadFile(file, index), index * 100));"
"}"
"function uploadFile(file, index) {"
"  if (!file.name.toLowerCase().endsWith('.mp3')) {"
"    uploadStatus.textContent = 'Error: ' + file.name + ' is not an MP3 file';"
"    uploadStatus.style.color = '#f44336';"
"    return;"
"  }"
"  uploadProgress.style.display = 'block';"
"  uploadFilename.textContent = file.name;"
"  uploadPercent.textContent = '0%';"
"  uploadBar.style.width = '0%';"
"  uploadStatus.textContent = 'Uploading...';"
"  uploadStatus.style.color = '#aaa';"
"  const formData = new FormData();"
"  formData.append('file', file);"
"  const xhr = new XMLHttpRequest();"
"  xhr.upload.addEventListener('progress', (e)=>{"
"    if (e.lengthComputable) {"
"      const percent = Math.round((e.loaded / e.total) * 100);"
"      uploadPercent.textContent = percent + '%';"
"      uploadBar.style.width = percent + '%';"
"    }"
"  });"
"  xhr.addEventListener('load', ()=>{"
"    if (xhr.status === 200) {"
"      const response = JSON.parse(xhr.responseText);"
"      if (response.success) {"
"        uploadStatus.textContent = '✅ Uploaded: ' + file.name + ' (' + formatBytes(response.size) + ')';"
"        uploadStatus.style.color = '#4CAF50';"
"        uploadBar.style.background = '#4CAF50';"
"        setTimeout(()=>{"
"          loadAudioList();"
"          uploadProgress.style.display = 'none';"
"        }, 2000);"
"      } else {"
"        uploadStatus.textContent = '❌ Error: ' + (response.error || 'Upload failed');"
"        uploadStatus.style.color = '#f44336';"
"      }"
"    } else {"
"      uploadStatus.textContent = '❌ Upload failed: HTTP ' + xhr.status;"
"      uploadStatus.style.color = '#f44336';"
"    }"
"  });"
"  xhr.addEventListener('error', ()=>{"
"    uploadStatus.textContent = '❌ Network error during upload';"
"    uploadStatus.style.color = '#f44336';"
"  });"
"  xhr.open('POST', '/api/audio/upload');"
"  xhr.send(formData);"
"}"
"function formatBytes(bytes) {"
"  if (bytes === 0) return '0 Bytes';"
"  const k = 1024;"
"  const sizes = ['Bytes', 'KB', 'MB', 'GB'];"
"  const i = Math.floor(Math.log(bytes) / Math.log(k));"
"  return Math.round(bytes / Math.pow(k, i) * 100) / 100 + ' ' + sizes[i];"
"}"
"// Initial feature status check on page load"
"setTimeout(()=>{ loadLogs(); }, 1000);"
"// Initial feature status check on page load"
"setTimeout(()=>{ loadLogs(); }, 1000);"
"setInterval(()=>{ refreshAll(); if(autoRefreshLogs) loadLogs(); }, 5000);"
"refreshAll();"
"loadLogs();"
"loadAudioList();"
"</script>"
"</body></html>";

// Root handler - serve dashboard
static esp_err_t root_handler(httpd_req_t *req) {
    (void)req;  // Unused
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, html_dashboard, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// API: Get device status
static esp_err_t api_status_handler(httpd_req_t *req) {
    (void)req;
    cJSON *json = cJSON_CreateObject();
    
    // WiFi status
    cJSON_AddBoolToObject(json, "wifi_connected", wifi_manager_is_connected());
    
    // Get IP address
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif) {
        esp_netif_ip_info_t ip_info;
        if (esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
            char ip_str[16];
            snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ip_info.ip));
            cJSON_AddStringToObject(json, "ip_address", ip_str);
        }
    }
    
    // Voice assistant status
    cJSON_AddBoolToObject(json, "voice_assistant_active", voice_assistant_is_active());
    
    // System info
    cJSON_AddNumberToObject(json, "free_heap", esp_get_free_heap_size());
    cJSON_AddNumberToObject(json, "uptime_seconds", esp_timer_get_time() / 1000000);
    cJSON_AddStringToObject(json, "firmware_version", "0.1");
    
    char *json_str = cJSON_Print(json);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_str, HTTPD_RESP_USE_STRLEN);
    free(json_str);
    cJSON_Delete(json);
    return ESP_OK;
}

// API: Execute action
static esp_err_t api_action_handler(httpd_req_t *req) {
    cJSON *json = cJSON_CreateObject();
    esp_err_t err = ESP_OK;
    
    // Read request body
    char content[512];
    int ret = httpd_req_recv(req, content, sizeof(content) - 1);
    if (ret <= 0) {
        cJSON_AddBoolToObject(json, "success", false);
        cJSON_AddStringToObject(json, "error", "Failed to read request");
    } else {
        content[ret] = '\0';
        ESP_LOGI(TAG, "Received action request: %s", content);
        
        // Execute action via action_manager
        err = action_manager_execute_json(content);
        cJSON_AddBoolToObject(json, "success", err == ESP_OK);
        if (err != ESP_OK) {
            cJSON_AddStringToObject(json, "error", esp_err_to_name(err));
        }
    }
    
    char *json_str = cJSON_Print(json);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_str, HTTPD_RESP_USE_STRLEN);
    free(json_str);
    cJSON_Delete(json);
    return ESP_OK;
}

// API: Get device state
static esp_err_t api_state_handler(httpd_req_t *req) {
    (void)req;
    device_state_t state;
    cJSON *json = cJSON_CreateObject();
    
    esp_err_t err = action_manager_get_state(&state);
    if (err == ESP_OK) {
        cJSON_AddBoolToObject(json, "paused", state.paused);
        cJSON_AddNumberToObject(json, "volume", state.current_volume);
        cJSON_AddNumberToObject(json, "led_intensity", state.current_led_intensity);
        cJSON_AddBoolToObject(json, "audio_playing", state.audio_playing);
    } else {
        cJSON_AddStringToObject(json, "error", esp_err_to_name(err));
    }
    
    char *json_str = cJSON_Print(json);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_str, HTTPD_RESP_USE_STRLEN);
    free(json_str);
    cJSON_Delete(json);
    return ESP_OK;
}

esp_err_t webserver_start(webserver_t **out_server, const webserver_config_t *cfg) {
    if (!out_server || !cfg) {
        return ESP_ERR_INVALID_ARG;
    }
    
    struct webserver *ws = calloc(1, sizeof(struct webserver));
    if (!ws) {
        return ESP_ERR_NO_MEM;
    }
    
    ws->config = *cfg;
    if (ws->config.port <= 0) {
        ws->config.port = DEFAULT_PORT;
    }
    
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = ws->config.port;
    config.max_uri_handlers = 10;
    config.max_open_sockets = 7;
    
    esp_err_t err = httpd_start(&ws->server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server: %s", esp_err_to_name(err));
        free(ws);
        return err;
    }
    
    // Register handlers
    httpd_uri_t root_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = root_handler,
        .user_ctx = ws
    };
    httpd_register_uri_handler(ws->server, &root_uri);
    
    httpd_uri_t status_uri = {
        .uri = "/api/status",
        .method = HTTP_GET,
        .handler = api_status_handler,
        .user_ctx = ws
    };
    httpd_register_uri_handler(ws->server, &status_uri);
    
    httpd_uri_t action_uri = {
        .uri = "/api/action",
        .method = HTTP_POST,
        .handler = api_action_handler,
        .user_ctx = ws
    };
    httpd_register_uri_handler(ws->server, &action_uri);
    
    httpd_uri_t state_uri = {
        .uri = "/api/state",
        .method = HTTP_GET,
        .handler = api_state_handler,
        .user_ctx = ws
    };
    httpd_register_uri_handler(ws->server, &state_uri);
    
    httpd_uri_t logs_uri = {
        .uri = "/api/logs",
        .method = HTTP_GET,
        .handler = api_logs_handler,
        .user_ctx = ws
    };
    httpd_register_uri_handler(ws->server, &logs_uri);
    
    httpd_uri_t sensors_uri = {
        .uri = "/api/sensors",
        .method = HTTP_GET,
        .handler = api_sensors_handler,
        .user_ctx = ws
    };
    httpd_register_uri_handler(ws->server, &sensors_uri);
    
    httpd_uri_t audio_list_uri = {
        .uri = "/api/audio/list",
        .method = HTTP_GET,
        .handler = api_audio_list_handler,
        .user_ctx = ws
    };
    httpd_register_uri_handler(ws->server, &audio_list_uri);
    
    httpd_uri_t audio_play_uri = {
        .uri = "/api/audio/play",
        .method = HTTP_POST,
        .handler = api_audio_play_handler,
        .user_ctx = ws
    };
    httpd_register_uri_handler(ws->server, &audio_play_uri);
    
    httpd_uri_t audio_upload_uri = {
        .uri = "/api/audio/upload",
        .method = HTTP_POST,
        .handler = api_audio_upload_handler,
        .user_ctx = ws
    };
    httpd_register_uri_handler(ws->server, &audio_upload_uri);
    
    // Initialize log buffer and hook into logging system
    if (!s_log_buffer.initialized) {
        // Allocate log buffer from PSRAM
        s_log_buffer.entries = (log_entry_t *)heap_caps_malloc(
            MAX_LOG_ENTRIES * sizeof(log_entry_t),
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
        );
        if (!s_log_buffer.entries) {
            ESP_LOGW(TAG, "Failed to allocate log buffer from PSRAM, using internal RAM");
            s_log_buffer.entries = (log_entry_t *)malloc(MAX_LOG_ENTRIES * sizeof(log_entry_t));
        }
        
        if (s_log_buffer.entries) {
            s_log_buffer.capacity = MAX_LOG_ENTRIES;
            s_log_buffer.count = 0;
            s_log_buffer.write_index = 0;
            s_log_buffer.mutex = xSemaphoreCreateMutex();
            if (s_log_buffer.mutex) {
                s_log_buffer.initialized = true;
                // Hook into ESP-IDF logging system
                s_original_vprintf = esp_log_set_vprintf(log_vprintf);
                ESP_LOGI(TAG, "Log buffer initialized (%zu entries)", MAX_LOG_ENTRIES);
            } else {
                ESP_LOGE(TAG, "Failed to create log buffer mutex");
                if (s_log_buffer.entries) {
                    free(s_log_buffer.entries);
                    s_log_buffer.entries = NULL;
                }
            }
        } else {
            ESP_LOGE(TAG, "Failed to allocate log buffer");
        }
    }
    
    ws->running = true;
    *out_server = (webserver_t *)ws;
    
    ESP_LOGI(TAG, "HTTP server started on port %d", ws->config.port);
    ESP_LOGI(TAG, "Access dashboard at http://nap.local/ or http://<device-ip>/");
    return ESP_OK;
}

void webserver_stop(webserver_t *server) {
    if (!server) {
        return;
    }
    
    struct webserver *ws = (struct webserver *)server;
    if (!ws->running) {
        return;
    }
    
    httpd_stop(ws->server);
    ws->running = false;
    free(ws);
    ESP_LOGI(TAG, "HTTP server stopped");
}

bool webserver_is_running(webserver_t *server) {
    if (!server) {
        return false;
    }
    struct webserver *ws = (struct webserver *)server;
    return ws->running;
}

// API: List available audio tracks
static esp_err_t api_audio_list_handler(httpd_req_t *req) {
    (void)req;
    
    char **names = NULL;
    size_t count = 0;
    
    esp_err_t ret = audio_file_manager_get_all_names(&names, &count);
    if (ret != ESP_OK) {
        cJSON *json = cJSON_CreateObject();
        cJSON_AddNumberToObject(json, "count", 0);
        cJSON_AddArrayToObject(json, "tracks");
        char *json_str = cJSON_Print(json);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, json_str, HTTPD_RESP_USE_STRLEN);
        free(json_str);
        cJSON_Delete(json);
        return ESP_OK;
    }
    
    cJSON *json = cJSON_CreateObject();
    cJSON *tracks = cJSON_CreateArray();
    
    for (size_t i = 0; i < count; i++) {
        audio_file_info_t info;
        if (audio_file_manager_get_by_name(names[i], &info) == ESP_OK) {
            cJSON *track = cJSON_CreateObject();
            cJSON_AddStringToObject(track, "name", info.name);
            cJSON_AddStringToObject(track, "display_name", info.display_name ? info.display_name : info.name);
            cJSON_AddNumberToObject(track, "size", info.data_len);
            cJSON_AddItemToArray(tracks, track);
        }
    }
    
    cJSON_AddItemToObject(json, "tracks", tracks);
    cJSON_AddNumberToObject(json, "count", count);
    
    // Free names array
    if (names) {
        for (size_t i = 0; i < count; i++) {
            free(names[i]);
        }
        free(names);
    }
    
    char *json_str = cJSON_Print(json);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_str, HTTPD_RESP_USE_STRLEN);
    free(json_str);
    cJSON_Delete(json);
    return ESP_OK;
}

// API: Play audio track
static esp_err_t api_audio_play_handler(httpd_req_t *req) {
    cJSON *json = cJSON_CreateObject();
    esp_err_t err = ESP_OK;
    
    // Read request body
    char content[512];
    int ret = httpd_req_recv(req, content, sizeof(content) - 1);
    if (ret <= 0) {
        cJSON_AddBoolToObject(json, "success", false);
        cJSON_AddStringToObject(json, "error", "Failed to read request");
    } else {
        content[ret] = '\0';
        ESP_LOGI(TAG, "Received audio play request: %s", content);
        
        cJSON *root = cJSON_Parse(content);
        if (root) {
            cJSON *name = cJSON_GetObjectItem(root, "name");
            cJSON *volume = cJSON_GetObjectItem(root, "volume");
            
            if (cJSON_IsString(name)) {
                float vol = 1.0f;
                if (cJSON_IsNumber(volume)) {
                    vol = (float)volume->valuedouble;
                }
                
                err = audio_file_manager_play(name->valuestring, vol, -1);
                cJSON_AddBoolToObject(json, "success", err == ESP_OK);
                if (err != ESP_OK) {
                    cJSON_AddStringToObject(json, "error", esp_err_to_name(err));
                } else {
                    cJSON_AddStringToObject(json, "message", "Playback started");
                }
            } else {
                cJSON_AddBoolToObject(json, "success", false);
                cJSON_AddStringToObject(json, "error", "Missing or invalid 'name' field");
            }
            cJSON_Delete(root);
        } else {
            cJSON_AddBoolToObject(json, "success", false);
            cJSON_AddStringToObject(json, "error", "Invalid JSON");
        }
    }
    
    char *json_str = cJSON_Print(json);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_str, HTTPD_RESP_USE_STRLEN);
    free(json_str);
    cJSON_Delete(json);
    return ESP_OK;
}

// API: Upload MP3 file to SD card
static esp_err_t api_audio_upload_handler(httpd_req_t *req) {
    cJSON *json = cJSON_CreateObject();
    esp_err_t err = ESP_OK;
    FILE *fp = NULL;
    char *boundary = NULL;
    char *filename = NULL;
    char filepath[512] = {0};  // Increased size to prevent truncation, initialized
    size_t total_len = req->content_len;
    size_t received = 0;
    bool in_file_data = false;
    size_t file_bytes_written = 0;
    char *buf = NULL;  // Initialize to NULL - will be allocated below
    
    // Get Content-Type header to extract boundary
    size_t buf_len = httpd_req_get_hdr_value_len(req, "Content-Type") + 1;
    if (buf_len > 1) {
        char *content_type = malloc(buf_len);
        if (content_type) {
            if (httpd_req_get_hdr_value_str(req, "Content-Type", content_type, buf_len) == ESP_OK) {
                // Extract boundary from Content-Type: multipart/form-data; boundary=----WebKitFormBoundary...
                char *boundary_start = strstr(content_type, "boundary=");
                if (boundary_start) {
                    boundary_start += 9; // Skip "boundary="
                    boundary = strdup(boundary_start);
                }
            }
            free(content_type);
        }
    }
    
    if (!boundary) {
        cJSON_AddBoolToObject(json, "success", false);
        cJSON_AddStringToObject(json, "error", "Invalid Content-Type: multipart/form-data required");
        goto cleanup;
    }
    
    // Try SD card first, then SPIFFS
    const char *base_paths[] = {"/sdcard/sounds", "/spiffs/sounds", "/sdcard", "/spiffs", NULL};
    const char *base_path = NULL;
    for (int i = 0; base_paths[i] != NULL; i++) {
        struct stat st;
        if (stat(base_paths[i], &st) == 0 && S_ISDIR(st.st_mode)) {
            base_path = base_paths[i];
            break;
        }
    }
    
    if (!base_path) {
        cJSON_AddBoolToObject(json, "success", false);
        cJSON_AddStringToObject(json, "error", "No storage available (SD card or SPIFFS not mounted)");
        goto cleanup;
    }
    
    // Create sounds directory if it doesn't exist
    char sounds_dir[512];  // Increased size to prevent truncation
    snprintf(sounds_dir, sizeof(sounds_dir), "%s/sounds", base_path);
    struct stat st;
    if (stat(sounds_dir, &st) != 0) {
        // Directory doesn't exist - files will be saved to base_path directly
        // The sounds subdirectory will be created automatically when first file is written
        // For now, use base_path if sounds doesn't exist
        if (stat(base_path, &st) == 0) {
            strncpy(sounds_dir, base_path, sizeof(sounds_dir) - 1);
            sounds_dir[sizeof(sounds_dir) - 1] = '\0';
        }
    }
    
    // Read multipart data
    buf = (char *)malloc(4096);
    if (!buf) {
        cJSON_AddBoolToObject(json, "success", false);
        cJSON_AddStringToObject(json, "error", "Memory allocation failed");
        goto cleanup;
    }
    // Initialize to avoid uninitialized warning
    memset(buf, 0, 4096);
    
    char boundary_marker[128];
    snprintf(boundary_marker, sizeof(boundary_marker), "--%s", boundary);
    char boundary_end[128];
    snprintf(boundary_end, sizeof(boundary_end), "--%s--", boundary);
    
    while (received < total_len) {
        int ret = httpd_req_recv(req, buf, 4096);
        if (ret <= 0) {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }
            break;
        }
        
        received += ret;
        
        // Simple multipart parsing
        if (!in_file_data && !filename) {
            // Look for filename in headers
            char *filename_start = strstr((char *)buf, "filename=\"");
            if (filename_start) {
                filename_start += 10; // Skip "filename=\""
                char *filename_end = strchr(filename_start, '"');
                if (filename_end) {
                    size_t name_len = filename_end - filename_start;
                    filename = malloc(name_len + 1);
                    if (filename) {
                        strncpy(filename, filename_start, name_len);
                        filename[name_len] = '\0';
                        
                        // Build file path - ensure .mp3 extension
                        char safe_filename[256];
                        strncpy(safe_filename, filename, sizeof(safe_filename) - 1);
                        safe_filename[sizeof(safe_filename) - 1] = '\0';
                        
                        // Ensure .mp3 extension
                        size_t name_len = strlen(safe_filename);
                        if (name_len < 4 || strcasecmp(safe_filename + name_len - 4, ".mp3") != 0) {
                            size_t remaining = sizeof(safe_filename) - strlen(safe_filename) - 1;
                            if (remaining >= 4) {
                                strncat(safe_filename, ".mp3", remaining);
                            }
                        }
                        
                        // Use temporary buffer to avoid format truncation warning
                        char temp_path[512];
                        int path_len = snprintf(temp_path, sizeof(temp_path), "%s/%s", sounds_dir, safe_filename);
                        if (path_len > 0 && path_len < (int)sizeof(filepath)) {
                            strncpy(filepath, temp_path, sizeof(filepath) - 1);
                            filepath[sizeof(filepath) - 1] = '\0';
                        } else {
                            // Fallback: use base_path if path is too long
                            snprintf(filepath, sizeof(filepath), "%s/%s", base_path, safe_filename);
                        }
                        ESP_LOGI(TAG, "Uploading file: %s", filepath);
                        
                        // Open file for writing
                        fp = fopen(filepath, "wb");
                        if (!fp) {
                            // Try creating sounds directory and retry
                            char try_dir[512];  // Increased size to prevent truncation
                            snprintf(try_dir, sizeof(try_dir), "%s/sounds", base_path);
                            if (stat(try_dir, &st) != 0) {
                                // Try to create directory (may not work on all filesystems)
                                // If it fails, we'll save to base_path
                            }
                            // Retry with original path or base_path (use temp buffer to avoid truncation warning)
                            if (!fp) {
                                char temp_path2[512];
                                int path_len2 = snprintf(temp_path2, sizeof(temp_path2), "%s/%s", base_path, safe_filename);
                                if (path_len2 > 0 && path_len2 < (int)sizeof(filepath)) {
                                    strncpy(filepath, temp_path2, sizeof(filepath) - 1);
                                    filepath[sizeof(filepath) - 1] = '\0';
                                }
                                fp = fopen(filepath, "wb");
                            }
                            if (!fp) {
                                cJSON_AddBoolToObject(json, "success", false);
                                cJSON_AddStringToObject(json, "error", "Failed to create file - check SD card is mounted");
                                goto cleanup;
                            }
                        }
                    }
                }
            }
            
            // Look for start of file data (empty line after headers)
            char *data_start = strstr((char *)buf, "\r\n\r\n");
            if (data_start && filename && fp) {
                data_start += 4; // Skip "\r\n\r\n"
                size_t data_len = ret - (data_start - (char *)buf);
                if (data_len > 0) {
                    // Check if this is the end boundary
                    if (strstr(data_start, boundary_marker) == data_start) {
                        // End of file data
                        break;
                    }
                    fwrite(data_start, 1, data_len, fp);
                    file_bytes_written += data_len;
                    in_file_data = true;
                }
            }
        } else if (in_file_data && fp) {
            // Write file data
            char *boundary_pos = strstr((char *)buf, boundary_marker);
            if (boundary_pos) {
                // End of file data, write up to boundary
                size_t data_len = boundary_pos - (char *)buf;
                if (data_len > 0) {
                    fwrite(buf, 1, data_len, fp);
                    file_bytes_written += data_len;
                }
                break;
            } else {
                // Write all data
                fwrite(buf, 1, ret, fp);
                file_bytes_written += ret;
            }
        }
    }
    
    if (fp) {
        fclose(fp);
        fp = NULL;
    }
    
    if (filename && file_bytes_written > 0) {
        cJSON_AddBoolToObject(json, "success", true);
        cJSON_AddStringToObject(json, "filename", filename);
        cJSON_AddNumberToObject(json, "size", file_bytes_written);
        cJSON_AddStringToObject(json, "path", filepath);
        ESP_LOGI(TAG, "✅ File uploaded successfully: %s (%zu bytes) to %s", filename, file_bytes_written, filepath);
        
        // Reload audio file list to include new file
        audio_file_manager_init();
    } else {
        cJSON_AddBoolToObject(json, "success", false);
        if (!filename) {
            cJSON_AddStringToObject(json, "error", "No filename in upload");
        } else if (file_bytes_written == 0) {
            cJSON_AddStringToObject(json, "error", "No file data received");
        } else {
            cJSON_AddStringToObject(json, "error", "Upload failed");
        }
    }
    
cleanup:
    if (fp) fclose(fp);
    if (boundary) free(boundary);
    if (filename) free(filename);
    if (buf) free(buf);  // buf is initialized to NULL, then malloc'd or stays NULL
    
    char *json_str = cJSON_Print(json);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_str, HTTPD_RESP_USE_STRLEN);
    free(json_str);
    cJSON_Delete(json);
    return ESP_OK;
}

// Custom vprintf that captures logs
static int log_vprintf(const char *fmt, va_list args) {
    // First, call the original vprintf to maintain normal logging behavior
    int ret = 0;
    if (s_original_vprintf) {
        ret = s_original_vprintf(fmt, args);
    }
    
    // Then capture the log if buffer is initialized
    if (s_log_buffer.initialized && s_log_buffer.mutex) {
        if (xSemaphoreTake(s_log_buffer.mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            // Format the log message
            char message[MAX_LOG_LINE_LENGTH];
            int len = vsnprintf(message, sizeof(message), fmt, args);
            if (len > 0 && len < (int)sizeof(message)) {
                // Extract log level and tag from the format string
                // ESP-IDF log format: "I (timestamp) tag: message"
                esp_log_level_t level = ESP_LOG_INFO;
                char tag[16] = "unknown";
                
                // Parse log level from first character
                if (fmt[0] == 'E') level = ESP_LOG_ERROR;
                else if (fmt[0] == 'W') level = ESP_LOG_WARN;
                else if (fmt[0] == 'I') level = ESP_LOG_INFO;
                else if (fmt[0] == 'D') level = ESP_LOG_DEBUG;
                else if (fmt[0] == 'V') level = ESP_LOG_VERBOSE;
                
                // Try to extract tag (format: "I (123) tag: message")
                const char *tag_start = strchr(fmt, ')');
                if (tag_start) {
                    tag_start++;  // Skip ')'
                    while (*tag_start == ' ') tag_start++;  // Skip spaces
                    const char *tag_end = strchr(tag_start, ':');
                    if (tag_end && (tag_end - tag_start) < (int)sizeof(tag)) {
                        size_t tag_len = tag_end - tag_start;
                        strncpy(tag, tag_start, tag_len);
                        tag[tag_len] = '\0';
                    }
                }
                
                // Add log entry
                log_entry_t *entry = &s_log_buffer.entries[s_log_buffer.write_index];
                entry->timestamp_ms = esp_timer_get_time() / 1000;
                entry->level = level;
                strncpy(entry->tag, tag, sizeof(entry->tag) - 1);
                entry->tag[sizeof(entry->tag) - 1] = '\0';
                strncpy(entry->message, message, sizeof(entry->message) - 1);
                entry->message[sizeof(entry->message) - 1] = '\0';
                
                // Update indices (circular buffer)
                s_log_buffer.write_index = (s_log_buffer.write_index + 1) % s_log_buffer.capacity;
                if (s_log_buffer.count < s_log_buffer.capacity) {
                    s_log_buffer.count++;
                }
            }
            xSemaphoreGive(s_log_buffer.mutex);
        }
    }
    
    return ret;
}

// API: Get console logs
static esp_err_t api_logs_handler(httpd_req_t *req) {
    (void)req;
    cJSON *json = cJSON_CreateObject();
    cJSON *logs_array = cJSON_CreateArray();
    
    if (s_log_buffer.initialized && s_log_buffer.mutex) {
        if (xSemaphoreTake(s_log_buffer.mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            // Return logs in chronological order
            // Since we use a circular buffer, we need to handle wrap-around
            size_t start_index = 0;
            if (s_log_buffer.count == s_log_buffer.capacity) {
                // Buffer is full, start from oldest entry
                start_index = s_log_buffer.write_index;
            }
            
            size_t entries_to_return = s_log_buffer.count;
            if (entries_to_return > 500) entries_to_return = 500;  // Limit to last 500 entries
            
            for (size_t i = 0; i < entries_to_return; i++) {
                size_t idx = (start_index + i) % s_log_buffer.capacity;
                log_entry_t *entry = &s_log_buffer.entries[idx];
                
                cJSON *log_entry = cJSON_CreateObject();
                cJSON_AddNumberToObject(log_entry, "timestamp_ms", entry->timestamp_ms);
                
                const char *level_str = "INFO";
                if (entry->level == ESP_LOG_ERROR) level_str = "ERROR";
                else if (entry->level == ESP_LOG_WARN) level_str = "WARN";
                else if (entry->level == ESP_LOG_DEBUG) level_str = "DEBUG";
                else if (entry->level == ESP_LOG_VERBOSE) level_str = "VERBOSE";
                
                cJSON_AddStringToObject(log_entry, "level", level_str);
                cJSON_AddStringToObject(log_entry, "tag", entry->tag);
                cJSON_AddStringToObject(log_entry, "message", entry->message);
                cJSON_AddItemToArray(logs_array, log_entry);
            }
            
            xSemaphoreGive(s_log_buffer.mutex);
        }
    }
    
    cJSON_AddItemToObject(json, "logs", logs_array);
    cJSON_AddNumberToObject(json, "count", cJSON_GetArraySize(logs_array));
    
    char *json_str = cJSON_Print(json);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_str, HTTPD_RESP_USE_STRLEN);
    free(json_str);
    cJSON_Delete(json);
    return ESP_OK;
}

// API: Get sensor data
static esp_err_t api_sensors_handler(httpd_req_t *req) {
    (void)req;
    
    sensor_integration_data_t sensor_data = sensor_integration_get_data();
    
    cJSON *json = cJSON_CreateObject();
    cJSON *sensors = cJSON_CreateObject();
    
    cJSON_AddNumberToObject(json, "timestamp_ms", (double)(esp_timer_get_time() / 1000));
    
    // SHT4x data (Sensirion SHT4x - Temperature & Humidity)
    cJSON *sht4x = cJSON_CreateObject();
    cJSON_AddNumberToObject(sht4x, "temperature_c", sensor_data.temperature_c);
    cJSON_AddNumberToObject(sht4x, "humidity_rh", sensor_data.humidity_rh);
    cJSON_AddBoolToObject(sht4x, "synthetic", !sensor_data.sht4x_available);
    cJSON_AddItemToObject(sensors, "sht4x", sht4x);
    
    // CM1106S data (Cubic CM1106S - CO2)
    cJSON *cm1106s = cJSON_CreateObject();
    cJSON_AddNumberToObject(cm1106s, "co2_ppm", sensor_data.co2_ppm);
    cJSON_AddBoolToObject(cm1106s, "synthetic", !sensor_data.cm1106s_available);
    cJSON_AddItemToObject(sensors, "cm1106s", cm1106s);
    
    // PM2012 data (Cubic PM2012 - PM1.0+2.5+10+VOC)
    cJSON *pm2012 = cJSON_CreateObject();
    cJSON_AddNumberToObject(pm2012, "pm1_0_ug_m3", sensor_data.pm1_0_ug_m3);
    cJSON_AddNumberToObject(pm2012, "pm2_5_ug_m3", sensor_data.pm2_5_ug_m3);
    cJSON_AddNumberToObject(pm2012, "pm10_ug_m3", sensor_data.pm10_ug_m3);
    cJSON_AddNumberToObject(pm2012, "voc_index", sensor_data.voc_index);
    cJSON_AddBoolToObject(pm2012, "synthetic", !sensor_data.pm2012_available);
    cJSON_AddItemToObject(sensors, "pm2012", pm2012);
    
    // TSL2561 data (AMS TSL2561 - Light)
    cJSON *tsl2561 = cJSON_CreateObject();
    cJSON_AddNumberToObject(tsl2561, "light_lux", sensor_data.light_lux);
    cJSON_AddBoolToObject(tsl2561, "synthetic", !sensor_data.tsl2561_available);
    cJSON_AddItemToObject(sensors, "tsl2561", tsl2561);
    
    // AS7341 data (UV Light)
    cJSON *as7341 = cJSON_CreateObject();
    cJSON_AddNumberToObject(as7341, "uv_index", sensor_data.uv_index);
    cJSON_AddBoolToObject(as7341, "synthetic", !sensor_data.as7341_available);
    cJSON_AddItemToObject(sensors, "as7341", as7341);
    
    // VEML7700 data (VISHAY VEML7700 - Ambient Light)
    cJSON *veml7700 = cJSON_CreateObject();
    cJSON_AddNumberToObject(veml7700, "ambient_lux", sensor_data.ambient_lux);
    cJSON_AddBoolToObject(veml7700, "synthetic", !sensor_data.veml7700_available);
    cJSON_AddItemToObject(sensors, "veml7700", veml7700);
    
    cJSON_AddItemToObject(json, "sensors", sensors);
    
    char *json_str = cJSON_Print(json);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_str, HTTPD_RESP_USE_STRLEN);
    free(json_str);
    cJSON_Delete(json);
    return ESP_OK;
}
