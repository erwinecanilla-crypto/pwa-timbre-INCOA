/* ==========================================================================
   Timbre Escolar Automático — INCOA
   esp32/timbre_esp32.ino

   SKETCH DE EJEMPLO para el ESP32 (módulo WROOM-32) como controlador
   ÚNICO del sistema (sección 5 del documento del proyecto). No hay
   Arduino Uno en esta versión: el ESP32 decide, el ESP32 controla el
   contactor.

   SOBRE LA DUDA DE "EL ARDUINO SOLO LEE NÚMEROS":
   Esa limitación viene de un diseño distinto (ESP32 <-> Arduino Uno por
   Serial mandando un solo dígito de comando), que NO es el diseño de
   este proyecto. Aquí el ESP32 habla directo por WiFi con la API en PHP
   usando HTTP + JSON, que es texto (caracteres) de principio a fin:
   nombres de campos, horas en formato "HH:MM", etc. Las librerías
   HTTPClient.h y ArduinoJson.h (ambas estándar para ESP32) están hechas
   exactamente para eso: pedir una URL, recibir texto/JSON, y convertirlo
   a variables de C++. No hay ninguna limitación de "solo números" en el
   ESP32: esa es una limitación de un Arduino Uno hablando por Serial con
   un protocolo casero, no de este chip ni de este protocolo.

   LIBRERÍAS NECESARIAS (Arduino IDE -> Administrador de bibliotecas):
     - RTClib (Adafruit)           -> lectura/escritura del DS3231
     - ArduinoJson (Benoit Blanchon) -> parseo de las respuestas de la API
     - (WiFi.h, HTTPClient.h, Preferences.h ya vienen con el paquete de
        placas ESP32 de Espressif, no hay que instalarlas aparte)

   ESTE ARCHIVO ES UN PUNTO DE PARTIDA FUNCIONAL, no el firmware final de
   producción: antes de instalarlo en el edificio real, agreguen manejo
   de errores más robusto, reconexión WiFi con backoff, etc.
   ========================================================================== */

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <RTClib.h>
#include <Preferences.h>

// ---------------------------------------------------------------------
// Configuración: AJUSTAR antes de cargar
// ---------------------------------------------------------------------
const char *WIFI_SSID = "POCO X7 Pro";
const char *WIFI_PASS = "el_de_las_recargas";

// IP o dominio donde corre el backend PHP (XAMPP en la misma red, o un
// hosting). Ej: "http://192.168.1.50/timbre/api"
const char *API_BASE = "http://10.89.137.76/pwa-timbre/api";

const long  GMT_OFFSET_SEG = -6 * 3600; // El Salvador: UTC-6, sin horario de verano
const int   DST_OFFSET_SEG = 0;
const char *NTP_SERVER = "pool.ntp.org";

// Pines (ajustar según el cableado real / opto-acoplador del contactor)
// Cableado real del prototipo (ESP32 DevKit 30 pines):
const int PIN_RELE      = 26; // D26 -> IN del módulo relé
const int PIN_PULSADOR  = 2;  // D2  -> pulsador (la otra pata a GND, INPUT_PULLUP)
const int PIN_SDA       = 15; // D15 -> SDA del RTC y del LCD
const int PIN_SCL       = 13; // D13 -> SCL del RTC y del LCD

// Muchos módulos de relé de 5V se activan con nivel BAJO. Si al cargar el
// código el relé suena/activa "al revés" (activo en reposo, apagado al tocar),
// cambia este valor.
const bool RELE_ACTIVO_BAJO = true; // --> esto es CLAVE!

// Duraciones de cada patrón (sección 4 del proyecto)
const unsigned long DURACION_CORTO_MS = 3000;
const unsigned long DURACION_MEDIO_MS = 9000;
const unsigned long DURACION_LARGO_MS = 32000; // "largo" total (>=30 s)
const unsigned long LARGO_PULSO_ON_MS = 1000;
const unsigned long LARGO_PULSO_OFF_MS = 500;

