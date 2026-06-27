#include "./foc_thread.h"
#include "utils.h"
#include "HapticCommander.h"
#include "./com_thread.h"
#include "sprite_store.h"   // SpriteStore::binReceiving() — park the motor during uploads


/*
    FOC Thread runs BLDCHaptic Library in estimated current mode
    It sends and receives data from Com Thread.
*/


// global variables
BLDCMotor motor = BLDCMotor(7, 5.3);
BLDCDriver3PWM driver = BLDCDriver3PWM(PIN_IN_U, PIN_IN_V, PIN_IN_W, PIN_EN_U, PIN_EN_V, PIN_EN_W);

MagneticSensorMT6701SSI encoder(PIN_MAG_CS);

HapticInterface haptic = HapticInterface(&motor);
HapticCommander commander = HapticCommander(&motor);



FocThread::FocThread(const uint8_t task_core) : Thread("FOC", 8192, 1, task_core) {
    _q_motor_in = xQueueCreate(5, sizeof( String* ));
    _q_haptic_in = xQueueCreate(2, sizeof( DetentProfile ));
    _q_angleevt_out = xQueueCreate(5, sizeof( AngleEvt ));
    assert(_q_motor_in != NULL);
    assert(_q_haptic_in != NULL);
    assert(_q_angleevt_out != NULL);
}

FocThread::~FocThread() {}


void FocThread::init(DetentProfile& initialConfig) {
    haptic.haptic_state = HapticState(initialConfig);
};


void FocThread::run() {
    SPIClass* spi = new SPIClass(HSPI);
    spi->begin(PIN_MAG_CLK, PIN_MAG_DO, -1, PIN_MAG_CS);
    encoder.init(spi);

    // FIX: use negotiated PD voltage from DeviceSettings; clamp to board-safe [5.0, 9.0] V.
    {
        float pdV = DeviceSettings::getInstance().pdVoltage;
        if (pdV < 5.0f || pdV > 9.0f) pdV = 5.0f; // default/safe fallback
        driver.voltage_power_supply = pdV;
        driver.voltage_limit = pdV; // limit cannot exceed supply
    }

    driver.init();
    motor.linkSensor(&encoder);
    motor.linkDriver(&driver);
    motor.LPF_velocity.Tf = 0.01f;
    motor.current_limit = 1.22;
    motor.init();
    Direction dir = motor.sensor_direction;
    if (dir == Direction::UNKNOWN) {
        com_thread.put_string_message(StringMessage(new String("Calibration required..."), StringMessageType::STRING_MESSAGE_DEBUG));
    }
    int initResult = motor.initFOC();
    if (motor.sensor_direction != dir && initResult != 0) {
        com_thread.put_string_message(StringMessage(new String("Storing calibration in Preferences..."), StringMessageType::STRING_MESSAGE_DEBUG));
        MotorCalibration cal = { motor.sensor_direction, motor.zero_electric_angle };
        DeviceSettings::getInstance().storeCalibration(cal);
    }
    if (initResult == 0) {
        com_thread.put_string_message(StringMessage(new String("Motor init failed!"), StringMessageType::STRING_MESSAGE_ERROR));
    }
    haptic.init();
    haptic.motor->sensor_offset = haptic.motor->shaft_angle;
    // float lastang = encoder.getAngle();
    // unsigned long ts = micros();
    uint16_t serial_last_pos = 0;
    while (true) {
        // Drive non-blocking recalibration; skip haptic output while in progress.
        // Also park the motor (no torque) during a binary sprite upload: LittleFS
        // flash writes stall this core's cached code, so holding detents makes the
        // knob buzz. Free-spin for the ~3 s upload, then detents resume.
        if (!commander.tickRecalibration() && !SpriteStore::binReceiving()) {
            haptic.haptic_loop();
        }
        float ang = encoder.getAngle();
        unsigned long now = micros();
        // if (fabs(ang - lastang) >= angleEventMinAngle && now - ts >= angleEventMinMicroseconds) {
        if (haptic.haptic_state.current_pos != serial_last_pos){
            AngleEvt ae = { haptic.haptic_state.current_pos };
            xQueueSend(_q_angleevt_out, &ae, (TickType_t)0);
            serial_last_pos = haptic.haptic_state.current_pos;
            // Relaxed stores: HMI thread on core 0 reads these via relaxed loads in
            // pass_cur_pos/pass_start_pos/pass_end_pos — no torn-read, no mutex needed.
            _a_cur_pos.store(haptic.haptic_state.current_pos, std::memory_order_relaxed);
            _a_start_pos.store(haptic.haptic_state.detent_profile.start_pos, std::memory_order_relaxed);
            _a_end_pos.store(haptic.haptic_state.detent_profile.end_pos, std::memory_order_relaxed);
        }
        
        
        handleMessage();
        handleHapticConfig();
    }
        
};

