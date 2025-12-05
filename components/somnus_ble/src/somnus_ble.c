/**
 * @file somnus_ble.c
 * @brief Somnus-compatible BLE UART implementation using NimBLE.
 */

#include "somnus_ble.h"

#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "cJSON.h"
#include "esp_err.h"
#include "esp_log.h"
#if defined(CONFIG_BT_ENABLED) || defined(CONFIG_BT_NIMBLE_ENABLED) || defined(CONFIG_BT_BLUEDROID_ENABLED)
#include "esp_bt.h"
#endif
#ifdef CONFIG_BT_NIMBLE_ENABLED
#include "esp_nimble_hci.h"
#elif defined(CONFIG_BT_BLUEDROID_ENABLED)
#include "esp_bt_main.h"
#include "esp_gatt_common_api.h"
#include "esp_gatts_api.h"
#include "esp_bt_defs.h"
#include "esp_gap_ble_api.h"
#endif
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_hs_mbuf.h"
#include "host/ble_uuid.h"
#include "host/util/util.h"
#include "host/ble_gatt.h"
#include "nimble/ble.h"
#include "store/ram/ble_store_ram.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#include "somnus_profile.h"
#ifdef CONFIG_SENSOR_MANAGER_ENABLED
#include "sensor_integration.h"
#endif

#define SOMNUS_BLE_TAG "somnus_ble"

#define SOMNUS_BLE_CMD_MAX_LEN 255
#define SOMNUS_BLE_QUEUE_LENGTH 6
#define SOMNUS_BLE_TASK_STACK 4096
#define SOMNUS_BLE_TASK_PRIO 5

#define SOMNUS_BLE_NOTIFY_CHUNK 20
#define SOMNUS_WIFI_SCAN_MAX_AP 20
#define SOMNUS_BLE_LOG_MAX_ENTRIES 100
#define SOMNUS_BLE_LOG_MSG_MAX_LEN 256

#define SOMNUS_MIN(a, b) ((a) < (b) ? (a) : (b))

typedef struct {
    size_t len;
    char payload[SOMNUS_BLE_CMD_MAX_LEN + 1];
} somnus_ble_cmd_msg_t;

// BLE log entry
typedef enum {
    BLE_LOG_CONNECT,
    BLE_LOG_DISCONNECT,
    BLE_LOG_RX,
    BLE_LOG_TX,
    BLE_LOG_SUBSCRIBE,
} ble_log_type_t;

typedef struct {
    ble_log_type_t type;
    uint32_t timestamp_ms;
    char message[SOMNUS_BLE_LOG_MSG_MAX_LEN];
} ble_log_entry_t;

// BLE log ring buffer
typedef struct {
    ble_log_entry_t entries[SOMNUS_BLE_LOG_MAX_ENTRIES];
    size_t head;
    size_t count;
    portMUX_TYPE lock;
} ble_log_buffer_t;

static somnus_ble_connect_wifi_cb_t s_connect_cb;
static void *s_connect_ctx;
static somnus_ble_device_command_cb_t s_device_command_cb;
static void *s_device_command_ctx;
static bool s_started;
static bool s_notify_enabled;
static uint16_t s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static uint16_t s_tx_val_handle;
static uint8_t s_tx_buffer[512];
static size_t s_tx_buffer_len = 0;

static TaskHandle_t s_cmd_task;
static QueueHandle_t s_cmd_queue;
static portMUX_TYPE s_state_lock = portMUX_INITIALIZER_UNLOCKED;
static ble_log_buffer_t s_ble_log = {0};

// Queue for posting notification work to the NimBLE host task
typedef struct {
    uint16_t conn_handle;
    uint16_t val_handle;
    struct os_mbuf *om;
} notify_work_t;

static QueueHandle_t s_notify_queue = NULL;

/* UUIDs in little-endian format for NimBLE macros */
#define SOMNUS_UUID128_SERVICE BLE_UUID128_DECLARE(0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0, 0x93, 0xF3, 0xA3, 0xB5, 0x01, 0x00, 0x40, 0x6E)
#define SOMNUS_UUID128_CHR_TX BLE_UUID128_DECLARE(0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0, 0x93, 0xF3, 0xA3, 0xB5, 0x03, 0x00, 0x40, 0x6E)
#define SOMNUS_UUID128_CHR_RX BLE_UUID128_DECLARE(0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0, 0x93, 0xF3, 0xA3, 0xB5, 0x02, 0x00, 0x40, 0x6E)

static int somnus_ble_gap_event(struct ble_gap_event *event, void *arg);
static void somnus_ble_start_advertising(void);
static void somnus_ble_on_sync(void);
static void somnus_ble_host_task(void *param);
static void somnus_ble_command_task(void *param);
static void somnus_ble_handle_command(const char *payload);
static void somnus_ble_handle_scan_action(void);
static void somnus_ble_handle_connect_action(const cJSON *root);
static void somnus_ble_handle_read_sensors_action(void);
static int somnus_ble_rx_access_cb(uint16_t conn_handle,
                                   uint16_t attr_handle,
                                   struct ble_gatt_access_ctxt *ctxt,
                                   void *arg);
static int somnus_ble_tx_access_cb(uint16_t conn_handle,
                                   uint16_t attr_handle,
                                   struct ble_gatt_access_ctxt *ctxt,
                                   void *arg);
static esp_err_t somnus_ble_send_chunked(const char *message,
                                         TickType_t inter_chunk_delay);
static char *somnus_ble_perform_wifi_scan(size_t *out_len);

// BLE log functions
static void ble_log_init(void)
{
    s_ble_log.head = 0;
    s_ble_log.count = 0;
    portMUX_TYPE tmp = portMUX_INITIALIZER_UNLOCKED;
    s_ble_log.lock = tmp;
}

static void ble_log_add(ble_log_type_t type, const char *message)
{
    if (!message) {
        return;
    }
    
    portENTER_CRITICAL(&s_ble_log.lock);
    
    size_t idx = (s_ble_log.head + s_ble_log.count) % SOMNUS_BLE_LOG_MAX_ENTRIES;
    ble_log_entry_t *entry = &s_ble_log.entries[idx];
    
    entry->type = type;
    entry->timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000);
    strncpy(entry->message, message, SOMNUS_BLE_LOG_MSG_MAX_LEN - 1);
    entry->message[SOMNUS_BLE_LOG_MSG_MAX_LEN - 1] = '\0';
    
    if (s_ble_log.count < SOMNUS_BLE_LOG_MAX_ENTRIES) {
        s_ble_log.count++;
    } else {
        // Ring buffer full, overwrite oldest entry
        s_ble_log.head = (s_ble_log.head + 1) % SOMNUS_BLE_LOG_MAX_ENTRIES;
    }
    
    portEXIT_CRITICAL(&s_ble_log.lock);
}

static const struct ble_gatt_chr_def somnus_chr_defs[] = {
    {
        .uuid = SOMNUS_UUID128_CHR_TX,
        .access_cb = somnus_ble_tx_access_cb,
        .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
        .val_handle = &s_tx_val_handle,
    },
    {
        .uuid = SOMNUS_UUID128_CHR_RX,
        .access_cb = somnus_ble_rx_access_cb,
        .flags = BLE_GATT_CHR_F_WRITE,
    },
    {0},
};

static const struct ble_gatt_svc_def somnus_svc_defs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = SOMNUS_UUID128_SERVICE,
        .characteristics = somnus_chr_defs,
    },
    {0},
};

