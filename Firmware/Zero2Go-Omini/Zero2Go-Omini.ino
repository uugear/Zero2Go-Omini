/**
 * Firmware for Zero2Go Omini 
 * 
 * Version: 1.18
 */
#include <core_timers.h>
#include <analogComp.h>
#include <avr/sleep.h>
#include <util/delay.h>
#include <WireS.h>
#include <EEPROM.h>


#define PIN_BUTTON      2             // pin to button
#define PIN_BOOST_DIS   10            // pin to - lead of blue LED
#define PIN_BULK_EN     9             // pin to + lead of green LED
#define PIN_SYSTEM_UP   1             // pin to + lead of red LED
#define PIN_RPI_TXD     0             // pin to Raspberry Pi's TXD
#define PIN_CHANNEL_A   A3            // pin to ADC3
#define PIN_CHANNEL_B   A5            // pin to ADC5
#define PIN_CHANNEL_C   A7            // pin to ADC7
#define PIN_SDA         4             // pin to SDA for I2C
#define PIN_SCL         6             // pin to SCL for I2C

#define I2C_ID          0             // firmware id
#define I2C_CHANNEL_AI  1             // integer part for voltage of channel A
#define I2C_CHANNEL_AD  2             // decimal part (x100) for voltage of channel A
#define I2C_CHANNEL_BI  3             // integer part for voltage of channel B
#define I2C_CHANNEL_BD  4             // decimal part (x100) for voltage of channel B
#define I2C_CHANNEL_CI  5             // integer part for voltage of channel C
#define I2C_CHANNEL_CD  6             // decimal part (x100) for voltage of channel C
#define I2C_BULK_BOOST  7             // working mode: bulk or boost
#define I2C_LV_SHUTDOWN 8             // 1 if system was shutdown by low voltage, other wise 0

#define I2C_CONF_ADDRESS          9   // I2C slave address: defaul=0x29
#define I2C_CONF_DEFAULT_ON       10  // turn on RPi when power is connected: 1=yes, 0=no
#define I2C_CONF_BLINK_INTERVAL   11  // blink interval: 9=8s,8=4s,7=2s,6=1s
#define I2C_CONF_LOW_VOLTAGE      12  // low voltage threshold (x10), 255=disabled
#define I2C_CONF_BULK_ALWAYS_ON   13  // always enable bulk converter: 1=yes, 0=no
#define I2C_CONF_POWER_CUT_DELAY  14  // the delay (x10) before power cut: default=50 (5 sec)
#define I2C_CONF_RECOVERY_VOLTAGE 15  // voltage (x10) that triggers recovery, 255=disabled
#define I2C_CONF_WATCHDOG_TIMEOUT 16  // set watchdog timeout, reset watchdog, 0=disabled

#define I2C_REG_COUNT   17            // number of I2C registers

volatile byte i2cReg[I2C_REG_COUNT];

volatile char i2cIndex = 0;

volatile boolean buttonPressed = false;

volatile boolean bulkOrBoost = true;  // true=bulk, false=boost

volatile boolean powerIsOn = false;

volatile boolean listenToTxd = false;

volatile boolean turningOff = false;

volatile boolean forcePowerCut = false;

volatile boolean wakeupByWatchdog = false;

volatile unsigned long buttonStateChangeTime = 0;

volatile unsigned long voltageQueryTime = 0;

volatile int watchdogCounter = 0;

void timer1_enable();
void timer2_enable();
void cutPower();
void powerOn();
void redLightOn();
void suggestShutdown();
void redLightOff();
void initializeRegisters();
void updateRegister(int index, byte value);
float getVoltage(int pin);
int getIntegerPart(float v);
int getDecimalPart(float v);
unsigned int getPowerCutPreloadTimer();
void receiveEvent(int count);
boolean addressEvent(uint16_t slaveAddress, uint8_t startCount);
void requestEvent();
void comparatorStatusChanged();
void sleep();
void resetWatchdog();

