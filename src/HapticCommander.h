
#pragma once

#include "comms/SimpleFOCRegisters.h"
#include "BLDCMotor.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>


#define REG_RECALIBRATE 0x81

// Recalibration settle time in ticks (replaces blocking vTaskDelay(1000ms))
#define RECAL_SETTLE_TICKS pdMS_TO_TICKS(1000)


/**
 * HapticCommander is an implementation of SimpleFOC RegisiterIO that operates on
 * messages received as Strings from the comms thread via ESP32 xQueue.
 *
 * It provides the bridge between SimpleFOC motor registers and the rest of the
 * comms code.
 */
class HapticCommander : public RegisterIO {
public:
    HapticCommander(BLDCMotor* motor);
    virtual ~HapticCommander() = default;

    void handleMessage(String* message);
    void sendRegister(uint8_t reg);

    /**
     * Call once per FOC loop iteration.
     * Drives the non-blocking recalibration state machine; returns true
     * while a recalibration is in progress (motor is disabled — callers
     * should skip haptic output during this window).
     */
    bool tickRecalibration();

    RegisterIO& operator<<(float value);
    RegisterIO& operator<<(uint32_t value);
    RegisterIO& operator<<(uint8_t value);
    RegisterIO& operator>>(float& value);
    RegisterIO& operator>>(uint32_t& value);
    RegisterIO& operator>>(uint8_t& value);

protected:
    BLDCMotor* motor;
    char* msg_in;
    String* msg_out;

    // Non-blocking recalibration state machine
    enum class RecalState : uint8_t {
        IDLE = 0,
        DISABLING,   // motor just disabled — wait for settle
        REINIT       // settle done — run initFOC then return to IDLE
    };
    RecalState _recalState = RecalState::IDLE;
    TickType_t _recalTickStart = 0;
};