static void somnus_ble_gatt_register_cb(struct ble_gatt_register_ctxt *ctxt, void *arg)
{
    (void)arg;
    char uuid_str[BLE_UUID_STR_LEN];
    switch (ctxt->op) {
    case BLE_GATT_REGISTER_OP_SVC:
        ble_uuid_to_str(ctxt->svc.svc_def->uuid, uuid_str);
        ESP_LOGI(SOMNUS_BLE_TAG,
                 "[BLE] Registered service %s handle=%u",
                 uuid_str,
                 ctxt->svc.handle);
        break;
    case BLE_GATT_REGISTER_OP_CHR:
        ble_uuid_to_str(ctxt->chr.chr_def->uuid, uuid_str);
        // Identify characteristic by checking UUID string or flags
        const char *chr_type = "UNKNOWN";
        if (ctxt->chr.chr_def->flags & BLE_GATT_CHR_F_NOTIFY) {
            chr_type = "TX (notify)";
        } else if (ctxt->chr.chr_def->flags & BLE_GATT_CHR_F_WRITE) {
            chr_type = "RX (write)";
        }
        ESP_LOGI(SOMNUS_BLE_TAG,
                 "[BLE] Registered characteristic %s (%s) handle=%u flags=0x%04lx",
                 uuid_str,
                 chr_type,
                 ctxt->chr.val_handle,
                 (unsigned long)ctxt->chr.chr_def->flags);
        break;
    default:
        ESP_LOGD(SOMNUS_BLE_TAG, "[BLE] GATT register op=%d", ctxt->op);
        break;
    }
}

static int somnus_ble_rx_access_cb(uint16_t conn_handle,
                                   uint16_t attr_handle,
                                   struct ble_gatt_access_ctxt *ctxt,
                                   void *arg)
{
    (void)arg;

    size_t pkt_len = OS_MBUF_PKTLEN(ctxt->om);
    ESP_LOGI(SOMNUS_BLE_TAG, "[BLE RX] conn=%u attr=%u op=%d len=%zu",
             conn_handle, attr_handle, ctxt->op, pkt_len);

    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) {
        ESP_LOGW(SOMNUS_BLE_TAG, "[BLE RX] unexpected op %d", ctxt->op);
        return BLE_ATT_ERR_UNLIKELY;
    }

    somnus_ble_cmd_msg_t msg = {0};
    size_t copy_len = SOMNUS_MIN(pkt_len, SOMNUS_BLE_CMD_MAX_LEN);

    int rc = os_mbuf_copydata(ctxt->om, 0, copy_len, msg.payload);
    if (rc != 0) {
        ESP_LOGE(SOMNUS_BLE_TAG, "[BLE RX] mbuf copydata failed rc=%d", rc);
        return BLE_ATT_ERR_UNLIKELY;
    }

    msg.len = strnlen(msg.payload, SOMNUS_BLE_CMD_MAX_LEN);
    msg.payload[msg.len] = '\0';

    ESP_LOGI(SOMNUS_BLE_TAG, "[BLE RX] payload[%zu]: %.*s", msg.len, (int)msg.len, msg.payload);
    
    // Log RX event
    char log_msg[SOMNUS_BLE_LOG_MSG_MAX_LEN];
    size_t payload_display_len = msg.len > 80 ? 80 : msg.len;
    snprintf(log_msg, sizeof(log_msg), "RX[%zu]: %.*s", msg.len, (int)payload_display_len, msg.payload);
    ble_log_add(BLE_LOG_RX, log_msg);
    
    // Log hex dump for non-printable or long messages
    if (msg.len > 64 || (msg.len > 0 && !strnlen(msg.payload, msg.len))) {
        ESP_LOGI(SOMNUS_BLE_TAG, "[BLE RX] hex[%zu]:", msg.len);
        for (size_t i = 0; i < msg.len && i < 128; i += 16) {
            char hex_line[64] = {0};
            char ascii_line[17] = {0};
            for (size_t j = 0; j < 16 && (i + j) < msg.len; j++) {
                uint8_t byte = (uint8_t)msg.payload[i + j];
                snprintf(hex_line + j * 3, 4, "%02x ", byte);
                ascii_line[j] = (byte >= 32 && byte < 127) ? byte : '.';
            }
            ESP_LOGI(SOMNUS_BLE_TAG, "[BLE RX]   %04zx: %-48s %s", i, hex_line, ascii_line);
        }
    }

    if (!s_cmd_queue) {
        ESP_LOGE(SOMNUS_BLE_TAG, "RX access: command queue not initialized");
        return BLE_ATT_ERR_UNLIKELY;
    }

    UBaseType_t queue_space = uxQueueSpacesAvailable(s_cmd_queue);
    ESP_LOGD(SOMNUS_BLE_TAG, "[BLE RX] queue status: %u spaces available", queue_space);
    
    if (xQueueSend(s_cmd_queue, &msg, 0) != pdTRUE) {
        ESP_LOGW(SOMNUS_BLE_TAG, "[BLE RX] command queue full (spaces=%u), dropping payload", queue_space);
        somnus_ble_notify("Queue busy");
    } else {
        ESP_LOGD(SOMNUS_BLE_TAG, "[BLE RX] queued successfully (remaining spaces=%u)", uxQueueSpacesAvailable(s_cmd_queue));
    }

    return 0;
}

static int
somnus_ble_tx_access_cb(uint16_t conn_handle,
                        uint16_t attr_handle,
                        struct ble_gatt_access_ctxt *ctxt,
                        void *arg)
{
    (void)arg;

    // When ble_gatts_chr_updated is called, conn_handle should be 0xffff (all connections)
    ESP_LOGI(SOMNUS_BLE_TAG, "[BLE TX ACCESS] conn=%u (0xffff=all) attr=%u op=%d", 
             conn_handle, attr_handle, ctxt->op);

    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        // Verify this is for the TX characteristic
        if (attr_handle != s_tx_val_handle) {
            ESP_LOGW(SOMNUS_BLE_TAG, "[BLE TX ACCESS] Wrong attr handle: %u != %u", 
                     attr_handle, s_tx_val_handle);
            return BLE_ATT_ERR_UNLIKELY;
        }
        
        // Return the current TX buffer data for notifications
        portENTER_CRITICAL(&s_state_lock);
        size_t buf_len = s_tx_buffer_len;
        portEXIT_CRITICAL(&s_state_lock);
        
        if (buf_len > 0) {
            ESP_LOGI(SOMNUS_BLE_TAG, "[BLE TX ACCESS] Returning buffer[%zu]: %.*s", 
                     buf_len, (int)(buf_len > 64 ? 64 : buf_len), s_tx_buffer);
            int rc = os_mbuf_copyinto(ctxt->om, 0, s_tx_buffer, buf_len);
            if (rc != 0) {
                ESP_LOGE(SOMNUS_BLE_TAG, "[BLE TX] Failed to copy data to mbuf rc=%d", rc);
                return BLE_ATT_ERR_UNLIKELY;
            }
            ESP_LOGI(SOMNUS_BLE_TAG, "[BLE TX ACCESS] Successfully copied %zu bytes to mbuf", buf_len);
            return 0;
        }
        ESP_LOGW(SOMNUS_BLE_TAG, "[BLE TX ACCESS] Buffer empty, returning error");
        return BLE_ATT_ERR_READ_NOT_PERMITTED;
    }
    ESP_LOGD(SOMNUS_BLE_TAG, "[BLE TX ACCESS] Unhandled op=%d", ctxt->op);
    return BLE_ATT_ERR_UNLIKELY;
}

esp_err_t somnus_ble_notify(const char *message)
{
    if (!message) {
        return ESP_ERR_INVALID_ARG;
    }
    return somnus_ble_send_chunked(message, 0);
}