void setup() {

  // initialize pin states and make sure power is cut
  digitalWrite(PIN_BOOST_DIS, 1);   // disable boost engine ASAP
  pinMode(PIN_BUTTON, INPUT_PULLUP);
  pinMode(PIN_BOOST_DIS, OUTPUT);
  pinMode(PIN_BULK_EN, OUTPUT);
  pinMode(PIN_SYSTEM_UP, INPUT);
  pinMode(PIN_RPI_TXD, INPUT);
  pinMode(PIN_CHANNEL_A, INPUT);
  pinMode(PIN_CHANNEL_B, INPUT);
  pinMode(PIN_CHANNEL_C, INPUT);
  pinMode(PIN_SDA, INPUT_PULLUP);
  pinMode(PIN_SCL, INPUT_PULLUP);
  cutPower();

  // use internal 2.2V reference
  analogReference(INTERNAL2V2);

  // initlize registers
  initializeRegisters();

  // i2c initialization
  Wire.begin((i2cReg[I2C_CONF_ADDRESS] <= 0x07 || i2cReg[I2C_CONF_ADDRESS] >= 0x78) ? 0x29 : i2cReg[I2C_CONF_ADDRESS]);
  Wire.onAddrReceive(addressEvent);
  Wire.onReceive(receiveEvent);
  Wire.onRequest(requestEvent);

  // disable global interrupts
  cli();

  // enable pin change interrupts 
  GIMSK = _BV (PCIE1);
  PCMSK1 = _BV (PCINT10) | _BV (PCINT9) | _BV (PCINT8);
  PCMSK0 = 0;

  // initialize Timer1
  TCCR1A = 0;    // set entire TCCR1A register to 0
  TCCR1B = 0;    // set entire TCCR1B register to 0

  // enable Timer1
  timer1_enable();

  // enable watchdog
  if (i2cReg[I2C_CONF_WATCHDOG_TIMEOUT]) {
    resetWatchdog();
  }

  // enable comparator
  analogComparator.setOn(INTERNAL_REFERENCE, AIN1);
  analogComparator.enableInterrupt(comparatorStatusChanged, CHANGE);
  comparatorStatusChanged();

  // enable all interrupts
  sei();

  // power on or sleep
  bool defaultOn = (i2cReg[I2C_CONF_DEFAULT_ON] == 1);
  if (defaultOn) {
    powerOn();  // power on directly
  } else {
    sleep();    // sleep and wait for button action
  }

}


void loop() {
  unsigned long curTime = micros();
  if (voltageQueryTime > curTime || curTime - voltageQueryTime >= 1000000) {
    voltageQueryTime = curTime;

    // read input voltages
    float va = getVoltage(PIN_CHANNEL_A);
    i2cReg[I2C_CHANNEL_AI] = getIntegerPart(va);
    i2cReg[I2C_CHANNEL_AD] = getDecimalPart(va);
  
    float vb = getVoltage(PIN_CHANNEL_B);
    i2cReg[I2C_CHANNEL_BI] = getIntegerPart(vb);
    i2cReg[I2C_CHANNEL_BD] = getDecimalPart(vb);
  
    float vc = getVoltage(PIN_CHANNEL_C);
    i2cReg[I2C_CHANNEL_CI] = getIntegerPart(vc);
    i2cReg[I2C_CHANNEL_CD] = getDecimalPart(vc);
  
    // detect low voltage
    if (powerIsOn && listenToTxd && i2cReg[I2C_LV_SHUTDOWN] == 0 && i2cReg[I2C_CONF_LOW_VOLTAGE] != 255) {
      float vmax = max(max(va, vb), vc);
      float vlow = ((float)i2cReg[I2C_CONF_LOW_VOLTAGE]) / 10;
      if (vmax < vlow) {  // all input voltages are below the low voltage threshold
        updateRegister(I2C_LV_SHUTDOWN, 1);
        suggestShutdown();
      }
    }
  }
}


