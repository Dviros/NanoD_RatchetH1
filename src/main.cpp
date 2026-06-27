#include <Arduino.h>

#include "nanofoc_d.h"
#include "./foc_thread.h"
#include "./hmi_thread.h"
#include "./lcd_thread.h"
#include "./com_thread.h"
#include "./DeviceSettings.h"
#include <esp_task_wdt.h>
#include <esp_ota_ops.h>   // Fix 2: direct OTA validity call (brick-proofing)
#include <LittleFS.h>
#include <Adafruit_TinyUSB.h>
#include "./wifi_thread.h"
#include "./sprite_store.h"

FocThread foc_thread(1);
HmiThread hmi_thread(0);
LcdThread lcd_thread(0);
ComThread com_thread(0);


void setup() {

  // initialize USB
  TinyUSBDevice.begin();
  hmi_thread.init_usb();
  TinyUSBDevice.setID(USB_VID, USB_PID);
  TinyUSBDevice.setProductDescriptor(USB_PRODUCT);
  TinyUSBDevice.setManufacturerDescriptor(USB_MANUFACTURER);
  // Per-device USB serial = eFuse MAC (read directly; DeviceSettings isn't up yet).
  // A unique serial lets macOS/Windows assign stable, distinct ports per unit and
  // lets the app tell two knobs apart. (Was the constant "Nano_D".)
  static char usb_serial[13];
  snprintf(usb_serial, sizeof(usb_serial), "%012llX", (unsigned long long)ESP.getEfuseMac());
  TinyUSBDevice.setSerialDescriptor(usb_serial);
  //TinyUSBDevice.attach();
  // 8 KB RX queue (USBCDC default is 256 B). The high-priority USB task drains the
  // 256 B TinyUSB FIFO into this queue, so a 4 KB binary sprite chunk can't overflow
  // and drop bytes mid-transfer. Must be set BEFORE begin() (begin keeps a preset queue).
  Serial.setRxBufferSize(8192);
  Serial.begin(DEFAULT_SERIAL_SPEED);

  delay(100);
  Serial.println("Welcome to Nano_D++!");
  Serial.print("Firmware version: ");
  Serial.println(NANO_FIRMWARE_VERSION);
  Serial.println("Initializing...");
  // before we begin, load our global settings...
  DeviceSettings& settings = DeviceSettings::getInstance();
  settings.init();
  settings.fromSPIFFS(); // load persisted settings (LittleFS, atomic write + CRC checked)

  // mount sprite storage + register the LVGL 'L:' filesystem driver
  SpriteStore::begin();

  // initialize PD power — negotiates voltage and stores it into DeviceSettings.pdVoltage
  // BEFORE foc_thread starts, so the motor driver comes up at the negotiated supply.
  hmi_thread.init_pd();

  // then load the profiles
  HapticProfileManager& profileManager = HapticProfileManager::getInstance();
  profileManager.fromSPIFFS(); // attempt to load profiles from SPIFFS

  // load motor calibration from Preferences
  MotorCalibration cal = settings.loadCalibration();
  foc_thread.setCalibration(cal);
  
  // load current profile from Preferences
  String current_profile = settings.loadCurrentProfile();
  profileManager.setCurrentProfile(current_profile);

  // bring up WiFi station / OTA / web transport (compiles to a no-op stub unless
  // WIFI_ENABLED is set). Non-blocking: never stalls boot if the network is absent.
  wifi_thread.begin();

  // init threads
  hmi_thread.init(profileManager.getCurrentProfile()->led_config, profileManager.getCurrentProfile()->hmi_config);
  if (profileManager.getCurrentProfile()->hmi_config.knob.num > 0)
    foc_thread.init(profileManager.getCurrentProfile()->hmi_config.knob.values[0].haptic);

  // start threads
  Serial.println("Starting threads...");
  Serial.flush();
  vTaskDelay(100 / portTICK_PERIOD_MS);
  lcd_thread.begin();
  com_thread.begin();
  hmi_thread.begin();
  foc_thread.begin();

  // Core threads are up. Give them a moment to fault on a bad flash, then mark
  // this OTA image valid so the bootloader stops arming a rollback. If the new
  // firmware crash-loops before this point, the bootloader reverts to the last
  // known-good slot. (Full auto-rollback also requires a bootloader built with
  // CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE — see docs/BRICK_PROOFING.md.)
  vTaskDelay(2000 / portTICK_PERIOD_MS);
  // Fix 2: call esp_ota_mark_app_valid_cancel_rollback() DIRECTLY so all build
  // variants (including the default no-WiFi build) actually mark the image valid.
  // wifi_thread.mark_ota_valid() is a no-op stub when WIFI_ENABLED is undefined,
  // meaning rollback was never cancelled on the default build. Ignore return value
  // — ESP_ERR_INVALID_ARG just means no OTA partition scheme is present, which is fine.
  esp_ota_mark_app_valid_cancel_rollback();
  wifi_thread.mark_ota_valid(); // wifi build may do additional bookkeeping

  vTaskDelete(NULL);
}

void loop() {
  // main loop not used...
  vTaskDelay(1000);
}
