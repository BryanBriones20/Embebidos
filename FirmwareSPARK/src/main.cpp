#include <Wire.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Adafruit_PWMServoDriver.h>

WebServer server(80);
Adafruit_PWMServoDriver pwm = Adafruit_PWMServoDriver();

// WiFi credentials
const char* ssid = "/HOGAR";
const char* password = "841405841405";

// PWM valores mínimos y máximos (ajusta según tus servos)
#define SERVO_MIN_PULSE 150
#define SERVO_MAX_PULSE 600

// Ángulos actuales de los 5 servos
int angulos[5] = {0};

// Mapear ángulo a pulso PWM
int angleToPulse(int angle) {
  return map(angle, 0, 180, SERVO_MIN_PULSE, SERVO_MAX_PULSE);
}

// Mover suavemente un servo de su posición actual al nuevo ángulo
void moveServoSmooth(int channel, int targetAngle) {
  targetAngle = constrain(targetAngle, 0, 180);
  int currentAngle = angulos[channel];

  if (currentAngle == targetAngle) return;

  int step = (targetAngle > currentAngle) ? 1 : -1;
  for (int angle = currentAngle; angle != targetAngle; angle += step) {
    int pulse = angleToPulse(angle);
    pwm.setPWM(channel, 0, pulse);
    delay(10);  // Más alto = más suave, más lento
  }

  // Asegurar posición final exacta
  int finalPulse = angleToPulse(targetAngle);
  pwm.setPWM(channel, 0, finalPulse);
  angulos[channel] = targetAngle;
}

// --- HTTP Handlers ---
void handleRoot() {
  server.send(200, "text/plain", "Servidor activo - PCA9685 listo");
}

void handleSliders() {
  bool recibido = false;

  for (int i = 0; i < 5; i++) {
    String key = "a" + String(i + 1);
    if (server.hasArg(key)) {
      int nuevo = server.arg(key).toInt();
      moveServoSmooth(i, nuevo);
      recibido = true;
    }
  }

  if (recibido) {
    Serial.print("🎚 Ángulos recibidos: [");
    for (int i = 0; i < 5; i++) {
      Serial.print(angulos[i]);
      if (i < 4) Serial.print(", ");
    }
    Serial.println("]");
    server.send(200, "text/plain", "Servos actualizados");
  } else {
    server.send(400, "text/plain", "Faltan parámetros");
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("Iniciando ESP32 + PCA9685...");

  // Iniciar I2C
  Wire.begin(21, 22);

  // Iniciar PCA9685
  pwm.begin();
  pwm.setPWMFreq(50);
  delay(10);

  // Posición inicial 0°
  for (int i = 0; i < 5; i++) {
    int pulse = angleToPulse(angulos[i]);
    pwm.setPWM(i, 0, pulse);
  }

  // Conexión WiFi
  WiFi.begin(ssid, password);
  Serial.print("Conectando a WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\n✅ Conectado, IP: " + WiFi.localIP().toString());

  // Servidor HTTP
  server.on("/", handleRoot);
  server.on("/sliders", handleSliders);
  server.begin();
  Serial.println("🌐 Servidor iniciado en /sliders");
}

void loop() {
  server.handleClient();
}
