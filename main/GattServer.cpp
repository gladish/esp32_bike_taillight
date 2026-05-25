#include "GattServer.h"

#include <sdkconfig.h>

#include <algorithm>
#include <cstring>
#include <utility>

#include <esp_bt.h>
#include <esp_bt_main.h>
#include <esp_gatt_common_api.h>
#include <esp_log.h>

#include <nvs_flash.h>

namespace {
constexpr char kTag[] = "gatts";

constexpr uint16_t kAppId = 0x55;

constexpr uint16_t kSetupServiceUuid = 0xA000;
constexpr uint16_t kColorCharUuid = 0xA001;
constexpr uint16_t kBrightnessCharUuid = 0xA002;

constexpr uint8_t kAdvConfigFlag = 1;
constexpr uint8_t kScanRspConfigFlag = 2;

constexpr char kDeviceName[] = "BikeTailLight";

#if CONFIG_BT_BLE_42_FEATURES_SUPPORTED
esp_ble_adv_data_t adv_data = {
    .set_scan_rsp = false,
    .include_name = true,
    .include_txpower = true,
    .min_interval = 0x20,
    .max_interval = 0x40,
    .appearance = 0x00,
    .manufacturer_len = 0,
    .p_manufacturer_data = nullptr,
    .service_data_len = 0,
    .p_service_data = nullptr,
    .service_uuid_len = 0,
    .p_service_uuid = nullptr,
    .flag = ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT,
};

esp_ble_adv_data_t scan_rsp_data = {
    .set_scan_rsp = true,
    .include_name = true,
    .include_txpower = true,
    .min_interval = 0x20,
    .max_interval = 0x40,
    .appearance = 0x00,
    .manufacturer_len = 0,
    .p_manufacturer_data = nullptr,
    .service_data_len = 0,
    .p_service_data = nullptr,
    .service_uuid_len = 0,
    .p_service_uuid = nullptr,
    .flag = ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT,
};

esp_ble_adv_params_t adv_params = {
    .adv_int_min = ESP_BLE_GAP_ADV_ITVL_MS(20),
    .adv_int_max = ESP_BLE_GAP_ADV_ITVL_MS(40),
    .adv_type = ADV_TYPE_IND,
    .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
    .peer_addr = {0},
    .peer_addr_type = BLE_ADDR_TYPE_PUBLIC,
    .channel_map = ADV_CHNL_ALL,
    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};
  #endif

#if CONFIG_BT_BLE_50_FEATURES_SUPPORTED
constexpr uint8_t kExtAdvInstance = 0;

esp_ble_gap_ext_adv_params_t ext_adv_params = {
    .type = ESP_BLE_GAP_SET_EXT_ADV_PROP_LEGACY_IND,
    .interval_min = 0x20,
    .interval_max = 0x40,
    .channel_map = ADV_CHNL_ALL,
    .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
    .peer_addr_type = BLE_ADDR_TYPE_PUBLIC,
    .peer_addr = {0},
    .filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
    .tx_power = EXT_ADV_TX_PWR_NO_PREFERENCE,
    .primary_phy = ESP_BLE_GAP_PHY_1M,
    .max_skip = 0,
    .secondary_phy = ESP_BLE_GAP_PHY_1M,
    .sid = 0,
    .scan_req_notif = false,
};

uint8_t ext_adv_raw_data[31] = {};
uint16_t ext_adv_raw_len = 0;

void PrepareExtAdvRawData()
{
  const size_t device_name_len = strlen(kDeviceName);
  const size_t max_name_len = sizeof(ext_adv_raw_data) - 5;
  const size_t name_len = std::min(device_name_len, max_name_len);

  ext_adv_raw_data[0] = 2;
  ext_adv_raw_data[1] = ESP_BLE_AD_TYPE_FLAG;
  ext_adv_raw_data[2] = ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT;
  ext_adv_raw_data[3] = static_cast<uint8_t>(name_len + 1);
  ext_adv_raw_data[4] = ESP_BLE_AD_TYPE_NAME_CMPL;
  memcpy(&ext_adv_raw_data[5], kDeviceName, name_len);
  ext_adv_raw_len = static_cast<uint16_t>(name_len + 5);
}
#endif

GattServer* g_server_instance = nullptr;

uint8_t ClampBrightness(uint8_t value)
{
  return static_cast<uint8_t>(std::clamp<int>(value, 1, 100));
}

}  // namespace

GattServer& GattServer::Instance()
{
  static GattServer server;
  return server;
}

