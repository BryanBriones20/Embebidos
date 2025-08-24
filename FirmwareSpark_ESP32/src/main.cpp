#include <Wire.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Adafruit_PWMServoDriver.h>
#include <FirebaseESP32.h>

// ===== WiFi =====
const char* ssid     = "DanielXS";
const char* password = "holajaja";

// ===== Firebase (mantener como está) =====
#define FIREBASE_HOST "se-sparkpp-default-rtdb.firebaseio.com"
#define FIREBASE_AUTH "ZKrSbPVQyeNnB9PQdQTIuyQDy9QGdmpnVL1mU4Gj"
FirebaseData firebaseData;
FirebaseConfig fbConfig;
FirebaseAuth fbAuth;
String ultimaClasificacion = "";

// ===== PCA9685 / SERVO =====
Adafruit_PWMServoDriver pwm = Adafruit_PWMServoDriver(0x40);
#define SERVO_MIN_PULSE 150
#define SERVO_MAX_PULSE 600

static const uint8_t SERVO_COUNT = 5;
static const uint8_t SERVO_CH[SERVO_COUNT] = {0,1,2,3,4}; // 0=base,1=hombro,2=codo,3=muñeca,4=gripper

float minDeg[SERVO_COUNT] = {0,0,0,0,0};
float maxDeg[SERVO_COUNT] = {180,180,180,180,180};

volatile float targetDeg[SERVO_COUNT] = {0,0,0,0,0};
float currentDeg[SERVO_COUNT]         = {0,0,0,0,0};

float maxSpeedDPS = 180.0f;
const uint32_t CONTROL_DT_MS = 20;
uint32_t lastUpdateMs = 0;

// ===== Banda (L298N con LEDC) =====
#define IN1 25
#define IN2 26
#define ENA 14
const int BELT_PWM_CH = 4;
const int BELT_PWM_FREQ = 2500;
const int BELT_PWM_BITS = 8;
int  beltDuty = 165;          // nominal
int  beltCurrentDuty = 0;     // lo que realmente está aplicándose

inline void beltStart(int duty = -1) {
  if (duty >= 0) beltDuty = constrain(duty, 0, 255);
  digitalWrite(IN1, HIGH);
  digitalWrite(IN2, LOW);
  ledcWrite(BELT_PWM_CH, beltDuty);
  beltCurrentDuty = beltDuty;
}
inline void beltStop() {
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, LOW);
  ledcWrite(BELT_PWM_CH, 0);
  beltCurrentDuty = 0;
}

// ===== Sensor (activo-bajo) =====
#define SENSOR_PIN 27
const uint32_t DEBOUNCE_MS = 80;

// ===== Modo / Marcha =====
volatile bool running = false;    // Marcha/Paro
volatile bool modeAuto = false;   // false=manual, true=auto

// ===== HTTP =====
WebServer server(80);

// ===== Utilidades =====
inline uint16_t angleToPulse(float deg) {
  deg = fmaxf(0.0f, fminf(180.0f, deg));
  return (uint16_t)map((long)roundf(deg), 0, 180, SERVO_MIN_PULSE, SERVO_MAX_PULSE);
}
inline bool reached(float cur, float tgt, float tol=0.8f) { return fabsf(cur - tgt) <= tol; }
bool nearTargets(const uint8_t* chs, int n, float tol=0.8f) {
  for (int i=0;i<n;i++) { uint8_t ch = chs[i]; if (!reached(currentDeg[ch], targetDeg[ch], tol)) return false; }
  return true;
}
void setTarget(uint8_t ch, float deg) {
  deg = fmaxf(minDeg[ch], fminf(maxDeg[ch], deg));
  targetDeg[ch] = deg;
}
void setTargets(const uint8_t* channels, const float* degs, int n) {
  for (int i=0;i<n;i++) setTarget(channels[i], degs[i]);
}