const unsigned long INTERVALO_REVISAR_ALARMA_MS = 5000;   // polling de alarma.php
const unsigned long INTERVALO_SINCRONIZAR_HORARIOS_MS = 60UL * 1000; // 1 minuto

// ---------------------------------------------------------------------
// Estado global
// ---------------------------------------------------------------------
RTC_DS3231 rtc;
Preferences prefs;

struct Horario {
  uint16_t minutosDelDia; // 0-1439, ej 07:00 -> 420
  char tipo[12];          // "clase" | "recreo" | "salida" | "entrada" | "emergencia"
  uint8_t diasBitmask;    // bit0=L, bit1=M, bit2=X, bit3=J, bit4=V
  bool activo;
};

#define MAX_HORARIOS 60
Horario g_horarios[MAX_HORARIOS];
int g_numHorarios = 0;

bool g_sincronizadoInicial = false;
unsigned long g_ultimoIntentoWiFi = 0;
int g_ultimoMinutoRevisado = -1;
unsigned long g_ultimaRevisionAlarma = 0;
unsigned long g_ultimaSincronizacion = 0;

enum EstadoToque { SIN_SONAR, SONANDO_CORTO, SONANDO_MEDIO, SONANDO_LARGO };
EstadoToque g_estadoToque = SIN_SONAR;
unsigned long g_inicioToque = 0;
String g_origenToqueActual = "";
String g_tipoToqueActual = "";

// El pulsador solo levanta una bandera desde la interrupción; el trabajo
// real (activar el relé, llamar a la API) pasa en loop(), nunca dentro
// de la ISR.
volatile bool g_pulsadorPresionado = false;
volatile unsigned long g_ultimoPulsadorMs = 0;

void IRAM_ATTR isrPulsador() {
  unsigned long ahora = millis();
  if (ahora - g_ultimoPulsadorMs > 300) { // debounce simple, 300 ms
    g_pulsadorPresionado = true;
    g_ultimoPulsadorMs = ahora;
  }
}

// ---------------------------------------------------------------------
// Utilidades de hardware
// ---------------------------------------------------------------------
void setRele(bool encendido) {
  digitalWrite(PIN_RELE, (encendido != RELE_ACTIVO_BAJO) ? HIGH : LOW);
}

// Escáner I2C: en el monitor serie debe aparecer el RTC (0x68) y el LCD
// (normalmente 0x27 o 0x3F). Sirve para comprobar el cableado.
void escanearI2C() {
  Serial.println("Escaneando bus I2C...");
  int encontrados = 0;
  for (uint8_t dir = 1; dir < 127; dir++) {
    Wire.beginTransmission(dir);
    if (Wire.endTransmission() == 0) {
      Serial.printf("  dispositivo en 0x%02X\n", dir);
      encontrados++;
    }
  }
  if (encontrados == 0) Serial.println("  ninguno: revise SDA/SCL y alimentación.");
}

// ---------------------------------------------------------------------
// LCD 16x2 por I2C (mochila PCF8574). Driver mínimo, sin librerías extra.
// ---------------------------------------------------------------------
uint8_t g_lcdAddr = 0; // 0 = no se encontró LCD
const uint8_t LCD_BL = 0x08, LCD_EN = 0x04, LCD_RS = 0x01;

void lcdExpansor(uint8_t d) {
  Wire.beginTransmission(g_lcdAddr);
  Wire.write(d | LCD_BL); // luz de fondo siempre encendida
  Wire.endTransmission();
}

void lcdEnviar4(uint8_t nibble, uint8_t rs) {
  uint8_t d = (nibble << 4) | rs;
  lcdExpansor(d | LCD_EN);
  delayMicroseconds(2);
  lcdExpansor(d & ~LCD_EN);
  delayMicroseconds(60);
}

void lcdEnviar(uint8_t v, uint8_t rs) {
  lcdEnviar4(v >> 4, rs);
  lcdEnviar4(v & 0x0F, rs);
}

