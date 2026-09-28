-- ==========================================================================
-- Timbre Escolar Automático — INCOA
-- schema.sql
--
-- Cómo usarlo:
--   1. Abran phpMyAdmin (http://localhost/phpmyadmin con XAMPP corriendo).
--   2. Pestaña "SQL" -> peguen todo este archivo -> Continuar.
--      (O por consola: mysql -u root -p < schema.sql)
-- ==========================================================================

CREATE DATABASE IF NOT EXISTS timbre_escolar
  CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;

USE timbre_escolar;

-- ---------------------------------------------------------------------
-- Horarios de clase / receso / salida (y alarma, si se agenda alguna)
-- ---------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS horarios (
  id        INT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
  hora      TIME NOT NULL,
  tipo      ENUM('clase','recreo','salida','entrada','emergencia') NOT NULL,
  dias      VARCHAR(20) NOT NULL COMMENT 'Días separados por coma, ej: L,M,X,J,V',
  activo    TINYINT(1) NOT NULL DEFAULT 1,
  creado_en TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  UNIQUE KEY uniq_hora_tipo (hora, tipo)
);

-- ---------------------------------------------------------------------
-- Historial de activaciones reales, reportadas por el ESP32 (o por la
-- PWA en modo "Simular activación" para pruebas sin hardware).
-- ---------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS historial (
  id         INT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
  fecha      DATE NOT NULL,
  hora       TIME NOT NULL,
  tipo       ENUM('clase','recreo','salida','entrada','emergencia') NOT NULL,
  origen     VARCHAR(60) NOT NULL COMMENT 'Ej: ESP32 (horario), ESP32 (pulsador), ESP32 (remoto), PWA (simulación)',
  registrado_en TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
);

-- ---------------------------------------------------------------------
-- Estado de la alarma remota: una sola fila que actúa como "bandeja de
-- salida" de una orden de emergencia emitida desde la PWA. El ESP32 la
-- consulta (GET api/alarma.php) y, al ejecutar el toque largo, la
-- reconoce (POST { "accion": "reconocer" }) para bajar la bandera.
-- ---------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS alarma_estado (
  id         INT UNSIGNED PRIMARY KEY,
  pendiente  TINYINT(1) NOT NULL DEFAULT 0,
  origen     VARCHAR(60) NULL,
  fecha_hora TIMESTAMP NULL
);

INSERT INTO alarma_estado (id, pendiente, origen, fecha_hora)
VALUES (1, 0, NULL, NULL)
ON DUPLICATE KEY UPDATE id = id;

-- ---------------------------------------------------------------------
-- Datos de ejemplo (los mismos que trae la PWA en modo mock), para que
-- al apagar USE_MOCK_DATA la tabla no aparezca vacía.
-- ---------------------------------------------------------------------
INSERT IGNORE INTO horarios (hora, tipo, dias, activo) VALUES
  ('07:00:00', 'clase',  'L,M,X,J,V', 1),
  ('09:30:00', 'recreo', 'L,M,X,J,V', 1),
  ('12:00:00', 'salida', 'L,M,X,J,V', 1);
