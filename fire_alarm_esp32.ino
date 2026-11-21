/*
 * IoT Fire Alarm System with SMS Alert
 * Board  : ESP32 DevKit V1
 * Sensors: MQ-2 (smoke), IR flame sensor, DHT11 (temperature)
 * GSM    : SIM800L (SMS over 2G network)
 * Library: "DHT sensor library" by Adafruit (+ Adafruit Unified Sensor)
 */
#include <DHT.h>

// ---------- USER SETTINGS ----------
const char PHONE_NUMBER[] = "+91XXXXXXXXXX";  // <-- put your number with country code
const int  SMOKE_THRESHOLD = 1800;            // ADC value (0-4095), calibrate for your setup
const float TEMP_THRESHOLD = 50.0;            // degrees C
const unsigned long SMS_COOLDOWN = 60000;     // min gap between alert SMS (ms)
const unsigned long CLEAR_AFTER  = 15000;     // safe for this long -> "all clear" SMS
// -----------------------------------

#define MQ2_PIN     34
#define FLAME_PIN   27
#define DHT_PIN      4
#define BUZZER_PIN  26
#define LED_RED     25
#define LED_GREEN   33
#define TEST_BTN    14
#define SIM_RX      16   // ESP32 RX2  <- SIM800L TXD
#define SIM_TX      17   // ESP32 TX2  -> SIM800L RXD
#define SIM_RST      5

#define DHTTYPE DHT11
DHT dht(DHT_PIN, DHTTYPE);
HardwareSerial sim(2);

bool alarmActive = false;
unsigned long lastSMS = 0, safeSince = 0;

bool waitFor(const char* expected, uint32_t timeout) {
  String buf; uint32_t t = millis();
  while (millis() - t < timeout) {
    while (sim.available()) buf += (char)sim.read();
    if (buf.indexOf(expected) >= 0) return true;
  }
  return false;
}

bool sendAT(const char* cmd, const char* exp = "OK", uint32_t to = 2000) {
  sim.println(cmd);
  return waitFor(exp, to);
}

bool sendSMS(const String& msg) {
  if (!sendAT("AT+CMGF=1")) return false;           // text mode
  sim.print("AT+CMGS=\""); sim.print(PHONE_NUMBER); sim.println("\"");
  if (!waitFor(">", 5000)) return false;
  sim.print(msg);
  sim.write(26);                                    // Ctrl+Z = send
  bool ok = waitFor("+CMGS:", 15000);
  Serial.println(ok ? "SMS sent" : "SMS FAILED");
  return ok;
}

void initGSM() {
  pinMode(SIM_RST, OUTPUT);
  digitalWrite(SIM_RST, LOW);  delay(200);
  digitalWrite(SIM_RST, HIGH); delay(3000);
  sim.begin(9600, SERIAL_8N1, SIM_RX, SIM_TX);
  for (int i = 0; i < 10 && !sendAT("AT"); i++) delay(500);
  sendAT("ATE0");
  sendAT("AT+CPIN?", "READY", 3000);
  for (int i = 0; i < 20 && !sendAT("AT+CREG?", "+CREG: 0,1", 1500); i++) delay(1000);
  sendAT("AT+CMGF=1");
  Serial.println("GSM ready");
}

int readSmoke() {                                   // averaged ADC reading
  long s = 0;
  for (int i = 0; i < 10; i++) { s += analogRead(MQ2_PIN); delay(5); }
  return s / 10;
}

void setAlarm(bool on) {
  alarmActive = on;
  digitalWrite(BUZZER_PIN, on);
  digitalWrite(LED_RED, on);
  digitalWrite(LED_GREEN, !on);
}

void setup() {
  Serial.begin(115200);
  pinMode(FLAME_PIN, INPUT);
  pinMode(TEST_BTN, INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(LED_RED, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  setAlarm(false);
  dht.begin();
  Serial.println("Warming up MQ-2 (30 s)...");
  digitalWrite(LED_RED, HIGH); digitalWrite(LED_GREEN, HIGH);
  delay(30000);
  initGSM();
  setAlarm(false);
  sendSMS("Fire alarm system is ONLINE and monitoring.");
}

void loop() {
  int   smoke = readSmoke();
  bool  flame = (digitalRead(FLAME_PIN) == LOW);    // LOW = flame detected
  float temp  = dht.readTemperature();
  bool  test  = (digitalRead(TEST_BTN) == LOW);
  bool  hotTemp = !isnan(temp) && temp >= TEMP_THRESHOLD;
  bool  smokeHigh = smoke >= SMOKE_THRESHOLD;

  Serial.printf("Smoke:%d  Flame:%d  Temp:%.1fC\n", smoke, flame, temp);

  if (flame || smokeHigh || hotTemp || test) {
    safeSince = 0;
    if (!alarmActive) setAlarm(true);
    if (millis() - lastSMS > SMS_COOLDOWN || lastSMS == 0) {
      String m = test ? "TEST: " : "FIRE ALERT! ";
      if (flame)     m += "Flame detected. ";
      if (smokeHigh) m += "High smoke/gas (" + String(smoke) + "). ";
      if (hotTemp)   m += "Temp " + String(temp, 1) + "C. ";
      m += "Check immediately!";
      sendSMS(m);
      lastSMS = millis();
    }
  } else if (alarmActive) {
    if (safeSince == 0) safeSince = millis();
    if (millis() - safeSince > CLEAR_AFTER) {
      setAlarm(false);
      sendSMS("All clear: fire/smoke no longer detected.");
      lastSMS = 0;
    }
  }
  delay(500);
}
