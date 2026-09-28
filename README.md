# PWA — Timbre Escolar Automático (INCOA)

Interfaz de administración de horarios, historial y alarma de
emergencia. Por defecto sigue funcionando con datos de ejemplo en
`localStorage` (`USE_MOCK_DATA = true`), para poder probar la interfaz
sin depender de nada más. El backend en PHP/MySQL ya está incluido en
`api/` y listo para usarse cuando quieran probar el flujo completo.

## Cómo probarla (solo la interfaz, sin backend)

1. Copien esta carpeta dentro de `htdocs` de XAMPP (por ejemplo,
   `htdocs/timbre/`), o ábranla con cualquier servidor estático.
2. Abran `http://localhost/timbre/` en el navegador.
3. Prueben agregar, editar y eliminar horarios, el botón rojo
   "Activar alarma" (queda registrado en Historial), y "Simular
   activación".
4. Para probar el modo offline: herramientas de desarrollador →
   Application/Aplicación → Service Workers → "Offline". La interfaz
   debe seguir funcionando (los datos son locales).

## Cómo activar el backend real (PHP + MySQL)

1. Con XAMPP corriendo (Apache + MySQL), abran phpMyAdmin y ejecuten
   `api/schema.sql` para crear la base `timbre_escolar` y sus tablas.
2. Revisen `api/config.php`: usuario/contraseña de MySQL (por defecto,
   los de XAMPP: `root` sin contraseña).
3. En `js/app.js`, cambien `USE_MOCK_DATA` a `false` y ajusten
   `API_BASE` a la URL real, ej. `http://localhost/timbre/api`.
4. Recarguen la página: horarios, historial y alarma ahora se leen y
   escriben en MySQL a través de `api/horarios.php`, `api/historial.php`
   y `api/alarma.php`.

## Estructura

```
index.html         interfaz (pestañas Horarios / Historial)
css/style.css       estilos
js/app.js           lógica de la app y capa de datos (mock o API real)
manifest.json       configuración de instalación como PWA
sw.js               service worker (cache del app shell)
icons/icon.svg      ícono de la app
api/config.php      conexión a MySQL + headers comunes
api/schema.sql       script para crear la base de datos
api/horarios.php     CRUD de horarios (lo consume la PWA y el ESP32)
api/historial.php    registro y consulta de activaciones reales
api/alarma.php       bandeja de la orden de alarma remota
esp32/timbre_esp32/  sketch de ejemplo para el ESP32 (Arduino IDE)
```

## Integración con el ESP32

El ESP32 (WROOM-32) es el único controlador de hardware; no hay Arduino
Uno en este diseño. Habla con la misma API PHP que usa la PWA, por
WiFi + HTTP + JSON (texto, no solo números — ver los comentarios al
inicio de `esp32/timbre_esp32/timbre_esp32.ino` para el detalle):

- Descarga el horario vigente por `GET api/horarios.php` y lo guarda en
  su propia memoria (NVS), para seguir funcionando sin internet.
- Compara la hora del módulo RTC contra ese horario y activa el
  contactor de forma local — la red nunca es necesaria para que suene
  el timbre de clase.
- Revisa por `GET api/alarma.php` si hay una orden de alarma remota
  pendiente (activada desde el botón rojo de la PWA), y aparte cuenta
  con un pulsador físico cableado directo al ESP32, independiente de
  WiFi y del backend.
- Cada vez que hace sonar cualquier patrón, registra la activación real
  con `POST api/historial.php`.

El sketch en `esp32/timbre_esp32/timbre_esp32.ino` es un punto de
partida funcional con esa lógica ya implementada (WiFi, NTP, RTC
DS3231, HTTPClient + ArduinoJson, pulsador por interrupción, tres
patrones no bloqueantes). Antes de instalarlo en el edificio real hay
que endurecerlo (reconexión WiFi más robusta, cola local de eventos sin
conexión, etc.), pero ya demuestra que el ESP32 sí puede recibir y
enviar texto/JSON sin ningún problema.