// Control suave
void controlUpdate() {
  uint32_t now = millis();
  if (now - lastUpdateMs < CONTROL_DT_MS) return;
  float dt = (now - lastUpdateMs) / 1000.0f;
  lastUpdateMs = now;

  if (!running) return;

  float maxStep = maxSpeedDPS * dt;
  for (uint8_t i=0;i<SERVO_COUNT;i++) {
    float tgt = fmaxf(minDeg[i], fminf(maxDeg[i], targetDeg[i]));
    float cur = currentDeg[i];
    float err = tgt - cur;
    float next = cur;

    if (fabsf(err) <= maxStep) next = tgt;
    else                       next += (err > 0 ? maxStep : -maxStep);

    if (fabsf(next - cur) >= 0.05f) {
      currentDeg[i] = next;
      pwm.setPWM(SERVO_CH[i], 0, angleToPulse(next));
    }
  }
}

// ======= AUTO: Máquina de estados =======
enum AutoState {
  AUTO_TRANSITION,   // al entrar a modo AUTO: ir a "home"
  AUTO_IDLE,         // esperando objeto (sensor HIGH)
  AUTO_DETECTING,    // sensor LOW con antirrebote
  AUTO_WAIT_QR,      // <<< NUEVO: espera extra antes de leer Firebase
  AUTO_READ_FB,      // leer Firebase (clasificación + ángulos)
  AUTO_PREPICK_0,    // hombro -> 15
  AUTO_PREPICK_1,    // gripper -> 0 (cerrado)
  AUTO_PREPICK_2,    // hombro -> 22
  AUTO_PICK_0,       // base -> a0
  AUTO_PICK_1,       // codo -> a2
  AUTO_PICK_2,       // hombro -> a1
  AUTO_PICK_3,       // gripper -> a4
  AUTO_PICK_WAIT,    // espera 500 ms
  AUTO_HOME_0,       // codo -> 103
  AUTO_HOME_1,       // muñeca -> 94
  AUTO_HOME_2,       // hombro -> 22
  AUTO_HOME_3,       // gripper -> 60
  AUTO_HOME_4,       // base -> 180
  AUTO_RESUME        // reanudar banda y volver a IDLE
};

AutoState autoState = AUTO_IDLE;
uint32_t stateTs = 0;
uint32_t detectTs = 0;
bool detectLatched = false;

// Home pose
const float HOME_BASE   = 180;
const float HOME_HOMBRO = 28;
const float HOME_CODO   = 103;
const float HOME_MUNECA = 94;
const float HOME_GRIP   = 95;

// Espera extra para QR/Firebase (ajústalo si quieres)
uint32_t QR_WAIT_MS = 7000;

// Buffers Firebase
int movimiento[6] = {0,0,0,0,0,0};
String clasifActual = "";

const char* phaseString() {
  switch (autoState) {
    case AUTO_TRANSITION: return "transition";
    case AUTO_IDLE:       return "idle";
    case AUTO_DETECTING:  return "detecting";
    case AUTO_WAIT_QR:    return "classifying"; // mostrado como "Clasificando/esperando"
    case AUTO_READ_FB:    return "classifying";
    case AUTO_PREPICK_0:
    case AUTO_PREPICK_1:
    case AUTO_PREPICK_2:  return "pregrip";
    case AUTO_PICK_0:
    case AUTO_PICK_1:
    case AUTO_PICK_2:
    case AUTO_PICK_3:
    case AUTO_PICK_WAIT:  return "pickplace";
    case AUTO_HOME_0:
    case AUTO_HOME_1:
    case AUTO_HOME_2:
    case AUTO_HOME_3:
    case AUTO_HOME_4:     return "homing";
    case AUTO_RESUME:     return "idle";
  }
  return "idle";
}

bool sensorIsLow() { return digitalRead(SENSOR_PIN) == LOW; }

