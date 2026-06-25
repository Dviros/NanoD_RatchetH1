
#include "./HapticCommander.h"

HapticCommander::HapticCommander(BLDCMotor* motor) : motor(motor) {};

void HapticCommander::handleMessage(String* message) {
    msg_in = (char*)message->c_str();
    uint8_t reg = atoi(msg_in);
    msg_in = strchr(msg_in, '=');
    if (msg_in != NULL) {
        if (reg==REG_RECALIBRATE) {
            uint8_t value; *this >> value;
            if (value==1 && _recalState == RecalState::IDLE) {
                // FIX: removed blocking vTaskDelay(1000ms) from FOC real-time path.
                // Transition to the non-blocking state machine instead.
                // tickRecalibration() (called from foc_thread loop) drives the rest.
                motor->disable();
                _recalTickStart = xTaskGetTickCount();
                _recalState = RecalState::DISABLING;
            }
        }
        else
            SimpleFOCRegisters::regs->commsToRegister(*this, reg, motor);
    }
    msg_out = message;
    sendRegister(reg);
    msg_in = NULL;
};


bool HapticCommander::tickRecalibration() {
    switch (_recalState) {
        case RecalState::IDLE:
            return false;

        case RecalState::DISABLING:
            // Wait for the settle period without blocking.
            if ((xTaskGetTickCount() - _recalTickStart) >= RECAL_SETTLE_TICKS) {
                // Settle done — clear calibration and re-init.
                motor->sensor_direction = Direction::UNKNOWN;
                motor->zero_electric_angle = NOT_SET;
                _recalState = RecalState::REINIT;
            }
            return true; // still in progress

        case RecalState::REINIT:
            motor->enable();
            motor->initFOC();
            _recalState = RecalState::IDLE;
            return false; // done

        default:
            _recalState = RecalState::IDLE;
            return false;
    }
};



void HapticCommander::sendRegister(uint8_t reg) {
    msg_out->clear();
    msg_out->concat('r');
    msg_out->concat((int)reg);
    msg_out->concat('=');
    if (reg==REG_RECALIBRATE) {
        msg_out->concat("0");
    }
    else
        SimpleFOCRegisters::regs->registerToComms(*this, reg, motor);
};



RegisterIO& HapticCommander::operator<<(float value) {
    msg_out->concat(String(value, 4));
    return *this;
};

RegisterIO& HapticCommander::operator<<(uint32_t value) {
    msg_out->concat(value);
    return *this;
};

RegisterIO& HapticCommander::operator<<(uint8_t value) {
    msg_out->concat(value);
    return *this;
};

RegisterIO& HapticCommander::operator>>(float& value) {
    if (msg_in != NULL) {
        msg_in++; // skip the separator
        value = atoff(msg_in);
        msg_in = strchr(msg_in, ','); // next position, if any
    }
    return *this;
};


RegisterIO& HapticCommander::operator>>(uint32_t& value) {
    if (msg_in != NULL) {
        msg_in++; // skip the separator
        value = atoi(msg_in);
        msg_in = strchr(msg_in, ','); // next position, if any
    }
    return *this;
};


RegisterIO& HapticCommander::operator>>(uint8_t& value) {
    if (msg_in != NULL) {
        msg_in++; // skip the separator
        value = atoi(msg_in);
        msg_in = strchr(msg_in, ','); // next position, if any
    }
    return *this;
};


