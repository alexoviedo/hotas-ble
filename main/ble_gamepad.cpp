#include "ble_gamepad.h"
#include "esp_log.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include <string.h>

static const char *TAG = "BLE_GAMEPAD";

// HID Report Descriptor for 32 Buttons, 6 Axes, 2 Sliders, 1 Hat
static const uint8_t hid_report_desc[] = {
    0x05, 0x01, // Usage Page (Generic Desktop Ctrls)
    0x09, 0x05, // Usage (Game Pad)
    0xA1, 0x01, // Collection (Application)
    0x85, 0x01, //   Report ID (1)

    // 32 Buttons
    0x05, 0x09, //   Usage Page (Button)
    0x19, 0x01, //   Usage Minimum (0x01)
    0x29, 0x20, //   Usage Maximum (0x20)
    0x15, 0x00, //   Logical Minimum (0)
    0x25, 0x01, //   Logical Maximum (1)
    0x95, 0x20, //   Report Count (32)
    0x75, 0x01, //   Report Size (1)
    0x81, 0x02, //   Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null
                //   Position)

    // 6 Axes: X, Y, Z, Rx, Ry, Rz
    0x05, 0x01,       //   Usage Page (Generic Desktop Ctrls)
    0x09, 0x30,       //   Usage (X)
    0x09, 0x31,       //   Usage (Y)
    0x09, 0x32,       //   Usage (Z)
    0x09, 0x33,       //   Usage (Rx)
    0x09, 0x34,       //   Usage (Ry)
    0x09, 0x35,       //   Usage (Rz)
    0x16, 0x01, 0x80, //   Logical Minimum (-32767)
    0x26, 0xFF, 0x7F, //   Logical Maximum (32767)
    0x95, 0x06,       //   Report Count (6)
    0x75, 0x10,       //   Report Size (16)
    0x81, 0x02, //   Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null
                //   Position)

    // 2 Sliders
    0x09, 0x36,       //   Usage (Slider)
    0x09, 0x37,       //   Usage (Dial / Slider 2)
    0x16, 0x01, 0x80, //   Logical Minimum (-32767)
    0x26, 0xFF, 0x7F, //   Logical Maximum (32767)
    0x95, 0x02,       //   Report Count (2)
    0x75, 0x10,       //   Report Size (16)
    0x81, 0x02,       //   Input (Data,Var,Abs)

    // 1 Hat Switch
    0x09, 0x39,       //   Usage (Hat switch)
    0x15, 0x01,       //   Logical Minimum (1)
    0x25, 0x08,       //   Logical Maximum (8)
    0x35, 0x00,       //   Physical Minimum (0)
    0x46, 0x3B, 0x01, //   Physical Maximum (315)
    0x65, 0x14,       //   Unit (System: English Rotation, Length: Centimeter)
    0x95, 0x01,       //   Report Count (1)
    0x75, 0x04,       //   Report Size (4)
    0x81,
    0x02, //   Input (Data,Var,Abs,No Wrap,Linear,Preferred State,Null State)

    // Padding 4 bits
    0x95, 0x01, 0x75, 0x04, 0x81, 0x03, //   Input (Const,Var,Abs)
    0xC0                                // End Collection
};

// BLE HID Structs
struct BleGamepadReport {
  uint32_t buttons;
  int16_t x;
  int16_t y;
  int16_t z;
  int16_t rx;
  int16_t ry;
  int16_t rz;
  int16_t slider1;
  int16_t slider2;
  uint8_t hat_and_pad;
} __attribute__((packed));

static uint16_t hid_report_handle;
static uint16_t conn_handle = BLE_HS_CONN_HANDLE_NONE;
static BleGamepadReport last_report = {0};

static int gatt_svr_chr_access_hid(uint16_t conn_handle, uint16_t attr_handle,
                                   struct ble_gatt_access_ctxt *ctxt,
                                   void *arg) {
  uint16_t uuid16 = ble_uuid_u16(ctxt->chr->uuid);

  if (uuid16 == 0x2A4B) { // Report Map
    int rc = os_mbuf_append(ctxt->om, hid_report_desc, sizeof(hid_report_desc));
    return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
  } else if (uuid16 == 0x2A4D) { // Report
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
      int rc = os_mbuf_append(ctxt->om, &last_report, sizeof(last_report));
      return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
  } else if (uuid16 == 0x2A4A) {               // HID Info
    uint8_t info[] = {0x11, 0x01, 0x00, 0x02}; // version, country, flags
    int rc = os_mbuf_append(ctxt->om, info, sizeof(info));
    return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
  }
  return BLE_ATT_ERR_UNLIKELY;
}

static const ble_uuid16_t hid_svc_uuid = BLE_UUID16_INIT(0x1812);
static const ble_uuid16_t hid_report_map_uuid = BLE_UUID16_INIT(0x2A4B);
static const ble_uuid16_t hid_report_uuid = BLE_UUID16_INIT(0x2A4D);
static const ble_uuid16_t hid_info_uuid = BLE_UUID16_INIT(0x2A4A);