static esp_err_t somnus_ble_send_chunked(const char *message,
                                         TickType_t inter_chunk_delay)
{
    size_t len = strlen(message);
    if (len == 0) {
        return ESP_OK;
    }

    portENTER_CRITICAL(&s_state_lock);
    uint16_t conn = s_conn_handle;
    bool notify_enabled = s_notify_enabled;
    portEXIT_CRITICAL(&s_state_lock);

    if (!notify_enabled || conn == BLE_HS_CONN_HANDLE_NONE) {
        // This is normal if no client is connected or hasn't subscribed yet
        ESP_LOGW(SOMNUS_BLE_TAG, "[BLE TX] cannot send (no client/subscription): notify=%d conn=%u", 
                 notify_enabled, conn);
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(SOMNUS_BLE_TAG, "[BLE TX] sending message[%zu]: %.*s", len, (int)(len > 128 ? 128 : len), message);
    
    // Log TX event
    char log_msg[SOMNUS_BLE_LOG_MSG_MAX_LEN];
    size_t msg_display_len = len > 80 ? 80 : len;
    snprintf(log_msg, sizeof(log_msg), "TX[%zu]: %.*s", len, (int)msg_display_len, message);
    ble_log_add(BLE_LOG_TX, log_msg);
    
    const uint8_t *data = (const uint8_t *)message;
    size_t total_sent = 0;
    size_t chunk_num = 0;
    
    while (len > 0) {
        size_t chunk_len = len > SOMNUS_BLE_NOTIFY_CHUNK ? SOMNUS_BLE_NOTIFY_CHUNK : len;
        ESP_LOGD(SOMNUS_BLE_TAG, "[BLE TX] chunk[%zu] len=%zu: %.*s", 
                 chunk_num, chunk_len, (int)chunk_len, (const char *)data);
        
        // Store chunk in TX buffer for access callback
        if (chunk_len > sizeof(s_tx_buffer)) {
            ESP_LOGE(SOMNUS_BLE_TAG, "[BLE TX] chunk too large: %zu > %zu", chunk_len, sizeof(s_tx_buffer));
            return ESP_ERR_INVALID_SIZE;
        }
        
        portENTER_CRITICAL(&s_state_lock);
        memcpy(s_tx_buffer, data, chunk_len);
        s_tx_buffer_len = chunk_len;
        portEXIT_CRITICAL(&s_state_lock);

        ESP_LOGI(SOMNUS_BLE_TAG, "[BLE TX] Stored chunk[%zu] len=%zu, sending notification (conn=%u, handle=%u)", 
                 chunk_num, chunk_len, conn, s_tx_val_handle);

        // Allocate mbuf with the chunk data
        // Note: ble_gatts_notify_custom consumes the mbuf on success, so we don't free it
        struct os_mbuf *om = ble_hs_mbuf_from_flat(s_tx_buffer, chunk_len);
        if (om == NULL) {
            ESP_LOGE(SOMNUS_BLE_TAG, "[BLE TX] Failed to allocate mbuf for notification");
            return ESP_ERR_NO_MEM;
        }
        
        // Post notification work to the NimBLE host task
        // GATT operations should be performed in the host task context
        if (s_notify_queue == NULL) {
            ESP_LOGE(SOMNUS_BLE_TAG, "[BLE TX] Notification queue not initialized");
            os_mbuf_free_chain(om);
            return ESP_ERR_INVALID_STATE;
        }
        
        notify_work_t notify_work = {
            .conn_handle = conn,
            .val_handle = s_tx_val_handle,
            .om = om
        };
        
        // Use blocking send with timeout to wait for queue space
        // This prevents dropping notifications when queue is temporarily full
        if (xQueueSend(s_notify_queue, &notify_work, pdMS_TO_TICKS(500)) != pdTRUE) {
            ESP_LOGE(SOMNUS_BLE_TAG, "[BLE TX] Failed to queue notification work (queue full after 500ms wait)");
            os_mbuf_free_chain(om);
            return ESP_ERR_NO_MEM;
        }
        
        ESP_LOGI(SOMNUS_BLE_TAG, "[BLE TX] Notification work queued (chunk %zu, %zu bytes)", 
                 chunk_num, chunk_len);
        
        // Small delay to allow host task to process the notification
        vTaskDelay(pdMS_TO_TICKS(20));

        data += chunk_len;
        len -= chunk_len;
        total_sent += chunk_len;
        chunk_num++;

        if (inter_chunk_delay > 0 && len > 0) {
            vTaskDelay(inter_chunk_delay);
        }
    }
    
    ESP_LOGI(SOMNUS_BLE_TAG, "[BLE TX] sent %zu bytes in %zu chunks", total_sent, chunk_num);
    return ESP_OK;
}

static void somnus_ble_host_task(void *param)
{
    (void)param;
    
    // Process notification queue in host task context
    notify_work_t notify_work;
    while (1) {
        // Process ALL pending notifications before calling nimble_port_run()
        // This ensures notifications are sent promptly even when nimble_port_run() blocks
        int notifications_processed = 0;
        while (s_notify_queue && xQueueReceive(s_notify_queue, &notify_work, 0) == pdTRUE) {
            ESP_LOGI(SOMNUS_BLE_TAG, "[BLE HOST TASK] Processing notification (conn=%u, handle=%u)", 
                     notify_work.conn_handle, notify_work.val_handle);
            
            int rc = ble_gatts_notify_custom(notify_work.conn_handle, notify_work.val_handle, notify_work.om);
            if (rc != 0) {
                ESP_LOGE(SOMNUS_BLE_TAG, "[BLE HOST TASK] ble_gatts_notify_custom failed: %d", rc);
                os_mbuf_free_chain(notify_work.om);
            } else {
                ESP_LOGI(SOMNUS_BLE_TAG, "[BLE HOST TASK] Notification sent successfully");
                // mbuf is consumed by ble_gatts_notify_custom on success
            }
            notifications_processed++;
        }
        
        if (notifications_processed > 0) {
            ESP_LOGD(SOMNUS_BLE_TAG, "[BLE HOST TASK] Processed %d notifications", notifications_processed);
        }
        
        // Run NimBLE port (this may block, but we've already processed all queued notifications)
        nimble_port_run();
    }
    
    nimble_port_freertos_deinit();
}

static esp_err_t somnus_ble_set_advertising_data(void)
{
    struct ble_hs_adv_fields fields;
    memset(&fields, 0, sizeof(fields));
    
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.tx_pwr_lvl_is_present = 0;
    fields.name = (uint8_t *)SOMNUS_BLE_LOCAL_NAME;
    fields.name_len = strlen(SOMNUS_BLE_LOCAL_NAME);
    fields.name_is_complete = 1;
    
    // 128-bit Service UUID (6e400001-b5a3-f393-e0a9-e50e24dcca9e) in little-endian format
    static const ble_uuid128_t service_uuid =
        {.u = {.type = BLE_UUID_TYPE_128},
         .value = {0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0, 0x93, 0xF3, 0xA3, 0xB5, 0x01, 0x00, 0x40, 0x6E}};
    
    fields.uuids128 = &service_uuid;
    fields.num_uuids128 = 1;
    fields.uuids128_is_complete = 1;
    
    // Log the service UUID being advertised
    char uuid_str[BLE_UUID_STR_LEN];
    ble_uuid_to_str(&service_uuid.u, uuid_str);
    ESP_LOGI(SOMNUS_BLE_TAG, "[BLE] Advertising service UUID: %s", uuid_str);
    ESP_LOGI(SOMNUS_BLE_TAG, "[BLE] Advertising name: \"%s\" (len=%zu)", 
             SOMNUS_BLE_LOCAL_NAME, fields.name_len);
    
    // Set advertising fields - this must be called when advertising is stopped
    // Note: Device name is already set via ble_svc_gap_device_name_set, so device will be discoverable
    // even if this fails. The service UUID in advertising data is optional.
    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        // These errors are expected if advertising is active - not a real failure
        if (rc == BLE_HS_EBUSY || rc == BLE_HS_EALREADY) {
            ESP_LOGD(SOMNUS_BLE_TAG, "Advertising fields set deferred (advertising active, rc=%d)", rc);
            return ESP_OK;  // Not an error - will be set when advertising stops
        }
        // EINVAL (4) means invalid parameters or stack not ready
        // This is not critical - device name is already set, so device will still be discoverable
        ESP_LOGD(SOMNUS_BLE_TAG, "ble_gap_adv_set_fields failed rc=%d (will use device name only)", rc);
        return ESP_OK;  // Not a fatal error - advertising will work with device name
    }
    
    ESP_LOGI(SOMNUS_BLE_TAG, "[BLE] Advertising fields set successfully");
    return ESP_OK;
}