void lcdLinea(uint8_t fila, const char *texto) {
  if (!g_lcdAddr) return;
  lcdEnviar(0x80 | (fila ? 0x40 : 0x00), 0); // posicionar al inicio de la fila
  int n = 0;
  for (; texto[n] && n < 16; n++) lcdEnviar(texto[n], LCD_RS);
  for (; n < 16; n++) lcdEnviar(' ', LCD_RS); // rellenar con espacios
}

void iniciarLCD() {
  const uint8_t candidatas[] = {0x27, 0x3F};
  for (uint8_t dir : candidatas) {
    Wire.beginTransmission(dir);
    if (Wire.endTransmission() == 0) { g_lcdAddr = dir; break; }
  }
  if (!g_lcdAddr) {
    Serial.println("LCD no encontrado (0x27/0x3F). Se sigue sin pantalla.");
    return;
  }
  Serial.printf("LCD encontrado en 0x%02X\n", g_lcdAddr);
  delay(50);
  lcdEnviar4(0x03, 0); delay(5);
  lcdEnviar4(0x03, 0); delay(1);
  lcdEnviar4(0x03, 0);
  lcdEnviar4(0x02, 0);        // modo 4 bits
  lcdEnviar(0x28, 0);         // 2 líneas, 5x8
  lcdEnviar(0x0C, 0);         // pantalla encendida, sin cursor
  lcdEnviar(0x06, 0);
  lcdEnviar(0x01, 0); delay(3); // limpiar
  lcdLinea(0, "Timbre escolar");
  lcdLinea(1, "Iniciando...");
}

// Fecha en la fila 1, hora y estado en la fila 2. Se refresca cada segundo.
void actualizarLCD() {
  if (!g_lcdAddr) return;
  static unsigned long ultimo = 0;
  if (millis() - ultimo < 1000) return;
  ultimo = millis();

  static const char *dias[] = {"Dom", "Lun", "Mar", "Mie", "Jue", "Vie", "Sab"};
  DateTime n = rtc.now();
  const char *estado = (g_estadoToque != SIN_SONAR) ? "TIMBRE"
                       : (WiFi.status() == WL_CONNECTED ? "WiFi" : "");
  char l1[32], l2[32];
  snprintf(l1, sizeof(l1), "%02d/%02d/%04d %s", n.day(), n.month(), n.year(), dias[n.dayOfTheWeek()]);
  snprintf(l2, sizeof(l2), "%02d:%02d:%02d %-7s", n.hour(), n.minute(), n.second(), estado);
  lcdLinea(0, l1);
  lcdLinea(1, l2);
}

// ---------------------------------------------------------------------
// setup()
// ---------------------------------------------------------------------
void setup() {
  Serial.begin(115200);

  pinMode(PIN_RELE, OUTPUT);
  setRele(false);

  pinMode(PIN_PULSADOR, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PIN_PULSADOR), isrPulsador, FALLING);

  Wire.begin(PIN_SDA, PIN_SCL);
  escanearI2C();
  iniciarLCD();
  if (!rtc.begin()) {
    Serial.println("ERROR: no se detectó el módulo RTC DS3231. Revise el cableado I2C.");
  }

  prefs.begin("timbre", false);
  cargarHorariosDesdeMemoria(); // por si no hay WiFi al arrancar

  conectarWiFi();
  if (WiFi.status() == WL_CONNECTED) {
    sincronizarHoraPorNTP();
    sincronizarHorarios();
    g_sincronizadoInicial = true;
  }
}