void FocThread::put_motor_command(String* message) {
    if (message!=nullptr)
        xQueueSend(_q_motor_in, &message, (TickType_t)0);
};


void FocThread::put_haptic_config(DetentProfile& profile) {
    xQueueSend(_q_haptic_in, &profile, (TickType_t)0);
};




bool FocThread::get_angle_event(AngleEvt* evt) {
    return xQueueReceive(_q_angleevt_out, evt, (TickType_t)0);
};



uint16_t FocThread::pass_actual_pos(){

    uint16_t pointer = (foc_thread.get_motor_angle() / 6.283185307179586f) * 360;
    pointer %= 360; // Ensure pointer value is within the range [0, 360)
    return pointer;
}

uint16_t FocThread::pass_cur_pos(){
    // Relaxed load: safe cross-core read of atomic shadow; no mutex needed.
    return _a_cur_pos.load(std::memory_order_relaxed);
}

uint16_t FocThread::pass_start_pos(){
    return _a_start_pos.load(std::memory_order_relaxed);
}

uint16_t FocThread::pass_end_pos(){
    return _a_end_pos.load(std::memory_order_relaxed);
}

uint16_t FocThread::pass_last_pos(){
    return haptic.haptic_state.last_pos;
}

bool FocThread::pass_at_limit(){
    return haptic.haptic_state.atLimit;
}





float FocThread::get_motor_angle() {
    return motor.shaft_angle;
};

float FocThread::get_motor_velocity() {
    return motor.shaft_velocity;
};

void FocThread::handleMessage() {
    String* message = nullptr;
    if (xQueueReceive(_q_motor_in, &message, (TickType_t)0)) {
        if (message!=nullptr) {
            Serial.println("Received and handling message: "+*message);
            commander.handleMessage(message);
            StringMessage smsg(message, StringMessageType::STRING_MESSAGE_MOTOR);
            com_thread.put_string_message(smsg); // message String* is returned to comms thread, where it is deleted if necessary
        }
    }
};


void FocThread::handleHapticConfig() {
    DetentProfile profile;
    if (xQueueReceive(_q_haptic_in, &profile, (TickType_t)0)) {
        // apply haptic config to motor
        haptic.haptic_state = HapticState(profile);
        // Keep atomic shadows consistent after full profile replacement.
        _a_cur_pos.store(haptic.haptic_state.current_pos, std::memory_order_relaxed);
        _a_start_pos.store(haptic.haptic_state.detent_profile.start_pos, std::memory_order_relaxed);
        _a_end_pos.store(haptic.haptic_state.detent_profile.end_pos, std::memory_order_relaxed);
    }
};



void FocThread::setCalibration(MotorCalibration& cal){
    haptic.motor->zero_electric_angle = cal.zero_angle;
    haptic.motor->sensor_direction = cal.direction==0 ? Direction::UNKNOWN : ( cal.direction==1 ? Direction::CW : Direction::CCW);
};



