import cv2
import numpy as np
from pyzbar.pyzbar import decode
import firebase_admin
from firebase_admin import credentials, db
import json
import time

# Clave Firebase
cred = credentials.Certificate("clave_firebase.json")
firebase_admin.initialize_app(cred, {
    'databaseURL': 'https://se-sparkpp-default-rtdb.firebaseio.com/' #Actualizar con clave propia de Firebase
})

# Función para verificar si la clasificación es válida
def es_clasificacion_valida(clasificacion):
    return clasificacion in ["Linea Normal", "Linea Fragil", "Linea Pesada"]

# Función para subir la clasificación a Firebase
def subir_clasificacion(clasificacion):
    # Solo se actualiza si la clasificación es válida
    if es_clasificacion_valida(clasificacion):
        ref = db.reference("/Orden Actual")
        ref.update({"clasificacion": clasificacion})
        print(f" Clasificación '{clasificacion}' subida a Firebase.")
    else:
        print(f" Clasificación desconocida: '{clasificacion}'. No se actualiza 'Orden Actual'.")

# Iniciar cámara
cap = cv2.VideoCapture(1)
print("Escaneando QR... presiona 'q' para salir.")

# Obtener el último valor de clasificación (solo al inicio)
ref = db.reference("/Orden Actual")
orden_actual = ref.get().get("clasificacion", "Clasificación Desconocida")

while True:
    ret, frame = cap.read()
    if not ret:
        continue

    # Convertir el frame a escala de grises
    gray_frame = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)

    # Detectar códigos QR en el frame en escala de grises
    codes = decode(gray_frame)

    for code in codes:
        data = code.data.decode('utf-8')
        print(f" QR leído: {data}")
        
        # Solo se actualiza si la clasificación es válida, si no, se mantiene el último valor
        if es_clasificacion_valida(data):
            orden_actual = data  # Actualiza el orden solo si es válido
            subir_clasificacion(orden_actual)
        else:
            print(f" Clasificación desconocida: '{data}'. Se mantiene '{orden_actual}'.")

        time.sleep(2)  # Para evitar múltiples lecturas repetidas

        # Dibuja el rectángulo alrededor del QR
        pts = code.polygon
        pts = [(p.x, p.y) for p in pts]
        cv2.polylines(frame, [np.array(pts)], True, (0, 255, 0), 2)
        cv2.putText(frame, data, (pts[0][0], pts[0][1]-10),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.6, (255, 0, 0), 2)

    # Mostrar el frame en escala de grises con los QR detectados
    cv2.imshow("Lector QR en Escala de Grises", gray_frame)
    
    if cv2.waitKey(1) & 0xFF == ord('q'):
        break

cap.release()
cv2.destroyAllWindows()