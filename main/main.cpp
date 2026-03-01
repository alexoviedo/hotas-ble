#include "ble_gamepad.h"
#include "debug_portal.h"
#include "hid_device_manager.h"
#include "usb_host_manager.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <stdio.h>

extern "C" void app_main() {
  printf("--- HOTAS USB to BLE Gamepad Bridge ---\n");

  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  // 1. Init BLE
  ble_gamepad_init();

  // 2. Init Debug Portal
  debug_portal_init();

  // 3. Init USB Host Manager
  usb_host_manager_init();

  // 4. Init HID Class Driver
  hid_device_manager_init();

  // 5. Main Loop to send BLE updates
  GamepadState state;
  while (1) {
    hid_device_manager_get_merged_state(&state);
    ble_gamepad_send_state(&state);
    vTaskDelay(pdMS_TO_TICKS(20)); // 50 Hz update rate
  }
}
