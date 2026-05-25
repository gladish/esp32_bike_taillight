#ifndef GATT_SERVER_H
#define GATT_SERVER_H

#include <cstdint>
#include <functional>

#include <esp_err.h>
#include <esp_gap_ble_api.h>
#include <esp_gatts_api.h>

//
// Your current config appears to have BLE 4.2 advertising APIs disabled, so
// advertising setup is conditionally skipped in code when
// CONFIG_BT_BLE_42_FEATURES_SUPPORTED is off.
// The GATT server code is complete, but for standard advertising-based discovery
// you may need BLE 4.2 advertising support enabled in sdkconfig
// (or switch to BLE 5 extended advertising APIs).
//

class GattServer {
public:
	struct Color {
		uint8_t r;
		uint8_t g;
		uint8_t b;
	};

	using ColorReadCallback = std::function<void(Color&)>;
	using ColorWriteCallback = std::function<void(const Color&)>;
	using BrightnessReadCallback = std::function<void(uint8_t&)>;
	using BrightnessWriteCallback = std::function<void(uint8_t)>;

	static GattServer& Instance();

	esp_err_t Initialize();

	void SetColorReadCallback(ColorReadCallback callback);
	void SetColorWriteCallback(ColorWriteCallback callback);
	void SetBrightnessReadCallback(BrightnessReadCallback callback);
	void SetBrightnessWriteCallback(BrightnessWriteCallback callback);

private:
	GattServer() = default;

	static void GapEventHandler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t* param);
	static void GattsEventHandler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t* param);

	void HandleGapEvent(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t* param);
	void HandleGattsEvent(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t* param);

	void HandleGattsRead(esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t* param);
	void HandleGattsWrite(esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t* param);

	bool initialized_ = false;
	uint8_t adv_config_done_ = 0;
	esp_gatt_if_t gatts_if_ = ESP_GATT_IF_NONE;
	uint16_t conn_id_ = 0;
	uint16_t service_handle_ = 0;
	uint16_t color_char_handle_ = 0;
	uint16_t brightness_char_handle_ = 0;

	Color color_value_ = {255, 0, 0};
	uint8_t brightness_value_ = 100;

	ColorReadCallback color_on_read_;
	ColorWriteCallback color_on_write_;
	BrightnessReadCallback brightness_on_read_;
	BrightnessWriteCallback brightness_on_write_;
};

void BluetoothInitialize();

#endif