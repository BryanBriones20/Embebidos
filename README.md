# ⚙️ Sppark
**System for Pick and Place Automation with Robotic Kinematics**

Sppark es un prototipo de sistema de clasificación automática que integra **robótica**, **visión artificial** y **comunicación en red**.  
El objetivo principal es clasificar objetos en distintas zonas utilizando un **robot manipulador de 5 GDL**, una **banda transportadora**, un **sensor de proximidad** y una **cámara web**.

---

## 🚀 Descripción del proyecto
El sistema funciona de la siguiente manera:
1. La **cámara web** captura y lee los **códigos QR** colocados en los objetos o cajas.
2. Cada QR contiene información sobre el producto y la zona de clasificación asignada.
3. Con esta información, el **robot de 5 GDL** determina cómo recoger el objeto y hacia dónde transportarlo.
4. La **banda transportadora** y el **sensor de proximidad** ayudan en el flujo del proceso de clasificación.
5. La comunicación entre el robot y el usuario se realiza a través de una **ESP32 con conexión WiFi**, utilizando la librería `WiFi.h`.

Toda la información se gestiona a través de una **base de datos Firebase**, a la cual se accede con una clave privada para:
- Consultar datos de los productos.
- Enviar y almacenar información del proceso en tiempo real.

---

## 🎮 Modo manual
Además del modo automático de clasificación, Sppark incluye un **modo manual**, donde el usuario puede:
- Controlar cada articulación del **robot de 5 GDL** mediante **sliders** en la aplicación móvil.
- Ajustar la **velocidad de movimiento del robot**, permitiendo hacerlo más rápido o más lento según los requerimientos de la tarea.
- Tener un control preciso del robot para pruebas, calibraciones o movimientos específicos.

---

## 📂 Estructura del repositorio
El repositorio está organizado en tres carpetas principales:

- **`FirmwareSpark_ESP32/`**  
  Proyecto en **PlatformIO** para la **ESP32**, que controla todo el sistema (robot, sensor y comunicaciones).

- **`app_Spark/`**  
  Proyecto en **Flutter** para la aplicación móvil que controla el proceso y el brazo robótico.  
  La app se conecta mediante la IP de la ESP32 (ambos deben estar en la misma red WiFi).

- **`Scripts/`**  
  Contiene utilidades adicionales, como:
  - `lecturaQR.py`: Script en **Python** que permite leer los códigos QR y enviar la información a **Firebase**.

---

## 🔌 Comunicación y funcionamiento
- La **ESP32** actúa como servidor en la red local.  
- La **aplicación móvil** envía solicitudes HTTP a la IP de la ESP32 para enviar o recibir datos.  
- El **script de lectura QR** también se comunica con Firebase para sincronizar información del proceso.  
- Es requisito que tanto la ESP32 como el dispositivo móvil estén conectados a la **misma red WiFi**.  

---

## 🛠️ Tecnologías utilizadas
- **Hardware**
  - ESP32
  - Robot de 5 GDL
  - Sensor de proximidad
  - Cámara web
  - Banda transportadora  

- **Software**
  - [PlatformIO](https://platformio.org/) para el firmware del ESP32
  - [Flutter](https://flutter.dev/) para la aplicación móvil
  - [Firebase](https://firebase.google.com/) para la base de datos en la nube
  - [Python 3](https://www.python.org/) con librerías de lectura de QR

---

## 👨‍💻 Autores
- **Cesar Daniel Pallazhco **  
- **Bryan Carlos Briones**

---