static const struct ble_gatt_svc_def gatt_svr_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &hid_svc_uuid.u,
        .includes = NULL,
        .characteristics =
            (struct ble_gatt_chr_def[]){
                {
                    .uuid = &hid_report_map_uuid.u,
                    .access_cb = gatt_svr_chr_access_hid,
                    .arg = NULL,
                    .descriptors = NULL,
                    .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC,
                    .min_key_size = 0,
                    .val_handle = NULL,
                },
                {
                    .uuid = &hid_report_uuid.u,
                    .access_cb = gatt_svr_chr_access_hid,
                    .arg = NULL,
                    .descriptors = NULL,
                    .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC |
                             BLE_GATT_CHR_F_NOTIFY,
                    .min_key_size = 0,
                    .val_handle = &hid_report_handle,
                },
                {
                    .uuid = &hid_info_uuid.u,
                    .access_cb = gatt_svr_chr_access_hid,
                    .arg = NULL,
                    .descriptors = NULL,
                    .flags = BLE_GATT_CHR_F_READ,
                    .min_key_size = 0,
                    .val_handle = NULL,
                },
                {
                    0, // No more characteristics
                }},
    },
    {
        0, // No more services
    },
};
static void ble_app_advertise(void);

static int ble_gamepad_gap_event(struct ble_gap_event *event, void *arg) {
  if (event->type == BLE_GAP_EVENT_CONNECT) {
    if (event->connect.status == 0) {
      ESP_LOGI(TAG, "BLE Connected");
      conn_handle = event->connect.conn_handle;
    } else {
      ESP_LOGW(TAG, "BLE Connection failed, status=%d", event->connect.status);
      ble_app_advertise(); // Restart advertising
    }
  } else if (event->type == BLE_GAP_EVENT_DISCONNECT) {
    ESP_LOGI(TAG, "BLE Disconnected, reason=%d", event->disconnect.reason);
    conn_handle = BLE_HS_CONN_HANDLE_NONE;
    ble_app_advertise(); // Restart advertising
  } else if (event->type == BLE_GAP_EVENT_PASSKEY_ACTION) {
    ESP_LOGI(TAG, "BLE Passkey Action Event: action=%d",
             event->passkey.params.action);
    struct ble_sm_io pkey;
    memset(&pkey, 0, sizeof(pkey));
    pkey.action = event->passkey.params.action;

    if (pkey.action == BLE_SM_IOACT_NONE) {
      // Just Works
      pkey.action = BLE_SM_IOACT_NONE;
    }

    int rc = ble_sm_inject_io(event->passkey.conn_handle, &pkey);
    ESP_LOGI(TAG, "Injected IO for passkey action, result=%d", rc);
  }
  return 0;
}

static uint8_t ble_addr_type;

static void ble_app_advertise(void) {
  struct ble_gap_adv_params adv_params;
  struct ble_hs_adv_fields fields;

  memset(&fields, 0, sizeof(fields));
  fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
  fields.name = (uint8_t *)"HOTAS-Bridge";
  fields.name_len = strlen("HOTAS-Bridge");
  fields.name_is_complete = 1;
  fields.appearance = 0x03C4; // Gamepad
  fields.appearance_is_present = 1;

  fields.uuids16 = (ble_uuid16_t[]){BLE_UUID16_INIT(0x1812)};
  fields.num_uuids16 = 1;
  fields.uuids16_is_complete = 1;

  ble_gap_adv_set_fields(&fields);

  memset(&adv_params, 0, sizeof(adv_params));
  adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
  adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
  ble_gap_adv_start(ble_addr_type, NULL, BLE_HS_FOREVER, &adv_params,
                    ble_gamepad_gap_event, NULL);
}

void ble_gamepad_sync_cb(void) {
  ble_hs_id_infer_auto(0, &ble_addr_type);
  ble_app_advertise();
}

static void ble_host_task(void *param) {
  ESP_LOGI(TAG, "BLE Host Task Started");
  nimble_port_run();
  nimble_port_freertos_deinit();
}

extern "C" void ble_store_config_init(void);

void ble_gamepad_init(void) {
  ESP_LOGI(TAG, "Initializing NimBLE Gamepad...");
  nimble_port_init();

  ble_hs_cfg.sync_cb = ble_gamepad_sync_cb;
  ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

  ble_svc_gap_device_name_set("HOTAS-Bridge");
  ble_svc_gap_init();
  ble_svc_gatt_init();

  ble_gatts_count_cfg(gatt_svr_svcs);
  ble_gatts_add_svcs(gatt_svr_svcs);

  // Set Security
  ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_NO_IO;
  ble_hs_cfg.sm_bonding = 1;
  ble_hs_cfg.sm_mitm = 0;
  ble_hs_cfg.sm_sc =
      0; // Disable strict Secure Connections for Just Works compatibility
  // Distribute both ENC (Encryption) and ID (Identity) keys
  ble_hs_cfg.sm_our_key_dist =
      BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
  ble_hs_cfg.sm_their_key_dist =
      BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;

  ble_store_config_init();

  nimble_port_freertos_init(ble_host_task);
}

void ble_gamepad_send_state(const struct GamepadState *state) {
  if (conn_handle == BLE_HS_CONN_HANDLE_NONE)
    return;

  last_report.buttons = state->buttons;
  last_report.x = state->x;
  last_report.y = state->y;
  last_report.z = state->z;
  last_report.rx = state->rx;
  last_report.ry = state->ry;
  last_report.rz = state->rz;
  last_report.slider1 = state->slider1;
  last_report.slider2 = state->slider2;
  last_report.hat_and_pad = state->hat & 0x0F;

  struct os_mbuf *om = ble_hs_mbuf_from_flat(&last_report, sizeof(last_report));
  if (om) {
    ble_gatts_notify_custom(conn_handle, hid_report_handle, om);
  }
}