// ---------------------------------------------------------------------
// loop()
// ---------------------------------------------------------------------
void loop() {
  // 1) Pulsador físico: máxima prioridad, no depende de red ni de la
  //    comparación de horario. Funciona aunque el WiFi esté caído.
  if (g_pulsadorPresionado) {
    g_pulsadorPresionado = false;
    if (g_estadoToque == SIN_SONAR) {
      iniciarToque("emergencia", SONANDO_LARGO, "ESP32 (pulsador)");
    }
  }

  actualizarLCD();

  // 2) Mantener el patrón de sonido en curso (no bloqueante).
  actualizarToqueEnCurso();

  // 3) Revisar el horario programado, una vez por minuto real.
  revisarHorarioProgramado();

  // 4) Si hay WiFi, revisar si hay una orden remota de alarma pendiente
  //    y, cada cierto tiempo, refrescar el horario vigente.
  if (WiFi.status() == WL_CONNECTED) {
    unsigned long ahora = millis();
    if (!g_sincronizadoInicial) { // el WiFi volvió después del arranque
      g_sincronizadoInicial = true;
      g_ultimaSincronizacion = ahora;
      sincronizarHoraPorNTP();
      sincronizarHorarios();
    }
    if (ahora - g_ultimaRevisionAlarma >= INTERVALO_REVISAR_ALARMA_MS) {
      g_ultimaRevisionAlarma = ahora;
      revisarAlarmaRemota();
    }
    if (ahora - g_ultimaSincronizacion >= INTERVALO_SINCRONIZAR_HORARIOS_MS) {
      g_ultimaSincronizacion = ahora;
      sincronizarHoraPorNTP();
      sincronizarHorarios();
    }
  } else {
    reintentarWiFi(); // no bloquea: el timbre sigue funcionando sin red
  }
}

// ---------------------------------------------------------------------
// WiFi / NTP
// ---------------------------------------------------------------------
// Solo para el arranque: un intento con espera de hasta 10 s.
void conectarWiFi() {
  Serial.printf("Conectando a WiFi '%s'...\n", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  unsigned long inicio = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - inicio < 10000) {
    delay(200);
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi conectado: " + WiFi.localIP().toString());
  } else {
    Serial.println("No se pudo conectar a WiFi por ahora; se reintentará.");
  }
}

// En loop(): reintento NO bloqueante cada 20 s. Así el timbre y el pulsador
// siguen funcionando aunque no haya red.
void reintentarWiFi() {
  if (millis() - g_ultimoIntentoWiFi < 20000) return;
  g_ultimoIntentoWiFi = millis();
  Serial.println("Reintentando WiFi...");
  WiFi.disconnect();
  WiFi.begin(WIFI_SSID, WIFI_PASS);
}

void sincronizarHoraPorNTP() {
  configTime(GMT_OFFSET_SEG, DST_OFFSET_SEG, NTP_SERVER);
  struct tm horaActual;
  if (getLocalTime(&horaActual, 5000)) {
    rtc.adjust(DateTime(
        horaActual.tm_year + 1900, horaActual.tm_mon + 1, horaActual.tm_mday,
        horaActual.tm_hour, horaActual.tm_min, horaActual.tm_sec));
    Serial.println("RTC actualizado por NTP.");
  } else {
    Serial.println("No se pudo obtener la hora por NTP; se conserva la del RTC.");
  }
}

// ---------------------------------------------------------------------
// Sincronización de horarios (GET api/horarios.php) — aquí es donde el
// ESP32 recibe TEXTO/JSON de la red, no solo números.
// ---------------------------------------------------------------------
void sincronizarHorarios() {
  HTTPClient http;
  http.begin(String(API_BASE) + "/horarios.php");
  int codigo = http.GET();

  if (codigo == 200) {
    String cuerpo = http.getString(); // <- esto es una cadena de caracteres (JSON)
    procesarJsonHorarios(cuerpo);
    guardarHorariosEnMemoria(cuerpo); // respaldo en NVS para cuando no haya red
    Serial.println("Horarios sincronizados desde la API.");
  } else {
    Serial.printf("No se pudo descargar horarios.php (código %d). Se usa el respaldo local.\n", codigo);
  }
  http.end();
}