static void somnus_ble_start_advertising(void)
{
    // Don't advertise if already connected
    portENTER_CRITICAL(&s_state_lock);
    bool already_connected = (s_conn_handle != BLE_HS_CONN_HANDLE_NONE);
    portEXIT_CRITICAL(&s_state_lock);
    
    if (already_connected) {
        ESP_LOGI(SOMNUS_BLE_TAG, "Skipping advertising restart - already connected (handle=%u)", s_conn_handle);
        return;
    }
    
    // Stop any existing advertising first
    int stop_rc = ble_gap_adv_stop();
    if (stop_rc != 0 && stop_rc != BLE_HS_EALREADY) {
        ESP_LOGD(SOMNUS_BLE_TAG, "ble_gap_adv_stop returned rc=%d", stop_rc);
    }
    
    // Wait a bit for stop to complete (if it was running)
    // Use a small delay to let the stack process the stop command
    vTaskDelay(pdMS_TO_TICKS(50));
    
    // Try to set advertising fields (optional - device name is already set via ble_svc_gap_device_name_set)
    // If this fails, advertising will still work with the device name
    esp_err_t fields_set = somnus_ble_set_advertising_data();
    if (fields_set != ESP_OK) {
        // Not critical - device name is already set, so device will still be discoverable
        ESP_LOGD(SOMNUS_BLE_TAG, "Advertising fields not set (will use device name only)");
    }
    
    // Start advertising with proper parameters
    struct ble_gap_adv_params adv_params;
    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;  // Undirected connectable - allows any device to connect
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;  // General discoverable

    ESP_LOGI(SOMNUS_BLE_TAG, "Starting BLE advertising: name=\"%s\" conn_mode=UND (connectable)", SOMNUS_BLE_LOCAL_NAME);
    int rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC,
                                NULL,
                                BLE_HS_FOREVER,
                                &adv_params,
                                somnus_ble_gap_event,
                                NULL);
    if (rc != 0) {
        if (rc == BLE_HS_EALREADY) {
            ESP_LOGD(SOMNUS_BLE_TAG, "Advertising already active");
        } else {
            ESP_LOGE(SOMNUS_BLE_TAG, "ble_gap_adv_start failed rc=%d", rc);
        }
    } else {
        ESP_LOGI(SOMNUS_BLE_TAG,
                 "Advertising as \"%s\" (connectable, waiting for connections)",
                 SOMNUS_BLE_LOCAL_NAME);
    }
}

static void somnus_ble_on_sync(void)
{
    uint8_t addr_val[6] = {0};
    int rc = ble_hs_id_infer_auto(0, &addr_val[0]);
    
    if (rc != 0) {
        ESP_LOGE(SOMNUS_BLE_TAG, "ble_hs_id_infer_auto failed rc=%d", rc);
        return;
    }
    
    rc = ble_hs_id_copy_addr(BLE_ADDR_PUBLIC, addr_val, NULL);
    if (rc != 0) {
        ESP_LOGE(SOMNUS_BLE_TAG, "ble_hs_id_copy_addr failed rc=%d", rc);
        return;
    }
    
    // Don't set advertising fields here - wait until we actually start advertising
    // The stack might not be fully ready yet in the sync callback
    ESP_LOGI(SOMNUS_BLE_TAG, "BLE stack synchronized, ready to start advertising");
    somnus_ble_start_advertising();
}

static int somnus_ble_gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        ESP_LOGI(SOMNUS_BLE_TAG, "[BLE] GAP connect: handle=%u status=%d",
                 event->connect.conn_handle, event->connect.status);
        if (event->connect.status == 0) {
            ESP_LOGI(SOMNUS_BLE_TAG, "[BLE] Client connected handle=%u", event->connect.conn_handle);
            portENTER_CRITICAL(&s_state_lock);
            s_conn_handle = event->connect.conn_handle;
            portEXIT_CRITICAL(&s_state_lock);
            ESP_LOGI(SOMNUS_BLE_TAG, "[BLE] Connection established - ready for RX/TX");
            char log_msg[128];
            snprintf(log_msg, sizeof(log_msg), "Client connected (handle=%u)", event->connect.conn_handle);
            ble_log_add(BLE_LOG_CONNECT, log_msg);
        } else {
            ESP_LOGW(SOMNUS_BLE_TAG, "[BLE] Connection failed status=%d (0=success, non-zero=error)", event->connect.status);
            char log_msg[128];
            snprintf(log_msg, sizeof(log_msg), "Connection failed (status=%d)", event->connect.status);
            ble_log_add(BLE_LOG_DISCONNECT, log_msg);
            somnus_ble_start_advertising();
        }
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(SOMNUS_BLE_TAG,
                 "[BLE] Client disconnected handle=%u reason=%d",
                 event->disconnect.conn.conn_handle,
                 event->disconnect.reason);
        portENTER_CRITICAL(&s_state_lock);
        s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        s_notify_enabled = false;
        portEXIT_CRITICAL(&s_state_lock);
        ESP_LOGI(SOMNUS_BLE_TAG, "[BLE] Connection closed - restarting advertising");
        char log_msg[128];
        snprintf(log_msg, sizeof(log_msg), "Client disconnected (handle=%u, reason=%d)", 
                 event->disconnect.conn.conn_handle, event->disconnect.reason);
        ble_log_add(BLE_LOG_DISCONNECT, log_msg);
        somnus_ble_start_advertising();
        break;
    case BLE_GAP_EVENT_CONN_UPDATE:
        ESP_LOGI(SOMNUS_BLE_TAG, "Connection update: handle=%u status=%d",
                 event->conn_update.conn_handle, event->conn_update.status);
        break;
    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(SOMNUS_BLE_TAG, "MTU update: handle=%u mtu=%u",
                 event->mtu.conn_handle, event->mtu.value);
        break;
    case BLE_GAP_EVENT_ADV_COMPLETE:
        ESP_LOGI(SOMNUS_BLE_TAG, "Advertisement complete status=%d (0=timeout/complete, non-zero=error)", event->adv_complete.reason);
        // Check if we're connected before restarting
        portENTER_CRITICAL(&s_state_lock);
        bool is_connected = (s_conn_handle != BLE_HS_CONN_HANDLE_NONE);
        portEXIT_CRITICAL(&s_state_lock);
        
        if (is_connected) {
            ESP_LOGI(SOMNUS_BLE_TAG, "Connection established - advertising stopped (normal, not restarting)");
            return 0;  // Don't restart advertising if connected
        }
        ESP_LOGI(SOMNUS_BLE_TAG, "Restarting advertising...");
        somnus_ble_start_advertising();
        break;
    case BLE_GAP_EVENT_SUBSCRIBE:
        ESP_LOGI(SOMNUS_BLE_TAG, "[BLE] Subscribe: handle=%u attr=%u notify=%d indicate=%d",
                 event->subscribe.conn_handle, event->subscribe.attr_handle,
                 event->subscribe.cur_notify, event->subscribe.cur_indicate);
        if (event->subscribe.attr_handle == s_tx_val_handle) {
            portENTER_CRITICAL(&s_state_lock);
            s_notify_enabled = event->subscribe.cur_notify;
            portEXIT_CRITICAL(&s_state_lock);
            ESP_LOGI(SOMNUS_BLE_TAG, "[BLE TX] Notifications %s (can now send messages to client)",
                     s_notify_enabled ? "enabled" : "disabled");
            char log_msg[128];
            snprintf(log_msg, sizeof(log_msg), "Notifications %s", s_notify_enabled ? "enabled" : "disabled");
            ble_log_add(BLE_LOG_SUBSCRIBE, log_msg);
        } else {
            ESP_LOGD(SOMNUS_BLE_TAG, "[BLE] Subscribe event for non-TX characteristic (handle=%u)", event->subscribe.attr_handle);
        }
        break;
    default:
        ESP_LOGD(SOMNUS_BLE_TAG, "GAP event: type=%d", event->type);
        break;
    }
    return 0;
}