// initialize the registers and synchronize with EEPROM
void initializeRegisters() {
  i2cReg[I2C_ID] = 0x76;
  i2cReg[I2C_CHANNEL_AI] = 0;
  i2cReg[I2C_CHANNEL_AD] = 0;
  i2cReg[I2C_CHANNEL_BI] = 0;
  i2cReg[I2C_CHANNEL_BD] = 0;
  i2cReg[I2C_CHANNEL_CI] = 0;
  i2cReg[I2C_CHANNEL_CD] = 0;
  i2cReg[I2C_BULK_BOOST] = 1;
  i2cReg[I2C_LV_SHUTDOWN] = 0;
  
  i2cReg[I2C_CONF_ADDRESS] = 0x29;
  i2cReg[I2C_CONF_DEFAULT_ON] = 0;
  i2cReg[I2C_CONF_BLINK_INTERVAL] = 8;
  i2cReg[I2C_CONF_LOW_VOLTAGE] = 255;
  i2cReg[I2C_CONF_BULK_ALWAYS_ON] = 0;
  i2cReg[I2C_CONF_POWER_CUT_DELAY] = 50;
  i2cReg[I2C_CONF_RECOVERY_VOLTAGE] = 255;

  i2cReg[I2C_CONF_WATCHDOG_TIMEOUT] = 0;

  // make sure product name is stored
  EEPROM.update(0, 'Z');
  EEPROM.update(1, 'e');
  EEPROM.update(2, 'r');
  EEPROM.update(3, 'o');
  EEPROM.update(4, '2');
  EEPROM.update(5, 'G');
  EEPROM.update(6, 'o');
  EEPROM.update(7, 0);
  EEPROM.update(8, 0);

  // synchronize configuration with EEPROM
  for (int i = I2C_CONF_ADDRESS; i < I2C_REG_COUNT; i ++) {
    byte val = EEPROM.read(i);
    if (val == 255) {
      EEPROM.update(i, i2cReg[i]);
    } else {
      i2cReg[i] = val;
    } 
  }
}


void watchdog_enable() {
  cli();
  WDTCSR |= _BV(WDIE);
  byte wdp = (i2cReg[I2C_CONF_BLINK_INTERVAL] > 9 ? 8 : i2cReg[I2C_CONF_BLINK_INTERVAL]);
  wdp = (((wdp & B00001000) << 2) | (wdp & B11110111));
  WDTCSR |= wdp;
  sei();
}


void watchdog_disable() {
  WDTCSR = 0;
}


void timer1_enable() {
  // set entire TCCR1A and TCCR1B register to 0
  TCCR1A = 0;
  TCCR1B = 0;
  
  // set 1024 prescaler
  bitSet(TCCR1B, CS12);
  bitSet(TCCR1B, CS10);

  // clear overflow interrupt flag
  bitSet(TIFR1, TOV1);

  // set timer counter
  TCNT1 = getPowerCutPreloadTimer();

  // enable Timer1 overflow interrupt
  bitSet(TIMSK1, TOIE1);
}


void timer1_disable() {
  // disable Timer1 overflow interrupt
  bitClear(TIMSK1, TOIE1);
}

void timer2_disable() {
  // disable Timer2 overflow interrupt:
  bitClear(TIMSK2, TOIE2);
}

void sleep() {
  timer1_disable();                       // disable Timer1
  timer2_disable();
  ADCSRA &= ~_BV(ADEN);                   // ADC off
  set_sleep_mode(SLEEP_MODE_PWR_DOWN);    // power-down mode 
  watchdog_enable();                      // enable watchdog
  sleep_enable();                         // sets the Sleep Enable bit in the MCUCR Register (SE BIT)
  sei();                                  // enable interrupts

  wakeupByWatchdog = true;
  do {
    sleep_cpu();                          // sleep
    if (wakeupByWatchdog) {               // wake up by watch dog
      // blink red LED
      redLightOn();
      redLightOn();
      redLightOn();
      redLightOn();
      redLightOn();
      redLightOn();
      redLightOff();
      // check input voltages if shutdown because of low voltage, and recovery voltage is set
      // will skip checking I2C_LV_SHUTDOWN if I2C_CONF_LOW_VOLTAGE is set to 0xFF
      if ((i2cReg[I2C_LV_SHUTDOWN] == 1 || i2cReg[I2C_CONF_LOW_VOLTAGE] == 255) && i2cReg[I2C_CONF_RECOVERY_VOLTAGE] != 255) {        
        ADCSRA |= _BV(ADEN);
        float va = getVoltage(PIN_CHANNEL_A);
        float vb = getVoltage(PIN_CHANNEL_B);
        float vc = getVoltage(PIN_CHANNEL_C);
        ADCSRA &= ~_BV(ADEN);
        float vmax = max(max(va, vb), vc);
        float vrec = ((float)i2cReg[I2C_CONF_RECOVERY_VOLTAGE]) / 10;
        if (vmax >= vrec) {
          wakeupByWatchdog = false;       // recovery from low voltage shutdown
        }
      }
    }
  } while (wakeupByWatchdog);             // quit sleeping if wake up by button

  cli();                                  // disable interrupts
  sleep_disable();                        // clear SE bit
  watchdog_disable();                     // disable watchdog
  ADCSRA |= _BV(ADEN);                    // ADC on
  timer1_enable();                        // enable Timer1
  sei();                                  // enable interrupts

  pinMode(PIN_SDA, INPUT_PULLUP);         // explicitly specify SDA pin mode before waking up
  pinMode(PIN_SCL, INPUT_PULLUP);         // explicitly specify SCL pin mode before waking up

  // tap the button to wake up
  listenToTxd = false;
  turningOff = false;
  buttonPressed = true;
  powerOn();
  TCNT1 = getPowerCutPreloadTimer();
}