void procesarJsonHorarios(const String &json) {
  JsonDocument doc; // ArduinoJson v7
  DeserializationError err = deserializeJson(doc, json);
  if (err) {
    Serial.print("Error al interpretar el JSON de horarios: ");
    Serial.println(err.c_str());
    return;
  }

  g_numHorarios = 0;
  for (JsonObject h : doc.as<JsonArray>()) {
    if (g_numHorarios >= MAX_HORARIOS) break;
    if (!h["activo"].as<bool>()) continue;

    const char *horaTexto = h["hora"]; // "HH:MM", sigue siendo texto
    int hh = 0, mm = 0;
    sscanf(horaTexto, "%d:%d", &hh, &mm);

    Horario &destino = g_horarios[g_numHorarios];
    destino.minutosDelDia = hh * 60 + mm;
    strncpy(destino.tipo, h["tipo"] | "clase", sizeof(destino.tipo) - 1);
    destino.tipo[sizeof(destino.tipo) - 1] = '\0';
    destino.activo = true;

    destino.diasBitmask = 0;
    for (JsonVariant d : h["dias"].as<JsonArray>()) {
      const char *dia = d.as<const char *>();
      if (!dia) continue;
      switch (dia[0]) {
        case 'L': destino.diasBitmask |= (1 << 0); break;
        case 'M': destino.diasBitmask |= (1 << 1); break;
        case 'X': destino.diasBitmask |= (1 << 2); break;
        case 'J': destino.diasBitmask |= (1 << 3); break;
        case 'V': destino.diasBitmask |= (1 << 4); break;
      }
    }
    g_numHorarios++;
  }
  Serial.printf("%d horarios cargados en memoria.\n", g_numHorarios);
}

void guardarHorariosEnMemoria(const String &json) {
  prefs.putString("horarios_json", json);
}

void cargarHorariosDesdeMemoria() {
  String json = prefs.getString("horarios_json", "");
  if (json.length() > 0) {
    procesarJsonHorarios(json);
    Serial.println("Horarios cargados desde el respaldo local (NVS).");
  }
}

// ---------------------------------------------------------------------
// Comparación de horario vigente contra el RTC (función de decisión
// local, sección 4.1 del documento). No depende del WiFi para nada.
// ---------------------------------------------------------------------
void revisarHorarioProgramado() {
  DateTime ahora = rtc.now();
  int minutoDelDia = ahora.hour() * 60 + ahora.minute();

  // Solo evaluar una vez por cada minuto real, para no disparar varias
  // veces dentro del mismo minuto.
  if (minutoDelDia == g_ultimoMinutoRevisado) return;
  g_ultimoMinutoRevisado = minutoDelDia;

  // dayOfTheWeek(): 0=domingo … 6=sábado (convención de RTClib)
  int diaSemana = ahora.dayOfTheWeek();
  int bit = -1;
  switch (diaSemana) {
    case 1: bit = 0; break; // lunes
    case 2: bit = 1; break; // martes
    case 3: bit = 2; break; // miércoles
    case 4: bit = 3; break; // jueves
    case 5: bit = 4; break; // viernes
  }
  if (bit < 0) return; // fin de semana: no hay horario de clases

  for (int i = 0; i < g_numHorarios; i++) {
    Horario &h = g_horarios[i];
    if (!h.activo) continue;
    if (h.minutosDelDia != minutoDelDia) continue;
    if (!(h.diasBitmask & (1 << bit))) continue;

    if (g_estadoToque == SIN_SONAR) {
      EstadoToque patron = patronParaTipo(h.tipo);
      iniciarToque(h.tipo, patron, "ESP32 (horario)");
    }
    break;
  }
}

EstadoToque patronParaTipo(const char *tipo) {
  if (strcmp(tipo, "recreo") == 0 || strcmp(tipo, "salida") == 0) return SONANDO_MEDIO;
  if (strcmp(tipo, "emergencia") == 0) return SONANDO_LARGO;
  return SONANDO_CORTO; // "clase", "entrada"
}