static void somnus_ble_command_task(void *param)
{
    (void)param;
    somnus_ble_cmd_msg_t msg;
    ESP_LOGI(SOMNUS_BLE_TAG, "[BLE] Command task started - waiting for messages");
    while (xQueueReceive(s_cmd_queue, &msg, portMAX_DELAY) == pdTRUE) {
        ESP_LOGI(SOMNUS_BLE_TAG, "[BLE] Processing command[%zu]: %.*s", msg.len, (int)msg.len, msg.payload);
        somnus_ble_handle_command(msg.payload);
        ESP_LOGD(SOMNUS_BLE_TAG, "[BLE] Command processing complete");
    }
}

static void somnus_ble_handle_command(const char *payload)
{
    if (!payload || payload[0] == '\0') {
        ESP_LOGW(SOMNUS_BLE_TAG, "[BLE] Empty command payload");
        return;
    }

    ESP_LOGD(SOMNUS_BLE_TAG, "[BLE] Parsing JSON command: %s", payload);
    cJSON *root = cJSON_Parse(payload);
    if (!root) {
        ESP_LOGW(SOMNUS_BLE_TAG, "[BLE] JSON parse failed for payload: %s", payload);
        somnus_ble_notify("Bad JSON format");
        return;
    }

    // Check for device command format (Action with capital A, or array of actions)
    const cJSON *action_capital = cJSON_GetObjectItemCaseSensitive(root, "Action");
    bool is_device_command = (action_capital != NULL && cJSON_IsString(action_capital)) || cJSON_IsArray(root);
    
    if (is_device_command) {
        // This is a device command (LED, SongChange, SetVolume, etc.)
        ESP_LOGI(SOMNUS_BLE_TAG, "[BLE] Device command detected, routing to action handler");
        if (s_device_command_cb) {
            esp_err_t err = s_device_command_cb(payload, s_device_command_ctx);
            if (err == ESP_OK) {
                somnus_ble_notify("Command executed");
            } else {
                char err_msg[64];
                snprintf(err_msg, sizeof(err_msg), "Command failed: %s", esp_err_to_name(err));
                somnus_ble_notify(err_msg);
            }
        } else {
            ESP_LOGW(SOMNUS_BLE_TAG, "[BLE] Device command received but no handler registered");
            somnus_ble_notify("Device command handler not available");
        }
        cJSON_Delete(root);
        return;
    }

    // Check for BLE-specific actions (lowercase "action" field)
    const cJSON *action = cJSON_GetObjectItemCaseSensitive(root, "action");
    if (!cJSON_IsString(action) || action->valuestring == NULL) {
        ESP_LOGW(SOMNUS_BLE_TAG, "[BLE] Missing or invalid 'action' field");
        somnus_ble_notify("Unknown action");
        cJSON_Delete(root);
        return;
    }

    ESP_LOGI(SOMNUS_BLE_TAG, "[BLE] Executing BLE action: %s", action->valuestring);
    if (strcasecmp(action->valuestring, "SCAN") == 0) {
        ESP_LOGI(SOMNUS_BLE_TAG, "[BLE] Handling SCAN action");
        somnus_ble_handle_scan_action();
    } else if (strcasecmp(action->valuestring, "CONNECT_WIFI") == 0) {
        ESP_LOGI(SOMNUS_BLE_TAG, "[BLE] Handling CONNECT_WIFI action");
        somnus_ble_handle_connect_action(root);
    } else if (strcasecmp(action->valuestring, "READ_SENSORS") == 0) {
        ESP_LOGI(SOMNUS_BLE_TAG, "[BLE] Handling READ_SENSORS action");
        somnus_ble_handle_read_sensors_action();
    } else {
        ESP_LOGW(SOMNUS_BLE_TAG, "[BLE] Unknown action: %s", action->valuestring);
        somnus_ble_notify("Unknown action");
    }

    cJSON_Delete(root);
}

static void somnus_ble_handle_read_sensors_action(void)
{
#ifdef CONFIG_SENSOR_MANAGER_ENABLED
    ESP_LOGI(SOMNUS_BLE_TAG, "[BLE] Reading sensor data");
    
    sensor_integration_data_t sensor_data = sensor_integration_get_data();
    
    cJSON *root = cJSON_CreateObject();
    cJSON *sensors = cJSON_CreateObject();
    
    // Add device ID if available
    // cJSON_AddStringToObject(root, "deviceId", "SOMNUS_XXX");
    cJSON_AddNumberToObject(root, "timestamp_ms", (double)(esp_timer_get_time() / 1000));
    
    // SHT45 data
    if (sensor_data.sht45_available) {
        cJSON *sht45 = cJSON_CreateObject();
        cJSON_AddNumberToObject(sht45, "temperature_c", sensor_data.temperature_c);
        cJSON_AddNumberToObject(sht45, "humidity_rh", sensor_data.humidity_rh);
        cJSON_AddBoolToObject(sht45, "synthetic", false);
        cJSON_AddItemToObject(sensors, "sht45", sht45);
    }
    
    // SGP40 data
    if (sensor_data.sgp40_available) {
        cJSON *sgp40 = cJSON_CreateObject();
        cJSON_AddNumberToObject(sgp40, "voc_index", sensor_data.voc_index);
        cJSON_AddNumberToObject(sgp40, "voc_ticks", sensor_data.voc_index * 10); // Approximate
        cJSON_AddBoolToObject(sgp40, "synthetic", false);
        cJSON_AddItemToObject(sensors, "sgp40", sgp40);
    }
    
    // SCD40 data
    if (sensor_data.scd40_available) {
        cJSON *scd40 = cJSON_CreateObject();
        cJSON_AddNumberToObject(scd40, "co2_ppm", sensor_data.co2_ppm);
        cJSON_AddNumberToObject(scd40, "temperature_c", sensor_data.temperature_co2_c);
        cJSON_AddNumberToObject(scd40, "humidity_rh", sensor_data.humidity_co2_rh);
        cJSON_AddBoolToObject(scd40, "synthetic", false);
        cJSON_AddItemToObject(sensors, "scd40", scd40);
    }
    
    // VCNL4040 data
    if (sensor_data.vcnl4040_available) {
        cJSON *vcnl4040 = cJSON_CreateObject();
        cJSON_AddNumberToObject(vcnl4040, "ambient_lux", sensor_data.ambient_lux);
        cJSON_AddNumberToObject(vcnl4040, "proximity", sensor_data.proximity);
        cJSON_AddBoolToObject(vcnl4040, "synthetic", false);
        cJSON_AddItemToObject(sensors, "vcnl4040", vcnl4040);
    }
    
    // EC10 data (PM sensor)
    if (sensor_data.ec10_available) {
        cJSON *ec10 = cJSON_CreateObject();
        // EC10 stores PM2.5 in ec_ms_per_cm field
        cJSON_AddNumberToObject(ec10, "pm2_5_ug_m3", sensor_data.ec_ms_per_cm);
        cJSON_AddNumberToObject(ec10, "pm1_0_ug_m3", sensor_data.ec_ms_per_cm * 0.5); // Estimate
        cJSON_AddNumberToObject(ec10, "pm10_ug_m3", sensor_data.ec_ms_per_cm * 1.5); // Estimate
        cJSON_AddBoolToObject(ec10, "synthetic", false);
        cJSON_AddItemToObject(sensors, "ec10", ec10);
    }
    
    cJSON_AddItemToObject(root, "sensors", sensors);
    
    char *json = cJSON_PrintUnformatted(root);
    if (json) {
        ESP_LOGI(SOMNUS_BLE_TAG, "[BLE] Sending sensor data: %s", json);
        somnus_ble_notify("SENSOR_DATA_START");
        somnus_ble_send_chunked(json, pdMS_TO_TICKS(50));
        somnus_ble_notify("SENSOR_DATA_END");
        free(json);
    } else {
        ESP_LOGE(SOMNUS_BLE_TAG, "[BLE] Failed to serialize sensor data");
        somnus_ble_notify("SENSOR_DATA_ERROR");
    }
    
    cJSON_Delete(root);
#else
    ESP_LOGW(SOMNUS_BLE_TAG, "[BLE] Sensor manager not enabled, cannot read sensors");
    somnus_ble_notify("SENSOR_DATA_ERROR: Sensor manager not available");
#endif
}