// Leer Firebase manteniendo %20
void leerFirebase() {
  if (!Firebase.getString(firebaseData, "/Orden%20Actual/clasificacion")) return;

  String clasif = firebaseData.stringData(); clasif.trim();
  if (clasif == "" || clasif == ultimaClasificacion) {
    clasifActual = ultimaClasificacion; // podría quedar igual si no cambió
    return;
  }

  ultimaClasificacion = clasif;
  clasif.replace(" ", "%20");
  clasifActual = clasif;

  String base = "/Clasificaciones/" + clasif + "/";
  for (int i = 0; i < 6; i++) {
    String path = base + "a" + String(i + 1);
    if (Firebase.getInt(firebaseData, path)) movimiento[i] = firebaseData.intData();
    else movimiento[i] = 0;
  }

  Serial.print("📦 FB clasif=");
  Serial.print(clasifActual);
  Serial.print(" mov=[");
  for (int i=0;i<6;i++){ Serial.print(movimiento[i]); if(i<5) Serial.print(","); }
  Serial.println("]");
}

// Avance de la máquina AUTO
void autoUpdate() {
  if (!modeAuto || !running) return;

  switch (autoState) {
    case AUTO_TRANSITION: {
      // Ir a home
      const uint8_t chs[] = {2,3,1,4,0};
      const float   degs[] = {HOME_CODO, HOME_MUNECA, HOME_HOMBRO, HOME_GRIP, HOME_BASE};
      setTargets(chs, degs, 5);
      if (nearTargets(chs, 5)) {
        autoState = AUTO_IDLE;
        beltStart(beltDuty); // en AUTO sí puede moverse
      }
    } break;

    case AUTO_IDLE: {
      if (sensorIsLow()) { detectTs = millis(); autoState = AUTO_DETECTING; }
    } break;

    case AUTO_DETECTING: {
      if (sensorIsLow()) {
        if (millis() - detectTs >= DEBOUNCE_MS) {
          beltStop();               // detener banda
          detectLatched = true;
          stateTs = millis();
          autoState = AUTO_WAIT_QR; // <<< NUEVO: espera antes de leer Firebase
        }
      } else {
        autoState = AUTO_IDLE;
      }
    } break;

    case AUTO_WAIT_QR: {
      if (millis() - stateTs >= QR_WAIT_MS) {
        autoState = AUTO_READ_FB;
      }
    } break;

    case AUTO_READ_FB: {
      leerFirebase();   // lectura (puede seguir siendo la misma si aún no cambió)
      autoState = AUTO_PREPICK_0;
    } break;

    // PRE-PICK
    case AUTO_PREPICK_0: { setTarget(1, 15); if (nearTargets((const uint8_t[]){1},1)) autoState = AUTO_PREPICK_1; } break;
    case AUTO_PREPICK_1: { setTarget(4, 0 ); if (nearTargets((const uint8_t[]){4},1)) autoState = AUTO_PREPICK_2; } break;
    case AUTO_PREPICK_2: { setTarget(1, 28); if (nearTargets((const uint8_t[]){1},1)) autoState = AUTO_PICK_0;   } break;

    // PICK & PLACE (base->a0, codo->a2, hombro->a1, gripper->a4)
    case AUTO_PICK_0: { setTarget(0, movimiento[0]); if (nearTargets((const uint8_t[]){0},1)) autoState = AUTO_PICK_1; } break;
    case AUTO_PICK_1: { setTarget(2, movimiento[2]); if (nearTargets((const uint8_t[]){2},1)) autoState = AUTO_PICK_2; } break;
    case AUTO_PICK_2: { setTarget(1, movimiento[1]); if (nearTargets((const uint8_t[]){1},1)) autoState = AUTO_PICK_3; } break;
    case AUTO_PICK_3: { setTarget(4, movimiento[4]); if (nearTargets((const uint8_t[]){4},1)) { stateTs = millis(); autoState = AUTO_PICK_WAIT; } } break;

    case AUTO_PICK_WAIT: { if (millis() - stateTs >= 500) autoState = AUTO_HOME_0; } break;

    // HOME
    case AUTO_HOME_0: { setTarget(2, 103); if (nearTargets((const uint8_t[]){2},1)) autoState = AUTO_HOME_1; } break;
    case AUTO_HOME_1: { setTarget(3, 94); if (nearTargets((const uint8_t[]){3},1)) autoState = AUTO_HOME_2; } break;
    case AUTO_HOME_2: { setTarget(1,  28); if (nearTargets((const uint8_t[]){1},1)) autoState = AUTO_HOME_3; } break;
    case AUTO_HOME_3: { setTarget(4,  95); if (nearTargets((const uint8_t[]){4},1)) autoState = AUTO_HOME_4; } break;
    case AUTO_HOME_4: { setTarget(0, 180); if (nearTargets((const uint8_t[]){0},1)) autoState = AUTO_RESUME;  } break;

    case AUTO_RESUME: {
      // reanuda banda cuando el sensor se libera (HIGH) estable
      if (sensorIsLow()) {
        // esperar liberación
      } else {
        static uint32_t freeTs = 0;
        if (freeTs == 0) freeTs = millis();
        if (millis() - freeTs >= DEBOUNCE_MS) {
          beltStart(beltDuty);
          detectLatched = false;
          freeTs = 0;
          autoState = AUTO_IDLE;
        }
      }
    } break;
  }
}

