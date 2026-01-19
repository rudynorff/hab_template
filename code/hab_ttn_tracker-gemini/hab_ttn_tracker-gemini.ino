




// static const PROGMEM u1_t NWKSKEY[16] = { 0x83, 0x8C, 0x71, 0xD2, 0x0B, 0xF8, 0x68, 0x08, 0x2C, 0xAB, 0x54, 0x69, 0xDD, 0x59, 0x81, 0x21 };
// static const PROGMEM u1_t APPSKEY[16] = { 0x2F, 0x0B, 0x5A, 0x6F, 0xB7, 0xD8, 0x92, 0x04, 0xB3, 0x7F, 0xF4, 0xDE, 0x90, 0xB9, 0x3C, 0x60 };
// static const u4_t DEVADDR = 0x260B6742; 
#include <lmic.h>
#include <hal/hal.h>
#include <SPI.h>
#include <TinyGPS++.h>
#include <axp20x.h>
#include <Wire.h>

// --- TTN ABP Keys ---
static const PROGMEM u1_t NWKSKEY[16] = { 0x83, 0x8C, 0x71, 0xD2, 0x0B, 0xF8, 0x68, 0x08, 0x2C, 0xAB, 0x54, 0x69, 0xDD, 0x59, 0x81, 0x21 };
static const PROGMEM u1_t APPSKEY[16] = { 0x2F, 0x0B, 0x5A, 0x6F, 0xB7, 0xD8, 0x92, 0x04, 0xB3, 0x7F, 0xF4, 0xDE, 0x90, 0xB9, 0x3C, 0x60 };
static const u4_t DEVADDR = 0x260B6742; 

void os_getArtEui (u1_t* buf) { }
void os_getDevEui (u1_t* buf) { }
void os_getDevKey (u1_t* buf) { }

const lmic_pinmap lmic_pins = {
    .nss = 18,
    .rxtx = LMIC_UNUSED_PIN,
    .rst = 23,
    .dio = {26, 33, 32},
};

TinyGPSPlus gps;
HardwareSerial GPS_Serial(1);
AXP20X_Class axp;
static osjob_t sendjob;

void setGPS_FlightMode() {
  // UBX-CFG-NAV5: Airborne < 2g
  byte flightmode[] = {0xB5, 0x62, 0x06, 0x24, 0x24, 0x00, 0xFF, 0xFF, 0x06, 0x03, 0x00, 0x00, 0x00, 0x00, 0x10, 0x27, 0x00, 0x00, 0x05, 0x00, 0xFA, 0x00, 0xFA, 0x00, 0x64, 0x00, 0x2C, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x16, 0xDC};
  GPS_Serial.write(flightmode, sizeof(flightmode));
}

void onEvent (ev_t ev) {
    if (ev == EV_TXCOMPLETE) {
        // Schedule next transmission in 120 seconds (2 minutes)
        os_setTimedCallback(&sendjob, os_getTime()+sec2osticks(120), do_send);
    }
}

void do_send(osjob_t* j){
    if (LMIC.opmode & OP_TXRXPEND) return;
    if (!gps.location.isValid()) {
        os_setTimedCallback(&sendjob, os_getTime()+sec2osticks(10), do_send);
        Serial.println(F("No GPS lock. Re-trying in 10s..."));
        return;
    }

    union {
        float f;
        uint8_t b[4];
    } latUnion, lonUnion;

    latUnion.f = (float)gps.location.lat();
    lonUnion.f = (float)gps.location.lng();
    uint16_t alt = (uint16_t)gps.altitude.meters();
    uint8_t sats = (uint8_t)gps.satellites.value();
    uint8_t spd = (uint8_t)gps.speed.kmph();

    // Increase to 12 bytes
    byte payload[12];
    payload[0] = latUnion.b[0]; payload[1] = latUnion.b[1]; 
    payload[2] = latUnion.b[2]; payload[3] = latUnion.b[3];
    
    payload[4] = lonUnion.b[0]; payload[5] = lonUnion.b[1]; 
    payload[6] = lonUnion.b[2]; payload[7] = lonUnion.b[3];
    
    payload[8] = (alt >> 8) & 0xFF;
    payload[9] = alt & 0xFF;
    
    payload[10] = sats; // New position
    payload[11] = spd;  // New position

    LMIC_setTxData2(1, payload, sizeof(payload), 0);
    Serial.println(F("12-byte packet queued"));
}

void setup() {
    Serial.begin(115200);
    Wire.begin(21, 22);
    if (!axp.begin(Wire, AXP192_SLAVE_ADDRESS)) {
        axp.setPowerOutPut(AXP192_LDO2, AXP202_ON); // GPS
        axp.setPowerOutPut(AXP192_LDO3, AXP202_ON); // LoRa
        axp.setPowerOutPut(AXP192_DCDC1, AXP202_ON); // OLED/Header
    }

    GPS_Serial.begin(9600, SERIAL_8N1, 34, 12);
    delay(1000);
    setGPS_FlightMode();

    os_init();
    LMIC_reset();
    
    uint8_t appskey[sizeof(APPSKEY)];
    uint8_t nwkskey[sizeof(NWKSKEY)];
    memcpy_P(appskey, APPSKEY, sizeof(APPSKEY));
    memcpy_P(nwkskey, NWKSKEY, sizeof(NWKSKEY));
    LMIC_setSession (0x1, DEVADDR, nwkskey, appskey);

    LMIC_setupChannel(0, 868100000, DR_RANGE_MAP(DR_SF12, DR_SF7), BAND_CENTI);
    LMIC_setLinkCheckMode(0);
    LMIC_setAdrMode(0); 
    LMIC.datarate = DR_SF7; 

    do_send(&sendjob);
}

void loop() {
    while (GPS_Serial.available() > 0) {
        char c = GPS_Serial.read();
        //Serial.write(c); // Uncomment for raw NMEA debugging
        gps.encode(c);
    }
    os_runloop_once();
}