// ---------------------------------------------------------------------
// Alarma remota (GET/POST api/alarma.php)
// ---------------------------------------------------------------------
void revisarAlarmaRemota() {
  HTTPClient http;
  http.begin(String(API_BASE) + "/alarma.php");
  int codigo = http.GET();

  if (codigo == 200) {
    String cuerpo = http.getString();
    JsonDocument doc;
    if (deserializeJson(doc, cuerpo) == DeserializationError::Ok) {
      bool pendiente = doc["pendiente"] | false;
      if (pendiente && g_estadoToque == SIN_SONAR) {
        iniciarToque("emergencia", SONANDO_LARGO, "ESP32 (remoto)");
        reconocerAlarmaRemota();
      }
    }
  }
  http.end();
}

void reconocerAlarmaRemota() {
  HTTPClient http;
  http.begin(String(API_BASE) + "/alarma.php");
  http.addHeader("Content-Type", "application/json");
  http.POST("{\"accion\":\"reconocer\"}");
  http.end();
}

// ---------------------------------------------------------------------
// Ejecución del patrón de sonido (no bloqueante) + registro en historial
// ---------------------------------------------------------------------
void iniciarToque(const char *tipo, EstadoToque patron, const char *origen) {
  g_estadoToque = patron;
  g_inicioToque = millis();
  g_tipoToqueActual = tipo;
  g_origenToqueActual = origen;
  Serial.printf("Iniciando toque '%s' (%s)\n", tipo, origen);
}

void actualizarToqueEnCurso() {
  if (g_estadoToque == SIN_SONAR) return;

  unsigned long transcurrido = millis() - g_inicioToque;

  switch (g_estadoToque) {
    case SONANDO_CORTO:
      setRele(true);
      if (transcurrido >= DURACION_CORTO_MS) finalizarToque();
      break;

    case SONANDO_MEDIO:
      setRele(true);
      if (transcurrido >= DURACION_MEDIO_MS) finalizarToque();
      break;

    case SONANDO_LARGO: {
      // Patrón intermitente en pulsos, para imitar una sirena y no
      // confundirse con un toque de clase prolongado (sección 4).
      unsigned long cicloTotal = LARGO_PULSO_ON_MS + LARGO_PULSO_OFF_MS;
      unsigned long posicion = transcurrido % cicloTotal;
      setRele(posicion < LARGO_PULSO_ON_MS);
      if (transcurrido >= DURACION_LARGO_MS) finalizarToque();
      break;
    }

    default:
      break;
  }
}

void finalizarToque() {
  setRele(false);
  Serial.printf("Toque '%s' finalizado (%s)\n", g_tipoToqueActual.c_str(), g_origenToqueActual.c_str());

  if (WiFi.status() == WL_CONNECTED) {
    registrarEnHistorial(g_tipoToqueActual, g_origenToqueActual);
  } else {
    Serial.println("Sin WiFi: no se pudo registrar en el historial en este momento.");
    // Para producción: guardar en una cola local (NVS o SPIFFS) y
    // reenviar cuando vuelva la conexión, en vez de perder el registro.
  }

  g_estadoToque = SIN_SONAR;
}

void registrarEnHistorial(const String &tipo, const String &origen) {
  DateTime ahora = rtc.now();
  char fecha[11], hora[9];
  snprintf(fecha, sizeof(fecha), "%04d-%02d-%02d", ahora.year(), ahora.month(), ahora.day());
  snprintf(hora, sizeof(hora), "%02d:%02d:%02d", ahora.hour(), ahora.minute(), ahora.second());

  JsonDocument doc;
  doc["fecha"] = fecha;
  doc["hora"] = hora;
  doc["tipo"] = tipo;
  doc["origen"] = origen;

  String cuerpo;
  serializeJson(doc, cuerpo); // <- de nuevo: texto/JSON, sin límite a números

  HTTPClient http;
  http.begin(String(API_BASE) + "/historial.php");
  http.addHeader("Content-Type", "application/json");
  int codigo = http.POST(cuerpo);
  http.end();

  if (codigo != 201 && codigo != 200) {
    Serial.printf("No se pudo registrar en historial.php (código %d)\n", codigo);
  }
}
