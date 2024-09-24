
#include "hmi_thread.h"
#include "com_thread.h"
#include "foc_thread.h"
#include <Adafruit_TinyUSB.h>
#include "MIDI.h"
#include "audio/audio.h"
#include <SparkFun_STUSB4500.h>

using namespace ace_button;

Adafruit_USBD_MIDI usb_midi(1);

MIDI_CREATE_INSTANCE(Adafruit_USBD_MIDI, usb_midi, midiu);
MIDI_CREATE_INSTANCE(HardwareSerial, Serial2, midi2)

enum {
  RID_KEYBOARD = 1,
  RID_MOUSE = 2,
  RID_GAMEPAD = 3,
};

uint8_t const desc_hid_report[] = {
  TUD_HID_REPORT_DESC_KEYBOARD( HID_REPORT_ID(RID_KEYBOARD) ),
  TUD_HID_REPORT_DESC_MOUSE   ( HID_REPORT_ID(RID_MOUSE) ),
  TUD_HID_REPORT_DESC_GAMEPAD( HID_REPORT_ID(RID_GAMEPAD) )
};

Adafruit_USBD_HID usb_hid;

// Initialize PD profiles
STUSB4500 usb_pd;

// init_usb() must be called before the thread is started
void HmiThread::init_usb() {
  usb_midi.setStringDescriptor("Nano_D MIDI");
  midiu.setHandleSystemExclusive(midi_sysex_handler);
  usb_midi.begin();
  midiu.begin();

  usb_hid.setBootProtocol(HID_ITF_PROTOCOL_NONE);
  usb_hid.setPollInterval(2);
  usb_hid.setReportDescriptor(desc_hid_report, sizeof(desc_hid_report));
  usb_hid.setStringDescriptor("Nano_D HID");
  usb_hid.begin();
  
  // Modify USB PD descriptors for compatibility with macOS and Windows
  TinyUSBDevice.setID(0x1209, 0x0001);  // Generic PD device VID/PID from pid.codes (or similar)
  TinyUSBDevice.setProductDescriptor("Certified PD Device");
  TinyUSBDevice.setManufacturerDescriptor("Binaris Circuitry");
  TinyUSBDevice.setSerialDescriptor("PD_Serial_12345");
  TinyUSBDevice.setDeviceClass(0x00);  // Device class defined at interface level
  TinyUSBDevice.setDeviceSubClass(0x00);
  TinyUSBDevice.setDeviceProtocol(0x00);
}

// Initialize USB Power Delivery (PD) profiles with compliant voltages and currents
PowerType HmiThread::init_pd() {
  Wire.begin(PIN_NANO_I2C_SDA, PIN_NANO_I2C_SCL);
  if (!usb_pd.begin()) {
    Serial.println("STUSB4500 not found");
  } else {
    Serial.println("STUSB4500 found");
  }

  // Check if the PD profiles are already set in NVM
  if (usb_pd.getPdoNumber() != 2) {
    Serial.println("Setting PD profiles in NVM...");
    
    // Program the PD profiles
    usb_pd.setUsbCommCapable(true);  // Enable USB communication

    // Set PDO1 (Default): 5V, 3A
    usb_pd.setVoltage(1, 5.0);
    usb_pd.setCurrent(1, 3.0);

    // Set PDO2 (High Priority): 9V, 3A
    usb_pd.setVoltage(2, 9.0);
    usb_pd.setCurrent(2, 3.0);

    // Disable PDO3
    usb_pd.setVoltage(3, 0);  // Disable PDO3
    usb_pd.setCurrent(3, 0);

    usb_pd.setPdoNumber(2);  // Only use 2 PDOs

    // Write the profiles to NVM
    usb_pd.write();
    Serial.println("PD profiles written to NVM.");
  } else {
    Serial.println("PD profiles already set.");
  }

  // Continue with normal device operation
  return POWER_5V_USB;
}