// ===== HTTP Handlers =====
void handleRoot() {
  String html =
    "<!doctype html><html><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width, initial-scale=1'/>"
    "<title>ESP32 PCA9685</title></head><body>"
    "<h3>ESP32 PCA9685 - Manual/Auto</h3>"
    "<ul>"
    "<li><code>/sliders?a1=..&a2=..&a3=..&a4=..&a5=..</code> (solo manual)</li>"
    "<li><code>/speed?dps=120</code></li>"
    "<li><code>/run?on=1|0</code></li>"
    "<li><code>/mode?auto=1|0</code></li>"
    "<li><code>/state</code></li>"
    "</ul>"
    "</body></html>";
  server.send(200, "text/html", html);
}

void handleSliders() {
  if (modeAuto) { server.send(409, "application/json", "{\"ok\":false,\"msg\":\"Modo AUTO activo\"}"); return; }
  bool any = false;
  int recv[SERVO_COUNT]; for (int i=0;i<SERVO_COUNT;i++) recv[i] = -1;

  for (uint8_t i=0;i<SERVO_COUNT;i++) {
    String key = "a" + String(i+1);
    if (server.hasArg(key)) {
      long v = server.arg(key).toInt();
      v = constrain(v, (long)minDeg[i], (long)maxDeg[i]);
      setTarget(i, (float)v);
      recv[i] = (int)v;
      any = true;
    }
  }
  if (!any) { server.send(400, "application/json", "{\"ok\":false,\"msg\":\"Faltan parametros (a1..a5)\"}"); return; }

  Serial.print("RX /sliders targets: [");
  for (int i=0;i<SERVO_COUNT;i++){ Serial.print(recv[i]); if(i<SERVO_COUNT-1) Serial.print(", "); }
  Serial.println("]");

  String json = "{\"ok\":true,\"targets\":[";
  for (int i=0;i<SERVO_COUNT;i++){ json += String((int)roundf(targetDeg[i])); if(i<SERVO_COUNT-1) json += ","; }
  json += "]}";
  server.send(200, "application/json", json);
}

void handleSpeed() {
  if (!server.hasArg("dps")) { server.send(400, "text/plain", "Falta dps"); return; }
  float dps = server.arg("dps").toFloat();
  if (dps <= 0 || dps > 720) { server.send(400, "text/plain", "Rango invalido (0<dps<=720)"); return; }
  maxSpeedDPS = dps;
  server.send(200, "text/plain", "OK");
  Serial.print("Nueva velocidad (dps): "); Serial.println(maxSpeedDPS);
}

void handleRun() {
  if (!server.hasArg("on")) { server.send(400, "text/plain", "Falta on"); return; }
  running = (server.arg("on").toInt() != 0);
  server.send(200, "text/plain", running ? "RUN=ON" : "RUN=OFF");
  Serial.println(running ? "RUN=ON" : "RUN=OFF");
}