void cutPower() {
  powerIsOn = false;
  digitalWrite(PIN_BULK_EN, 0);
  digitalWrite(PIN_BOOST_DIS, 1);
}


// suggest Raspberry Pi to shutdown
void suggestShutdown() {
  PCMSK1 &= ~_BV (PCINT10);
  pinMode(PIN_BUTTON, OUTPUT);
  digitalWrite(PIN_BUTTON, 1);
  digitalWrite(PIN_BUTTON, 0);
  pinMode(PIN_BUTTON, INPUT_PULLUP);
  PCMSK1 |= _BV (PCINT10);
}


void powerOn() {
  powerIsOn = true;
  if (bulkOrBoost == true) {
    digitalWrite(PIN_BULK_EN, 1);
    digitalWrite(PIN_BOOST_DIS, 1);
  } else {
    digitalWrite(PIN_BULK_EN, i2cReg[I2C_CONF_BULK_ALWAYS_ON] == 1);
    digitalWrite(PIN_BOOST_DIS, 0);
  }
}


void redLightOn() {
  PCMSK1 &= ~_BV (PCINT9);
  pinMode(PIN_SYSTEM_UP, OUTPUT);
  digitalWrite(PIN_SYSTEM_UP, 1);
  PCMSK1 |= _BV (PCINT9);
}


void redLightOff() {
  PCMSK1 &= ~_BV (PCINT9);
  digitalWrite(PIN_SYSTEM_UP, 0);
  pinMode(PIN_SYSTEM_UP, INPUT);
  PCMSK1 |= _BV (PCINT9);
}


float getVoltage(int pin) {
  return 0.0365591398 * analogRead(pin);  // 17*2.2/1023~=0.03656
}


int getIntegerPart(float v) {
  return (int)v;  
}


int getDecimalPart(float v) {
  return (int)((v - getIntegerPart(v)) * 100);
}


// get the preload timer value for power cut
unsigned int getPowerCutPreloadTimer() {
  return 65535 - 781 * ((i2cReg[I2C_CONF_POWER_CUT_DELAY] > 83) ? 50 : i2cReg[I2C_CONF_POWER_CUT_DELAY]);
}


// receives a sequence of start|address|direction bit from i2c master
boolean addressEvent(uint16_t slaveAddress, uint8_t startCount) {
  if (startCount > 0 && Wire.available()) {
    i2cIndex = Wire.read();
  }
  return true;
}


// receives a sequence of data from i2c master
void receiveEvent(int count) {
  if (Wire.available()) {
    i2cIndex = Wire.read();
    if (Wire.available()) {
      updateRegister(i2cIndex, Wire.read());
    }
  }
}


// i2c master requests data from this device
void requestEvent() {
  float v = 0.0;
  switch (i2cIndex) {
    case I2C_CHANNEL_AI:
      v = getVoltage(PIN_CHANNEL_A);
      updateRegister(I2C_CHANNEL_AI, getIntegerPart(v));
      updateRegister(I2C_CHANNEL_AD, getDecimalPart(v));
      break;
    case I2C_CHANNEL_BI:
      v = getVoltage(PIN_CHANNEL_B);
      updateRegister(I2C_CHANNEL_BI, getIntegerPart(v));
      updateRegister(I2C_CHANNEL_BD, getDecimalPart(v));
      break;
    case I2C_CHANNEL_CI:
      v = getVoltage(PIN_CHANNEL_C);
      updateRegister(I2C_CHANNEL_CI, getIntegerPart(v));
      updateRegister(I2C_CHANNEL_CD, getDecimalPart(v));
      break;
  }
  Wire.write(i2cReg[i2cIndex]);
}


