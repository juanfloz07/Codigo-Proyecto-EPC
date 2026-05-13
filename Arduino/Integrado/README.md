# Prototipo Biomédico con Arduino

Este proyecto corresponde a un sistema biomédico básico desarrollado con Arduino. El código integra varios sensores para medir y visualizar señales fisiológicas como ECG, pulso, SpO2, respiración y temperatura.

## Componentes usados

- Arduino
- Sensor ECG AD8232
- Sensor MAX30102 / MAX30105 para pulso y SpO2
- Sensor MPU6050 para estimación de respiración por movimiento
- Pantalla OLED SSD1306
- Módulo Bluetooth HC-05
- Cables y protoboard

## Funciones principales

- Lectura de señal ECG mediante el AD8232.
- Detección de conexión de electrodos.
- Lectura de señal óptica IR y RED con el MAX30102.
- Cálculo básico de BPM.
- Estimación de SpO2.
- Estimación de frecuencia respiratoria usando el MPU6050.
- Lectura de temperatura desde el MPU6050.
- Visualización de datos en pantalla OLED.
- Envío de datos por monitor serial y Bluetooth.

## Funcionamiento general

El sistema inicializa la pantalla OLED, el sensor MAX30102, el módulo MPU6050 y la comunicación serial/Bluetooth. Luego realiza lecturas continuas de los sensores y muestra variables como ECG, BPM, SpO2, respiraciones por minuto, temperatura y detección del dedo.

## Datos mostrados

El sistema puede mostrar:

- Valor de ECG
- BPM
- SpO2
- Estado del sensor óptico
- Frecuencia respiratoria
- Temperatura
- Detección de dedo

## Uso básico

1. Conectar los sensores al Arduino según el montaje del proyecto.
2. Cargar el código desde el IDE de Arduino.
3. Colocar correctamente los electrodos del AD8232.
4. Ubicar el dedo sobre el sensor MAX30102.
5. Mantener el MPU6050 estable en la zona donde se desea detectar el movimiento respiratorio.
6. Observar los datos en la pantalla OLED, monitor serial o mediante Bluetooth.

## Nota

Este proyecto es un prototipo académico y experimental. No es un dispositivo médico certificado y no debe usarse para diagnóstico clínico.