esp_err_t GattServer::Initialize()
{
  if (initialized_) {
    return ESP_OK;
  }

  g_server_instance = this;

  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));

  esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
  ret = esp_bt_controller_init(&bt_cfg);
  if (ret) {
    ESP_LOGE(kTag, "%s init controller failed: %s", __func__, esp_err_to_name(ret));
    return ret;
  }

  ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
  if (ret) {
    ESP_LOGE(kTag, "%s enable controller failed: %s", __func__, esp_err_to_name(ret));
    return ret;
  }

  esp_bluedroid_config_t cfg = BT_BLUEDROID_INIT_CONFIG_DEFAULT();
  ret = esp_bluedroid_init_with_cfg(&cfg);
  if (ret) {
    ESP_LOGE(kTag, "%s init bluedroid failed: %s", __func__, esp_err_to_name(ret));
    return ret;
  }

  ret = esp_bluedroid_enable();
  if (ret) {
    ESP_LOGE(kTag, "%s enable bluedroid failed: %s", __func__, esp_err_to_name(ret));
    return ret;
  }

  ret = esp_ble_gatts_register_callback(GattServer::GattsEventHandler);
  if (ret) {
    ESP_LOGE(kTag, "gatts register callback failed: %s", esp_err_to_name(ret));
    return ret;
  }

  ret = esp_ble_gap_register_callback(GattServer::GapEventHandler);
  if (ret) {
    ESP_LOGE(kTag, "gap register callback failed: %s", esp_err_to_name(ret));
    return ret;
  }

  ret = esp_ble_gatts_app_register(kAppId);
  if (ret) {
    ESP_LOGE(kTag, "gatts app register failed: %s", esp_err_to_name(ret));
    return ret;
  }

  ret = esp_ble_gatt_set_local_mtu(500);
  if (ret) {
    ESP_LOGE(kTag, "set local mtu failed: %s", esp_err_to_name(ret));
    return ret;
  }

  initialized_ = true;
  return ESP_OK;
}

void GattServer::SetColorReadCallback(ColorReadCallback callback)
{
  color_on_read_ = std::move(callback);
}

void GattServer::SetColorWriteCallback(ColorWriteCallback callback)
{
  color_on_write_ = std::move(callback);
}

void GattServer::SetBrightnessReadCallback(BrightnessReadCallback callback)
{
  brightness_on_read_ = std::move(callback);
}

void GattServer::SetBrightnessWriteCallback(BrightnessWriteCallback callback)
{
  brightness_on_write_ = std::move(callback);
}

void GattServer::GapEventHandler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t* param)
{
  if (g_server_instance == nullptr) {
    return;
  }

  g_server_instance->HandleGapEvent(event, param);
}

void GattServer::GattsEventHandler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t* param)
{
  if (g_server_instance == nullptr) {
    return;
  }

  g_server_instance->HandleGattsEvent(event, gatts_if, param);
}

