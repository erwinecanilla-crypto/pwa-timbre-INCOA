<?php
/* ==========================================================================
   Timbre Escolar Automático — INCOA
   api/config.php

   Conexión a la base de datos y helpers comunes para todos los endpoints
   (horarios.php, historial.php, alarma.php).

   AJUSTES NECESARIOS ANTES DE USAR:
   - Si usan XAMPP con su configuración por defecto, DB_USER/DB_PASS no
     necesitan cambiarse. Si le pusieron contraseña a root, o el hosting
     les da otras credenciales, ajusten aquí.
   - Ejecuten primero schema.sql en phpMyAdmin (o por consola mysql) para
     crear la base de datos y las tablas antes de probar estos endpoints.
   ========================================================================== */

declare(strict_types=1);

const DB_HOST = "localhost";
const DB_NAME = "timbre_escolar";
const DB_USER = "root";
const DB_PASS = "";        // XAMPP por defecto: contraseña vacía
const DB_CHARSET = "utf8mb4";

// Zona horaria del servidor (usada al generar fecha/hora en PHP).
date_default_timezone_set("America/El_Salvador");

/* ---------- Headers comunes ----------
   Access-Control-Allow-Origin "*" permite que la PWA (o el ESP32, aunque
   a este último no le importa CORS) llamen a la API aunque en desarrollo
   se sirvan desde puertos/orígenes distintos. En producción, si quieren
   restringirlo a su propio dominio, cambien "*" por esa URL exacta. */
header("Content-Type: application/json; charset=utf-8");
header("Access-Control-Allow-Origin: *");
header("Access-Control-Allow-Methods: GET, POST, PUT, DELETE, OPTIONS");
header("Access-Control-Allow-Headers: Content-Type");

// Peticiones "preflight" del navegador: responder vacío y salir.
if ($_SERVER["REQUEST_METHOD"] === "OPTIONS") {
    http_response_code(204);
    exit;
}

/**
 * Devuelve una conexión PDO reutilizable. Si falla, responde 500 en JSON
 * (nunca deja que PHP imprima un error HTML crudo, que rompería el fetch()
 * de la PWA y del ESP32).
 */
function get_db(): PDO
{
    static $pdo = null;
    if ($pdo === null) {
        $dsn = "mysql:host=" . DB_HOST . ";dbname=" . DB_NAME . ";charset=" . DB_CHARSET;
        try {
            $pdo = new PDO($dsn, DB_USER, DB_PASS, [
                PDO::ATTR_ERRMODE => PDO::ERRMODE_EXCEPTION,
                PDO::ATTR_DEFAULT_FETCH_MODE => PDO::FETCH_ASSOC,
            ]);
        } catch (PDOException $e) {
            responder_error(500, "No se pudo conectar a la base de datos. Verifiquen que MySQL esté "
                . "corriendo y que la base 'timbre_escolar' exista (ejecuten schema.sql).");
        }
    }
    return $pdo;
}

/** Responde JSON con el código HTTP indicado y termina la ejecución. */
function responder(int $codigo, $payload): void
{
    http_response_code($codigo);
    echo json_encode($payload, JSON_UNESCAPED_UNICODE);
    exit;
}

/** Atajo para respuestas de error con formato { "error": "..." }. */
function responder_error(int $codigo, string $mensaje): void
{
    responder($codigo, ["error" => $mensaje]);
}

/** Lee y decodifica el body JSON de la petición (POST/PUT). */
function leer_body_json(): array
{
    $crudo = file_get_contents("php://input");
    $datos = json_decode($crudo, true);
    if (!is_array($datos)) {
        responder_error(400, "Body JSON inválido o vacío.");
    }
    return $datos;
}