static void somnus_ble_handle_scan_action(void)
{
    ESP_LOGI(SOMNUS_BLE_TAG, "[BLE] Handling WiFi scan request");
    
    // Always send WIFI_LIST_START to indicate scan beginning
    esp_err_t notify_err = somnus_ble_notify("WIFI_LIST_START");
    if (notify_err != ESP_OK) {
        ESP_LOGW(SOMNUS_BLE_TAG, "[BLE] Failed to send WIFI_LIST_START: %s", esp_err_to_name(notify_err));
    }
    
    // Small delay to ensure notification is sent before starting scan
    vTaskDelay(pdMS_TO_TICKS(100));

    char *json = somnus_ble_perform_wifi_scan(NULL);
    if (!json) {
        ESP_LOGE(SOMNUS_BLE_TAG, "[BLE] WiFi scan failed - memory allocation error");
        somnus_ble_notify("WIFI_LIST_ERROR");
        somnus_ble_notify("WIFI_LIST_END");
        return;
    }

    // Check if scan returned empty list (WiFi not initialized or no networks found)
    size_t json_len = strlen(json);
    bool is_empty = (json_len <= 2 && strcmp(json, "[]") == 0);
    
    if (is_empty) {
        ESP_LOGI(SOMNUS_BLE_TAG, "[BLE] WiFi scan returned empty list (WiFi may not be initialized or no networks found)");
    } else {
        ESP_LOGI(SOMNUS_BLE_TAG, "[BLE] WiFi scan complete, sending %zu bytes", json_len);
    }
    
    esp_err_t send_err = somnus_ble_send_chunked(json, pdMS_TO_TICKS(50));
    if (send_err != ESP_OK) {
        ESP_LOGW(SOMNUS_BLE_TAG, "[BLE] Failed to send WiFi scan results: %s", esp_err_to_name(send_err));
        // Still send END marker even if chunked send failed
    }
    free(json);

    // Always send WIFI_LIST_END to indicate scan completion
    notify_err = somnus_ble_notify("WIFI_LIST_END");
    if (notify_err != ESP_OK) {
        ESP_LOGW(SOMNUS_BLE_TAG, "[BLE] Failed to send WIFI_LIST_END: %s", esp_err_to_name(notify_err));
    }
    
    ESP_LOGI(SOMNUS_BLE_TAG, "[BLE] WiFi scan response complete");
}

static void somnus_ble_handle_connect_action(const cJSON *root)
{
    const cJSON *ssid = cJSON_GetObjectItemCaseSensitive(root, "ssid");
    const cJSON *password = cJSON_GetObjectItemCaseSensitive(root, "password");
    const cJSON *token = cJSON_GetObjectItemCaseSensitive(root, "user_token");
    const cJSON *is_prod = cJSON_GetObjectItemCaseSensitive(root, "is_production");

    if (!cJSON_IsString(ssid) || !cJSON_IsString(password) || !cJSON_IsString(token)) {
        somnus_ble_notify("Missing ssid/password/token");
        return;
    }

    bool is_production = false;
    if (cJSON_IsBool(is_prod)) {
        is_production = cJSON_IsTrue(is_prod);
    } else if (cJSON_IsString(is_prod)) {
        is_production = strcasecmp(is_prod->valuestring, "true") == 0 ||
                        strcmp(is_prod->valuestring, "1") == 0;
    }

    char connecting_msg[96];
    snprintf(connecting_msg, sizeof(connecting_msg), "Connecting to %s...", ssid->valuestring);
    somnus_ble_notify(connecting_msg);

    bool success = false;
    if (s_connect_cb) {
        success = s_connect_cb(ssid->valuestring,
                               password->valuestring,
                               token->valuestring,
                               is_production,
                               s_connect_ctx);
    } else {
        ESP_LOGW(SOMNUS_BLE_TAG, "Connect callback not set");
    }

    if (success) {
        char connected_msg[96];
        snprintf(connected_msg, sizeof(connected_msg), "Connected to %s", ssid->valuestring);
        somnus_ble_notify(connected_msg);
    } else {
        somnus_ble_notify("Wi-Fi connection failed");
    }
}