void GattServer::HandleGapEvent(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t* param)
{
#if CONFIG_BT_BLE_42_FEATURES_SUPPORTED
  if (event == ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT) {
      adv_config_done_ &= static_cast<uint8_t>(~kAdvConfigFlag);
      if (adv_config_done_ == 0) {
        const esp_err_t adv_ret = esp_ble_gap_start_advertising(&adv_params);
        ESP_ERROR_CHECK(adv_ret);
      }
      return;
  }

  if (event == ESP_GAP_BLE_SCAN_RSP_DATA_SET_COMPLETE_EVT) {
      adv_config_done_ &= static_cast<uint8_t>(~kScanRspConfigFlag);
      if (adv_config_done_ == 0) {
        const esp_err_t adv_ret = esp_ble_gap_start_advertising(&adv_params);
        ESP_ERROR_CHECK(adv_ret);
      }
      return;
  }

  if (event == ESP_GAP_BLE_ADV_START_COMPLETE_EVT) {
      if (param->adv_start_cmpl.status != ESP_BT_STATUS_SUCCESS) {
        ESP_LOGE(kTag, "BLE4.2 advertising start failed, status=%d", param->adv_start_cmpl.status);
      }
      return;
  }

  if (event == ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT) {
      if (param->adv_stop_cmpl.status != ESP_BT_STATUS_SUCCESS) {
        ESP_LOGE(kTag, "BLE4.2 advertising stop failed, status=%d", param->adv_stop_cmpl.status);
      }
      return;
  }
#endif
#if CONFIG_BT_BLE_50_FEATURES_SUPPORTED
  if (event == ESP_GAP_BLE_EXT_ADV_SET_PARAMS_COMPLETE_EVT) {
      if (param->ext_adv_set_params.status != ESP_BT_STATUS_SUCCESS) {
        ESP_LOGE(kTag, "BLE5 ext adv parameter setup failed, status=%d", param->ext_adv_set_params.status);
        return;
      }

      const esp_err_t data_ret = esp_ble_gap_config_ext_adv_data_raw(kExtAdvInstance, ext_adv_raw_len, ext_adv_raw_data);
      ESP_ERROR_CHECK(data_ret);
      return;
  }

  if (event == ESP_GAP_BLE_EXT_ADV_DATA_SET_COMPLETE_EVT) {
      if (param->ext_adv_data_set.status != ESP_BT_STATUS_SUCCESS) {
        ESP_LOGE(kTag, "BLE5 ext adv data setup failed, status=%d", param->ext_adv_data_set.status);
        return;
      }
      const esp_ble_gap_ext_adv_t ext_adv = {
        .instance = kExtAdvInstance,
        .duration = 0,
        .max_events = 0,
      };
      const esp_err_t start_ret = esp_ble_gap_ext_adv_start(1, &ext_adv);
      ESP_ERROR_CHECK(start_ret);
      return;
  }

  if (event == ESP_GAP_BLE_EXT_SCAN_RSP_DATA_SET_COMPLETE_EVT) {
      return;
  }

  if (event == ESP_GAP_BLE_EXT_ADV_START_COMPLETE_EVT) {
      if (param->ext_adv_start.status != ESP_BT_STATUS_SUCCESS) {
        ESP_LOGE(kTag, "BLE5 ext advertising start failed, status=%d", param->ext_adv_start.status);
      }
      return;
  }

  if (event == ESP_GAP_BLE_EXT_ADV_STOP_COMPLETE_EVT) {
      if (param->ext_adv_stop.status != ESP_BT_STATUS_SUCCESS) {
        ESP_LOGE(kTag, "BLE5 ext advertising stop failed, status=%d", param->ext_adv_stop.status);
      }
      return;
  }
#endif
}

void GattServer::HandleGattsEvent(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t* param)
{
  if (event == ESP_GATTS_REG_EVT) {
    if (param->reg.status != ESP_GATT_OK) {
      ESP_LOGE(kTag, "registration failed: %d", param->reg.status);
      return;
    }

    gatts_if_ = gatts_if;

    ESP_ERROR_CHECK(esp_ble_gap_set_device_name(kDeviceName));
  #if CONFIG_BT_BLE_42_FEATURES_SUPPORTED
    adv_config_done_ = kAdvConfigFlag | kScanRspConfigFlag;

    ESP_ERROR_CHECK(esp_ble_gap_config_adv_data(&adv_data));
    ESP_ERROR_CHECK(esp_ble_gap_config_adv_data(&scan_rsp_data));
  #elif CONFIG_BT_BLE_50_FEATURES_SUPPORTED
    PrepareExtAdvRawData();
    ESP_ERROR_CHECK(esp_ble_gap_ext_adv_set_params(kExtAdvInstance, &ext_adv_params));
  #else
    ESP_LOGW(kTag, "BLE 4.2 advertising APIs disabled by sdkconfig; skipping advertising setup");
  #endif

    esp_gatt_srvc_id_t service_id = {
      .id = {
        .uuid = {
          .len = ESP_UUID_LEN_16,
          .uuid = {.uuid16 = kSetupServiceUuid},
        },
        .inst_id = 0,
      },
      .is_primary = true,
    };

    ESP_ERROR_CHECK(esp_ble_gatts_create_service(gatts_if_, &service_id, 8));
    return;
  }

  if (gatts_if_ != ESP_GATT_IF_NONE && gatts_if != ESP_GATT_IF_NONE && gatts_if != gatts_if_) {
    return;
  }

  switch (event) {
    case ESP_GATTS_CREATE_EVT: {
      service_handle_ = param->create.service_handle;
      ESP_ERROR_CHECK(esp_ble_gatts_start_service(service_handle_));

      esp_bt_uuid_t color_uuid = {
        .len = ESP_UUID_LEN_16,
        .uuid = {.uuid16 = kColorCharUuid},
      };

      uint8_t color_raw[3] = {color_value_.r, color_value_.g, color_value_.b};
      esp_attr_value_t color_attr = {
        .attr_max_len = sizeof(color_raw),
        .attr_len = sizeof(color_raw),
        .attr_value = color_raw,
      };

      esp_attr_control_t rsp_by_app = {.auto_rsp = ESP_GATT_RSP_BY_APP};

      ESP_ERROR_CHECK(esp_ble_gatts_add_char(
          service_handle_,
          &color_uuid,
          ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
          ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_WRITE,
          &color_attr,
          &rsp_by_app));
      break;
    }
    case ESP_GATTS_ADD_CHAR_EVT: {
      if (param->add_char.status != ESP_GATT_OK) {
        ESP_LOGE(kTag, "add char failed: %d", param->add_char.status);
        break;
      }

      if (param->add_char.char_uuid.len != ESP_UUID_LEN_16) {
        break;
      }

      const uint16_t uuid = param->add_char.char_uuid.uuid.uuid16;
      if (uuid == kColorCharUuid) {
        color_char_handle_ = param->add_char.attr_handle;

        esp_bt_uuid_t brightness_uuid = {
          .len = ESP_UUID_LEN_16,
          .uuid = {.uuid16 = kBrightnessCharUuid},
        };

        uint8_t brightness_raw[1] = {brightness_value_};
        esp_attr_value_t brightness_attr = {
          .attr_max_len = sizeof(brightness_raw),
          .attr_len = sizeof(brightness_raw),
          .attr_value = brightness_raw,
        };

        esp_attr_control_t rsp_by_app = {.auto_rsp = ESP_GATT_RSP_BY_APP};
        ESP_ERROR_CHECK(esp_ble_gatts_add_char(
            service_handle_,
            &brightness_uuid,
            ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
            ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_WRITE,
            &brightness_attr,
            &rsp_by_app));
      } else if (uuid == kBrightnessCharUuid) {
        brightness_char_handle_ = param->add_char.attr_handle;
      }
      break;
    }
    case ESP_GATTS_CONNECT_EVT:
      conn_id_ = param->connect.conn_id;
      break;
    case ESP_GATTS_READ_EVT:
      HandleGattsRead(gatts_if, param);
      break;
    case ESP_GATTS_WRITE_EVT:
      HandleGattsWrite(gatts_if, param);
      break;
    default:
      break;
  }
}

