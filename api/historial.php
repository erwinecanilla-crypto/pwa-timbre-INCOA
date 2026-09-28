<?php
/* ==========================================================================
   Timbre Escolar Automático — INCOA
   api/historial.php

   Contrato de datos (el mismo que ya espera js/app.js):
     GET  -> [{ id, fecha: "YYYY-MM-DD", hora: "HH:MM:SS", tipo, origen }, ...]
              (más recientes primero, máx. 200 registros)
     POST body { fecha, hora, tipo, origen } -> registra una activación.

   El ESP32 debe llamar a este mismo POST cada vez que hace sonar el
   timbre de verdad (por horario, por el pulsador físico o por una orden
   remota de alarma), usando origen = "ESP32 (horario)" / "ESP32 (pulsador)"
   / "ESP32 (remoto)" para poder distinguirlo en el historial.
   ========================================================================== */

declare(strict_types=1);
require __DIR__ . "/config.php";

$pdo = get_db();
$metodo = $_SERVER["REQUEST_METHOD"];

function formatear_historial(array $fila): array
{
    return [
        "id"     => (string)$fila["id"],
        "fecha"  => $fila["fecha"],
        "hora"   => $fila["hora"],
        "tipo"   => $fila["tipo"],
        "origen" => $fila["origen"],
    ];
}

switch ($metodo) {

    case "GET":
        $filas = $pdo->query(
            "SELECT * FROM historial ORDER BY fecha DESC, hora DESC LIMIT 200"
        )->fetchAll();
        responder(200, array_map("formatear_historial", $filas));
        break;

    case "POST":
        $datos = leer_body_json();

        $tiposValidos = ["clase", "recreo", "salida", "entrada", "emergencia"];
        $fecha  = $datos["fecha"] ?? date("Y-m-d");
        $hora   = $datos["hora"]  ?? date("H:i:s");
        $tipo   = $datos["tipo"]  ?? "";
        $origen = $datos["origen"] ?? "Desconocido";

        if (!in_array($tipo, $tiposValidos, true)) {
            responder_error(400, "El campo 'tipo' debe ser uno de: " . implode(", ", $tiposValidos));
        }

        $stmt = $pdo->prepare(
            "INSERT INTO historial (fecha, hora, tipo, origen) VALUES (:fecha, :hora, :tipo, :origen)"
        );
        $stmt->execute([
            ":fecha"  => $fecha,
            ":hora"   => $hora,
            ":tipo"   => $tipo,
            ":origen" => $origen,
        ]);

        $id = (int)$pdo->lastInsertId();
        $fila = $pdo->query("SELECT * FROM historial WHERE id = {$id}")->fetch();
        responder(201, formatear_historial($fila));
        break;

    default:
        responder_error(405, "Método no permitido.");
}