// watchdog interrupt routine
ISR (WDT_vect) {
  // no need to do anything here
}


// pin state change interrupt routine 
ISR (PCINT1_vect) {
  // debounce
  unsigned long prevTime = buttonStateChangeTime;
  buttonStateChangeTime = micros();
  if (buttonStateChangeTime - prevTime < 10) {
    return;
  }
    
  wakeupByWatchdog = false;
  
  if (forcePowerCut) {
    forcePowerCut = false;
    sleep();
  } else {
    if (digitalRead(PIN_BUTTON) == 0) { // button is pressed
      if (!buttonPressed && TCNT1 - getPowerCutPreloadTimer() > 5000) {
        buttonPressed = true;
        powerOn();
      }
      TCNT1 = getPowerCutPreloadTimer();
    } else {  // button is released
      buttonPressed = false;
    }
    
    if (digitalRead(PIN_SYSTEM_UP) == 1)  {
      // clear the low-voltage shutdown flag when sys_up signal arrives
      if (listenToTxd == false) {
        updateRegister(I2C_LV_SHUTDOWN, 0);
      }
      // start listen to TXD pin
      listenToTxd = true;
    }
    
    if (listenToTxd && digitalRead(PIN_RPI_TXD) == 0) {
      turningOff = true;
      TCNT1 = getPowerCutPreloadTimer();
    }
  }
}


// timer1 overflow interrupt routine
ISR (TIM1_OVF_vect) {
  TCNT1 = getPowerCutPreloadTimer();
  if (buttonPressed && digitalRead(PIN_BUTTON) == 0) {
    forcePowerCut = true;
    cutPower();
  }
  if (turningOff) {
    cutPower();
    sleep();
  }
}


// timer2 overflow interrupt routine
ISR (TIM2_OVF_vect) {
  // overflow in 8s
  TCNT2 = 3035;
  watchdogCounter++;
  // minimum watchdog timeout is 1m20s
  if (watchdogCounter >= (i2cReg[I2C_CONF_WATCHDOG_TIMEOUT] + 9)){
    // watchdog timeout, trigger a reboot
    watchdogCounter = 0;
    cutPower();
    _delay_ms (5000);
    powerOn();
  }
}


// analog comparator interrupt routine
void comparatorStatusChanged() {
  bulkOrBoost = ((ACSR0A & (1<<ACO)) == 0);
  if (powerIsOn) {
    digitalWrite(PIN_BOOST_DIS, bulkOrBoost);
    digitalWrite(PIN_BULK_EN, bulkOrBoost || i2cReg[I2C_CONF_BULK_ALWAYS_ON] == 1);
  }
  updateRegister(I2C_BULK_BOOST, bulkOrBoost ? 1 : 0);  
}

void resetWatchdog() {
  watchdogCounter = 0;
  // overflow in 8s
  TCNT2 = 3035;

  // initialize Timer2
  TCCR2A = 0;    // set entire TCCR1A register to 0
  TCCR2B = 0;    // set entire TCCR1B register to 0

  if (i2cReg[I2C_CONF_WATCHDOG_TIMEOUT]) {
    // enable Timer2 overflow interrupt:
    bitSet(TIMSK2, TOIE2);
    // set 1024 prescaler
    bitSet(TCCR2B, CS12);
    bitSet(TCCR2B, CS10);
  }

  redLightOn();
  _delay_ms(50);
  redLightOff();
}

// update I2C register, save to EEPROM if it is configuration
void updateRegister(int index, byte value) {
  if (i2cReg[index] != value) {
    i2cReg[index] = value;
    if (index >= I2C_CONF_ADDRESS) {
      EEPROM.update(index, value);
    }
  }
  if (index == I2C_CONF_BULK_ALWAYS_ON) {
    powerOn();
  }
  if (index == I2C_CONF_WATCHDOG_TIMEOUT) {
    resetWatchdog();
  }
}