static char *somnus_ble_perform_wifi_scan(size_t *out_len)
{
    // Check if WiFi is initialized and in a mode that supports scanning
    wifi_mode_t mode;
    esp_err_t err = esp_wifi_get_mode(&mode);
    bool wifi_was_initialized = (err == ESP_OK);
    
    // If WiFi is not initialized, try to initialize it for scanning
    if (err != ESP_OK || mode == WIFI_MODE_NULL) {
        ESP_LOGI(SOMNUS_BLE_TAG, "[BLE] WiFi not initialized, initializing for scan (err=%s)", 
                 esp_err_to_name(err));
        
        // CRITICAL: Do NOT call esp_netif_create_default_wifi_sta() here!
        // It uses ESP_ERROR_CHECK internally and will ABORT if handlers are already set.
        // The netif will be created automatically by esp_wifi_init()/esp_wifi_start() if needed.
        // If it already exists, those functions will handle it gracefully.
        ESP_LOGI(SOMNUS_BLE_TAG, "[BLE] Skipping explicit netif creation (WiFi init will handle it)");
        
        // Check if WiFi is already started - if so, we can skip initialization
        // We check both mode and if WiFi is actually running
        wifi_mode_t current_mode;
        esp_err_t mode_err = esp_wifi_get_mode(&current_mode);
        bool wifi_mode_set = (mode_err == ESP_OK && current_mode != WIFI_MODE_NULL);
        
        // Try to get WiFi status - if this succeeds, WiFi is likely already started
        // Note: There's no direct API to check if WiFi is started, so we check mode
        // and handle ESP_ERR_INVALID_STATE from esp_wifi_start() gracefully
        bool wifi_already_started = wifi_mode_set;
        
        if (!wifi_already_started) {
            // Initialize netif subsystem (safe to call multiple times)
            esp_err_t netif_err = esp_netif_init();
            if (netif_err != ESP_OK && netif_err != ESP_ERR_INVALID_STATE) {
                ESP_LOGW(SOMNUS_BLE_TAG, "[BLE] esp_netif_init returned: %s (continuing)", esp_err_to_name(netif_err));
            }
            
            // Try to initialize WiFi - this is safe even if wifi_manager will initialize it later
            // because esp_wifi_init checks if already initialized
            wifi_init_config_t wifi_cfg = WIFI_INIT_CONFIG_DEFAULT();
            err = esp_wifi_init(&wifi_cfg);
            if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
                ESP_LOGE(SOMNUS_BLE_TAG, "[BLE] Failed to initialize WiFi for scan: %s", esp_err_to_name(err));
                char *empty = strdup("[]");
                if (out_len) {
                    *out_len = 2;
                }
                return empty;
            }
            
            // Set WiFi to STA mode for scanning
            err = esp_wifi_set_mode(WIFI_MODE_STA);
            if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
                ESP_LOGE(SOMNUS_BLE_TAG, "[BLE] Failed to set WiFi mode to STA: %s", esp_err_to_name(err));
                char *empty = strdup("[]");
                if (out_len) {
                    *out_len = 2;
                }
                return empty;
            }
            
            // Start WiFi - required for scanning
            // NOTE: esp_wifi_start() internally calls esp_netif_create_default_wifi_sta() if netif doesn't exist.
            // If netif already exists (from previous init), it should handle it gracefully.
            // However, if handlers are already set, it will abort. This is an ESP-IDF limitation.
            err = esp_wifi_start();
            if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
                ESP_LOGE(SOMNUS_BLE_TAG, "[BLE] Failed to start WiFi for scan: %s", esp_err_to_name(err));
                char *empty = strdup("[]");
                if (out_len) {
                    *out_len = 2;
                }
                return empty;
            }
        } else {
            ESP_LOGI(SOMNUS_BLE_TAG, "[BLE] WiFi already started, skipping initialization");
        }
        
        // Small delay to allow WiFi to start
        vTaskDelay(pdMS_TO_TICKS(100));
        
        // Verify mode after initialization
        err = esp_wifi_get_mode(&mode);
        if (err != ESP_OK || (mode != WIFI_MODE_STA && mode != WIFI_MODE_APSTA)) {
            ESP_LOGW(SOMNUS_BLE_TAG, "[BLE] WiFi mode not suitable for scanning after init: %d", mode);
            char *empty = strdup("[]");
            if (out_len) {
                *out_len = 2;
            }
            return empty;
        }
        
        ESP_LOGI(SOMNUS_BLE_TAG, "[BLE] WiFi initialized in STA mode for scanning");
    }
    
    // WiFi must be in STA or APSTA mode to scan
    if (mode != WIFI_MODE_STA && mode != WIFI_MODE_APSTA) {
        ESP_LOGW(SOMNUS_BLE_TAG, "[BLE] WiFi mode (%d) does not support scanning, need STA or APSTA", mode);
        char *empty = strdup("[]");
        if (out_len) {
            *out_len = 2;
        }
        return empty;
    }
    
    ESP_LOGI(SOMNUS_BLE_TAG, "[BLE] Starting WiFi scan (mode=%d)", mode);

    wifi_scan_config_t scan_cfg = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = true,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time.active = {
            .min = 100,
            .max = 300,
        },
    };

    // Start WiFi scan (blocking call - waits for scan to complete)
    err = esp_wifi_scan_start(&scan_cfg, true);
    if (err != ESP_OK) {
        ESP_LOGE(SOMNUS_BLE_TAG, "[BLE] Wi-Fi scan start failed: %s", esp_err_to_name(err));
        // Return empty list instead of NULL for graceful handling
        char *empty = strdup("[]");
        if (out_len) {
            *out_len = 2;
        }
        return empty;
    }
    
    ESP_LOGI(SOMNUS_BLE_TAG, "[BLE] WiFi scan completed, retrieving results");

    uint16_t ap_num = 0;
    err = esp_wifi_scan_get_ap_num(&ap_num);
    if (err != ESP_OK) {
        ESP_LOGW(SOMNUS_BLE_TAG, "Wi-Fi get ap num failed (%s)", esp_err_to_name(err));
        // Return empty list instead of NULL for graceful handling
        char *empty = strdup("[]");
        if (out_len) {
            *out_len = 2;
        }
        return empty;
    }

    if (ap_num > SOMNUS_WIFI_SCAN_MAX_AP) {
        ap_num = SOMNUS_WIFI_SCAN_MAX_AP;
    }
    if (ap_num == 0) {
        char *empty = strdup("[]");
        if (out_len) {
            *out_len = 2;
        }
        return empty;
    }

    wifi_ap_record_t *ap_records = calloc(ap_num, sizeof(wifi_ap_record_t));
    if (!ap_records) {
        return NULL;
    }

    err = esp_wifi_scan_get_ap_records(&ap_num, ap_records);
    if (err != ESP_OK) {
        ESP_LOGW(SOMNUS_BLE_TAG, "Wi-Fi get ap records failed (%s)", esp_err_to_name(err));
        free(ap_records);
        // Return empty list instead of NULL for graceful handling
        char *empty = strdup("[]");
        if (out_len) {
            *out_len = 2;
        }
        return empty;
    }

    cJSON *array = cJSON_CreateArray();
    if (!array) {
        free(ap_records);
        return NULL;
    }

    for (uint16_t i = 0; i < ap_num; ++i) {
        wifi_ap_record_t *rec = &ap_records[i];
        if (rec->ssid[0] == '\0') {
            continue;
        }

        cJSON *obj = cJSON_CreateObject();
        if (!obj) {
            continue;
        }

        cJSON_AddStringToObject(obj, "ssid", (const char *)rec->ssid);

        char mac_buf[18];
        snprintf(mac_buf, sizeof(mac_buf),
                 "%02X:%02X:%02X:%02X:%02X:%02X",
                 rec->bssid[0],
                 rec->bssid[1],
                 rec->bssid[2],
                 rec->bssid[3],
                 rec->bssid[4],
                 rec->bssid[5]);
        cJSON_AddStringToObject(obj, "mac", mac_buf);

        // Add RSSI (signal strength)
        cJSON_AddNumberToObject(obj, "rssi", rec->rssi);

        // Add auth mode (security type)
        const char *auth_str = "Unknown";
        switch (rec->authmode) {
        case WIFI_AUTH_OPEN:
            auth_str = "Open";
            break;
        case WIFI_AUTH_WEP:
            auth_str = "WEP";
            break;
        case WIFI_AUTH_WPA_PSK:
            auth_str = "WPA";
            break;
        case WIFI_AUTH_WPA2_PSK:
            auth_str = "WPA2";
            break;
        case WIFI_AUTH_WPA_WPA2_PSK:
            auth_str = "WPA/WPA2";
            break;
        case WIFI_AUTH_WPA2_ENTERPRISE:
            auth_str = "WPA2-Enterprise";
            break;
        case WIFI_AUTH_WPA3_PSK:
            auth_str = "WPA3";
            break;
        case WIFI_AUTH_WPA2_WPA3_PSK:
            auth_str = "WPA2/WPA3";
            break;
        case WIFI_AUTH_WAPI_PSK:
            auth_str = "WAPI";
            break;
        default:
            auth_str = "Unknown";
            break;
        }
        cJSON_AddStringToObject(obj, "auth", auth_str);

        cJSON_AddItemToArray(array, obj);
    }

    char *json = cJSON_PrintUnformatted(array);
    if (out_len && json) {
        *out_len = strlen(json);
    }

    cJSON_Delete(array);
    free(ap_records);
    return json;
}

