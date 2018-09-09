/**
 * Firmware for Zero2Go Omini 
 * 
 * Version: 1.12
 */
#include <core_timers.h>
#include <analogComp.h>
#include <avr/sleep.h>
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

#define I2C_REG_COUNT   16            // number of I2C registers

volatile byte i2cReg[I2C_REG_COUNT];

volatile char i2cIndex = 0;

volatile boolean buttonPressed = false;

volatile boolean bulkOrBoost = true;  // true=bulk, false=boost

volatile boolean powerIsOn = false;

volatile boolean listenToTxd = false;

volatile boolean turningOff = false;

volatile boolean forcePowerCut = false;

volatile boolean wakeupByWatchdog = false;

unsigned long buttonStateChangeTime = 0;


void setup() {

  // initialize pin states and make sure power is cut
  pinMode(PIN_BUTTON, INPUT_PULLUP);
  pinMode(PIN_BOOST_DIS, OUTPUT);
  pinMode(PIN_BULK_EN, OUTPUT);
  pinMode(PIN_SYSTEM_UP, INPUT);
  pinMode(PIN_RPI_TXD, INPUT);
  pinMode(PIN_CHANNEL_A, INPUT);
  pinMode(PIN_CHANNEL_B, INPUT);
  pinMode(PIN_CHANNEL_C, INPUT);
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

  // enable Timer1 overflow interrupt:
  bitSet(TIMSK1, TOIE1);

  // set 1024 prescaler
  bitSet(TCCR1B, CS12);
  bitSet(TCCR1B, CS10);

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
  
  delay(1000);
}


// initialize the registers and synchronize with EEPROM
void initializeRegisters() {
  i2cReg[I2C_ID] = 0x70;
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


void sleep() {
  ADCSRA &= ~_BV(ADEN);                   // ADC off
  set_sleep_mode(SLEEP_MODE_PWR_DOWN);    // power-down mode 
  watchdog_enable();                      // enable watchdog
  sleep_enable();                         // sets the Sleep Enable bit in the MCUCR Register (SE BIT)
  sei();                                  // enable interrupts

  wakeupByWatchdog = true;
  do {
    sleep_cpu();                          // sleep
    if (wakeupByWatchdog) {               // wake up by watch dog
      redLightOn();                       // blink red LED (very short)
      delay(1);
      redLightOff();
      // check input voltages if shutdown because of low voltage, and recovery voltage is set
      if (i2cReg[I2C_LV_SHUTDOWN] == 1 && i2cReg[I2C_CONF_RECOVERY_VOLTAGE] != 255) {        
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
  sei();                                  // enable interrupts

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
  delay(300);
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


// analog comparator interrupt routine
void comparatorStatusChanged() {
  bulkOrBoost = ((ACSR0A & (1<<ACO)) == 0);
  updateRegister(I2C_BULK_BOOST, bulkOrBoost ? 1 : 0);
  if (powerIsOn) {
    powerOn();
  }
}


// update I2C register, save to EEPROM if it is configuration
void updateRegister(int index, byte value) {
  i2cReg[index] = value;
  if (index >= I2C_CONF_ADDRESS) {
    EEPROM.update(index, value);
  }
  if (index == I2C_CONF_BULK_ALWAYS_ON) {
    powerOn();
  }
}