void handleMode() {
  if (!server.hasArg("auto")) { server.send(400, "text/plain", "Falta auto"); return; }
  modeAuto = (server.arg("auto").toInt() != 0);

  if (modeAuto) {
    // Al entrar a AUTO: transición a HOME y la banda sí puede moverse en auto
    autoState = AUTO_TRANSITION;
    beltStart(beltDuty);
  } else {
    // Al entrar a MANUAL: banda siempre detenida
    beltStop();
  }

  server.send(200, "text/plain", modeAuto ? "MODE=AUTO" : "MODE=MANUAL");
  Serial.println(modeAuto ? "MODE=AUTO" : "MODE=MANUAL");
}

void handleState() {
  bool obj = sensorIsLow();
  String json = "{\"current\":[";
  for (int i=0;i<SERVO_COUNT;i++){ json += String((int)roundf(currentDeg[i])); if(i<SERVO_COUNT-1) json += ","; }
  json += "],\"targets\":[";
  for (int i=0;i<SERVO_COUNT;i++){ json += String((int)roundf(targetDeg[i])); if(i<SERVO_COUNT-1) json += ","; }
  json += "],\"speed_dps\":";
  json += String((int)roundf(maxSpeedDPS));
  json += ",\"running\":";
  json += (running ? "true":"false");
  json += ",\"mode\":\"";
  json += (modeAuto ? "auto":"manual");
  json += "\",\"phase\":\"";
  json += phaseString();
  json += "\",\"obj\":";
  json += (obj ? "true":"false");
  json += ",\"belt_speed\":";
  json += String(beltCurrentDuty);
  json += ",\"clasif\":\"";
  json += clasifActual;
  json += "\"}";
  server.send(200, "application/json", json);
}

// ===== Setup / Loop =====
void setup() {
  Serial.begin(115200);
  Serial.println("\nESP32 + PCA9685: Manual/Auto con transición, QR-wait y banda solo en AUTO");

  // Banda
  pinMode(IN1, OUTPUT); pinMode(IN2, OUTPUT);
  ledcSetup(BELT_PWM_CH, BELT_PWM_FREQ, BELT_PWM_BITS);
  ledcAttachPin(ENA, BELT_PWM_CH);
  beltStop(); // *** MANUAL por defecto => banda detenida ***

  // Sensor
  pinMode(SENSOR_PIN, INPUT);

  // I2C + PCA9685
  Wire.begin(21, 22);
  Wire.setClock(400000);
  pwm.begin();
  pwm.setPWMFreq(50);
  delay(10);

  // Posición inicial a 0° (o cambia a HOME si prefieres)
  for (uint8_t i=0;i<SERVO_COUNT;i++){
    currentDeg[i] = targetDeg[i] = 0.0f;
    pwm.setPWM(SERVO_CH[i], 0, angleToPulse(currentDeg[i]));
  }

  // WiFi
  WiFi.begin(ssid, password);
  Serial.print("Conectando a WiFi");
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis()-t0 < 15000) { delay(300); Serial.print("."); }
  if (WiFi.status()==WL_CONNECTED) Serial.println("\n✅ WiFi: " + WiFi.localIP().toString());
  else Serial.println("\n⚠️ WiFi no conectado");

  // Firebase
  fbConfig.host = FIREBASE_HOST;
  fbConfig.signer.tokens.legacy_token = FIREBASE_AUTH;
  Firebase.begin(&fbConfig, &fbAuth);
  Firebase.reconnectWiFi(true);

  // HTTP
  server.on("/", handleRoot);
  server.on("/sliders", handleSliders);
  server.on("/speed", handleSpeed);
  server.on("/run", handleRun);
  server.on("/mode", handleMode);
  server.on("/state", handleState);
  server.begin();
  Serial.println("HTTP listo: / /sliders /speed /run /mode /state");

  lastUpdateMs = millis();
}

void loop() {
  server.handleClient();
  controlUpdate();
  autoUpdate();
}