esp_err_t somnus_ble_start(const somnus_ble_config_t *config)
{
    if (s_started) {
        ESP_LOGW(SOMNUS_BLE_TAG, "somnus_ble_start called while already running");
        return ESP_OK;
    }

    // Initialize BLE log buffer
    ble_log_init();
    ble_log_add(BLE_LOG_CONNECT, "BLE service starting");

    if (config) {
        s_connect_cb = config->connect_cb;
        s_connect_ctx = config->connect_ctx;
        s_device_command_cb = config->device_command_cb;
        s_device_command_ctx = config->device_command_ctx;
    } else {
        s_connect_cb = NULL;
        s_connect_ctx = NULL;
        s_device_command_cb = NULL;
        s_device_command_ctx = NULL;
    }

    ESP_LOGI(SOMNUS_BLE_TAG, "Initialising NimBLE controller + host");
    esp_err_t err = esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(SOMNUS_BLE_TAG, "Failed to release Classic BT memory (%s)", esp_err_to_name(err));
        return err;
    }

    // Initialize NimBLE port (matches ESP-IDF reference examples)
    // Note: nimble_port_init() handles controller initialization internally
    // Reference examples do NOT call esp_nimble_hci_and_controller_init() separately
    err = nimble_port_init();
    if (err != ESP_OK) {
        ESP_LOGE(SOMNUS_BLE_TAG, "Failed to init nimble: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(SOMNUS_BLE_TAG, "nimble_port_init complete");

    ble_hs_cfg.sync_cb = somnus_ble_on_sync;
    ble_hs_cfg.gatts_register_cb = somnus_ble_gatt_register_cb;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    // ble_store_ram_init(); // Not needed - store is initialized automatically

    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_svc_gap_device_name_set(SOMNUS_BLE_LOCAL_NAME);
    ESP_LOGI(SOMNUS_BLE_TAG, "Gap/Gatt services configured");

    int rc = ble_gatts_count_cfg(somnus_svc_defs);
    if (rc != 0) {
        nimble_port_deinit();
        ESP_LOGE(SOMNUS_BLE_TAG, "ble_gatts_count_cfg failed rc=%d", rc);
        return ESP_FAIL;
    }
    rc = ble_gatts_add_svcs(somnus_svc_defs);
    if (rc != 0) {
        nimble_port_deinit();
        ESP_LOGE(SOMNUS_BLE_TAG, "ble_gatts_add_svcs failed rc=%d", rc);
        return ESP_FAIL;
    }
    ESP_LOGI(SOMNUS_BLE_TAG, "GATT services registered");

    // BLE store is configured via ble_hs_cfg.store_status_cb (already set above)
    // Reference examples call ble_store_config_init() which sets up store callbacks,
    // but we're using the default store_status_cb which should be sufficient

    s_cmd_queue = xQueueCreate(SOMNUS_BLE_QUEUE_LENGTH, sizeof(somnus_ble_cmd_msg_t));
    if (!s_cmd_queue) {
        nimble_port_deinit();
        ESP_LOGE(SOMNUS_BLE_TAG, "Failed to create command queue");
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(SOMNUS_BLE_TAG, "Command queue created");

    if (xTaskCreate(somnus_ble_command_task,
                    "somnus_ble_cmd",
                    SOMNUS_BLE_TASK_STACK,
                    NULL,
                    SOMNUS_BLE_TASK_PRIO,
                    &s_cmd_task) != pdPASS) {
        vQueueDelete(s_cmd_queue);
        s_cmd_queue = NULL;
        nimble_port_deinit();
        ESP_LOGE(SOMNUS_BLE_TAG, "Failed to create command task");
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(SOMNUS_BLE_TAG, "Command task running");

    // Create notification queue for host task
    // Increased size to handle chunked messages (461 bytes = ~24 chunks of 20 bytes)
    // With some headroom for multiple concurrent notifications
    s_notify_queue = xQueueCreate(50, sizeof(notify_work_t));
    if (!s_notify_queue) {
        vTaskDelete(s_cmd_task);
        s_cmd_task = NULL;
        vQueueDelete(s_cmd_queue);
        s_cmd_queue = NULL;
        nimble_port_deinit();
        ESP_LOGE(SOMNUS_BLE_TAG, "Failed to create notification queue");
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(SOMNUS_BLE_TAG, "Notification queue created");

    // Start FreeRTOS host task (must be last, matches reference examples)
    nimble_port_freertos_init(somnus_ble_host_task);
    // nimble_port_freertos_init returns void, so no error check needed
    ESP_LOGI(SOMNUS_BLE_TAG, "FreeRTOS host task launched");

    s_started = true;
    ESP_LOGI(SOMNUS_BLE_TAG, "Somnus BLE service started");
    return ESP_OK;
}

esp_err_t somnus_ble_stop(void)
{
    if (!s_started) {
        return ESP_OK;
    }

    int rc = ble_gap_adv_stop();
    if (rc != 0 && rc != BLE_HS_EALREADY) {
        ESP_LOGW(SOMNUS_BLE_TAG, "Failed to stop advertising rc=%d", rc);
    }

    nimble_port_stop();
    nimble_port_freertos_deinit();
    nimble_port_deinit();

    if (s_cmd_task) {
        vTaskDelete(s_cmd_task);
        s_cmd_task = NULL;
    }
    if (s_cmd_queue) {
        vQueueDelete(s_cmd_queue);
        s_cmd_queue = NULL;
    }
    if (s_notify_queue) {
        vQueueDelete(s_notify_queue);
        s_notify_queue = NULL;
    }

    portENTER_CRITICAL(&s_state_lock);
    s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
    s_notify_enabled = false;
    portEXIT_CRITICAL(&s_state_lock);

    s_started = false;
    ESP_LOGI(SOMNUS_BLE_TAG, "Somnus BLE service stopped");
    return ESP_OK;
}

esp_err_t somnus_ble_get_logs(char **out_json, size_t max_entries)
{
    if (!out_json) {
        return ESP_ERR_INVALID_ARG;
    }
    
    cJSON *json = cJSON_CreateObject();
    cJSON *logs_array = cJSON_CreateArray();
    
    portENTER_CRITICAL(&s_ble_log.lock);
    
    size_t count = s_ble_log.count;
    if (max_entries > 0 && count > max_entries) {
        count = max_entries;
    }
    
    // Get entries from oldest to newest
    for (size_t i = 0; i < count; i++) {
        size_t idx = (s_ble_log.head + i) % SOMNUS_BLE_LOG_MAX_ENTRIES;
        ble_log_entry_t *entry = &s_ble_log.entries[idx];
        
        cJSON *log_obj = cJSON_CreateObject();
        
        const char *type_str = "UNKNOWN";
        switch (entry->type) {
        case BLE_LOG_CONNECT:
            type_str = "CONNECT";
            break;
        case BLE_LOG_DISCONNECT:
            type_str = "DISCONNECT";
            break;
        case BLE_LOG_RX:
            type_str = "RX";
            break;
        case BLE_LOG_TX:
            type_str = "TX";
            break;
        case BLE_LOG_SUBSCRIBE:
            type_str = "SUBSCRIBE";
            break;
        }
        
        cJSON_AddStringToObject(log_obj, "type", type_str);
        cJSON_AddNumberToObject(log_obj, "timestamp_ms", entry->timestamp_ms);
        cJSON_AddStringToObject(log_obj, "message", entry->message);
        
        cJSON_AddItemToArray(logs_array, log_obj);
    }
    
    portEXIT_CRITICAL(&s_ble_log.lock);
    
    cJSON_AddItemToObject(json, "logs", logs_array);
    cJSON_AddNumberToObject(json, "total_count", s_ble_log.count);
    
    char *json_str = cJSON_Print(json);
    cJSON_Delete(json);
    
    if (!json_str) {
        return ESP_ERR_NO_MEM;
    }
    
    *out_json = json_str;
    return ESP_OK;
}

bool somnus_ble_is_running(void)
{
    return s_started;
}

bool somnus_ble_is_connected(void)
{
    portENTER_CRITICAL(&s_state_lock);
    bool connected = (s_conn_handle != BLE_HS_CONN_HANDLE_NONE);
    portEXIT_CRITICAL(&s_state_lock);
    return connected;
}

bool somnus_ble_is_advertising(void)
{
    // Advertising if running but not connected
    if (!s_started) {
        return false;
    }
    portENTER_CRITICAL(&s_state_lock);
    bool connected = (s_conn_handle != BLE_HS_CONN_HANDLE_NONE);
    portEXIT_CRITICAL(&s_state_lock);
    return !connected;  // Advertising when running but not connected
}

