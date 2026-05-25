import serial
import cv2
import numpy as np
import sys

# Mettez ici le port EXACT de votre Arduino (Ex: 'COM3', 'COM4', etc.)
PUERTO_SERIAL = 'COM8' 
VELOCIDAD = 1000000

try:
    # 1. Ouverture du port avec les paramètres de contrôle à False
    ser = serial.Serial()
    ser.port = PUERTO_SERIAL
    ser.baudrate = VELOCIDAD
    ser.timeout = 0.1
    ser.rts = False  # <- INDISPENSABLE POUR LA BASE MB : Désactive le Reset automatique
    ser.dtr = False  # <- INDISPENSABLE POUR LA BASE MB : Désactive le mode Programmation
    
    ser.open()
    print(f"[OK] Connecté au port {PUERTO_SERIAL} à 115200 baud.")
    
    # Réinitialisation forcée pour vider les résidus
    ser.reset_input_buffer()
    
    print("-> Fermez bien le moniteur série d'Arduino ! <-")
    print("-> APPUYEZ SUR LE BOUTON 'RST' DE LA CARTE MAINTENANT <-\n")
except Exception as e:
    print(f"[ERREUR] Impossible d'ouvrir le port {PUERTO_SERIAL}: {e}")
    sys.exit()

buffer = bytearray()
octets_recus_total = 0

while True:
    # Vérifier s'il y a des données sur le port
    en_attente = ser.in_waiting
    if en_attente > 0:
        datos = ser.read(en_attente)
        buffer.extend(datos)
        octets_recus_total += len(datos)
        
        # Message de diagnostic en temps réel
        print(f"[INFO] Octets reçus au total : {octets_recus_total} | Buffer actuel : {len(buffer)} octets", end='\r')

        # Recherche des images dans le buffer
        while len(buffer) >= 6:
            inicio = buffer.find(b'\xaa\xbb')
            if inicio == -1:
                buffer = buffer[-1:]
                break
            
            if inicio > 0:
                buffer = buffer[inicio:]
                continue
                
            tamano_imagen = int.from_bytes(buffer[2:6], byteorder='little')
            
            # Vérification de sécurité sur la taille de l'image (VGA max ~60KB)
            if tamano_imagen > 100000 or tamano_imagen <= 0:
                print(f"\n[ALERTE] Taille d'image invalide détectée ({tamano_imagen} bytes). Nettoyage du buffer.")
                buffer = buffer[2:]
                continue

            if len(buffer) < 6 + tamano_imagen:
                break
                
            bytes_jpeg = buffer[6:6 + tamano_imagen]
            buffer = buffer[6 + tamano_imagen:]
            
            np_arr = np.frombuffer(bytes_jpeg, dtype=np.uint8)
            img = cv2.imdecode(np_arr, cv2.IMREAD_COLOR)
            
            if img is not None:
                cv2.imshow("Video en vivo ESP32-CAM", img)
            else:
                print("\n[ALERTE] Échec du décodage d'une image JPEG reçue.")
            
    if cv2.waitKey(1) & 0xFF == ord('q'):
        break

ser.close()
cv2.destroyAllWindows()