void GattServer::HandleGattsRead(esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t* param)
{
  esp_gatt_rsp_t rsp = {};
  rsp.attr_value.handle = param->read.handle;

  if (param->read.handle == color_char_handle_) {
    Color value = color_value_;
    if (color_on_read_) {
      color_on_read_(value);
    }

    color_value_ = value;

    rsp.attr_value.len = 3;
    rsp.attr_value.value[0] = value.r;
    rsp.attr_value.value[1] = value.g;
    rsp.attr_value.value[2] = value.b;
  } else if (param->read.handle == brightness_char_handle_) {
    uint8_t value = brightness_value_;
    if (brightness_on_read_) {
      brightness_on_read_(value);
    }

    value = ClampBrightness(value);
    brightness_value_ = value;

    rsp.attr_value.len = 1;
    rsp.attr_value.value[0] = value;
  } else {
    ESP_ERROR_CHECK(esp_ble_gatts_send_response(gatts_if, param->read.conn_id, param->read.trans_id, ESP_GATT_NOT_FOUND, nullptr));
    return;
  }

  ESP_ERROR_CHECK(esp_ble_gatts_send_response(gatts_if, param->read.conn_id, param->read.trans_id, ESP_GATT_OK, &rsp));
}

void GattServer::HandleGattsWrite(esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t* param)
{
  esp_gatt_status_t status = ESP_GATT_OK;

  if (param->write.is_prep) {
    status = ESP_GATT_REQ_NOT_SUPPORTED;
  } else if (param->write.handle == color_char_handle_) {
    if (param->write.len != 3) {
      status = ESP_GATT_INVALID_ATTR_LEN;
    } else {
      Color value = {
        .r = param->write.value[0],
        .g = param->write.value[1],
        .b = param->write.value[2],
      };

      color_value_ = value;
      if (color_on_write_) {
        color_on_write_(value);
      }
    }
  } else if (param->write.handle == brightness_char_handle_) {
    if (param->write.len != 1) {
      status = ESP_GATT_INVALID_ATTR_LEN;
    } else {
      const uint8_t value = ClampBrightness(param->write.value[0]);
      brightness_value_ = value;
      if (brightness_on_write_) {
        brightness_on_write_(value);
      }
    }
  } else {
    status = ESP_GATT_NOT_FOUND;
  }

  if (param->write.need_rsp) {
    ESP_ERROR_CHECK(esp_ble_gatts_send_response(gatts_if, param->write.conn_id, param->write.trans_id, status, nullptr));
  }
}

void BluetoothInitialize()
{
  const esp_err_t ret = GattServer::Instance().Initialize();
  if (ret != ESP_OK) {
    ESP_LOGE(kTag, "BluetoothInitialize failed: %s", esp_err_to_name(ret));
  }
}
