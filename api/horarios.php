<?php
/* ==========================================================================
   Timbre Escolar Automático — INCOA
   api/horarios.php

   Contrato de datos (el mismo que ya espera js/app.js):
     GET    -> [{ id, hora: "HH:MM", tipo, dias: ["L","M",...], activo }, ...]
     POST   body { hora, tipo, dias:[...], activo } -> crea, responde el
            objeto creado con su id nuevo.
     PUT    body { id, hora, tipo, dias:[...], activo } -> actualiza.
     DELETE ?id=N -> elimina.

   Este mismo endpoint es el que el ESP32 debe consultar por GET para
   descargar el horario vigente (ver README y el sketch en /esp32).
   ========================================================================== */

declare(strict_types=1);
require __DIR__ . "/config.php";

$pdo = get_db();
$metodo = $_SERVER["REQUEST_METHOD"];

/** Convierte una fila de la tabla `horarios` al formato que consume la PWA/ESP32. */
function formatear_horario(array $fila): array
{
    return [
        "id"     => (string)$fila["id"],
        "hora"   => substr($fila["hora"], 0, 5), // "07:00:00" -> "07:00"
        "tipo"   => $fila["tipo"],
        "dias"   => $fila["dias"] === "" ? [] : explode(",", $fila["dias"]),
        "activo" => (bool)$fila["activo"],
    ];
}

switch ($metodo) {

    case "GET":
        $filas = $pdo->query("SELECT * FROM horarios ORDER BY hora ASC")->fetchAll();
        responder(200, array_map("formatear_horario", $filas));
        break;

    case "POST":
        $datos = leer_body_json();
        validar_horario($datos);

        $stmt = $pdo->prepare(
            "INSERT INTO horarios (hora, tipo, dias, activo) VALUES (:hora, :tipo, :dias, :activo)"
        );
        $stmt->execute([
            ":hora"   => $datos["hora"],
            ":tipo"   => $datos["tipo"],
            ":dias"   => implode(",", $datos["dias"]),
            ":activo" => (int)($datos["activo"] ?? true),
        ]);

        $id = (int)$pdo->lastInsertId();
        $fila = $pdo->query("SELECT * FROM horarios WHERE id = {$id}")->fetch();
        responder(201, formatear_horario($fila));
        break;

    case "PUT":
        $datos = leer_body_json();
        if (empty($datos["id"])) {
            responder_error(400, "Falta 'id' para actualizar el horario.");
        }
        validar_horario($datos);

        $stmt = $pdo->prepare(
            "UPDATE horarios SET hora = :hora, tipo = :tipo, dias = :dias, activo = :activo WHERE id = :id"
        );
        $stmt->execute([
            ":hora"   => $datos["hora"],
            ":tipo"   => $datos["tipo"],
            ":dias"   => implode(",", $datos["dias"]),
            ":activo" => (int)($datos["activo"] ?? true),
            ":id"     => (int)$datos["id"],
        ]);

        $fila = $pdo->query("SELECT * FROM horarios WHERE id = " . (int)$datos["id"])->fetch();
        if (!$fila) {
            responder_error(404, "No existe un horario con ese id.");
        }
        responder(200, formatear_horario($fila));
        break;

    case "DELETE":
        $id = (int)($_GET["id"] ?? 0);
        if ($id <= 0) {
            responder_error(400, "Falta ?id= en la URL.");
        }
        $pdo->prepare("DELETE FROM horarios WHERE id = ?")->execute([$id]);
        responder(200, ["ok" => true]);
        break;

    default:
        responder_error(405, "Método no permitido.");
}

/** Valida los campos mínimos antes de insertar/actualizar. */
function validar_horario(array $datos): void
{
    $tiposValidos = ["clase", "recreo", "salida", "entrada", "emergencia"];
    if (empty($datos["hora"]) || !preg_match('/^\d{2}:\d{2}$/', $datos["hora"])) {
        responder_error(400, "El campo 'hora' debe tener formato HH:MM.");
    }
    if (empty($datos["tipo"]) || !in_array($datos["tipo"], $tiposValidos, true)) {
        responder_error(400, "El campo 'tipo' debe ser uno de: " . implode(", ", $tiposValidos));
    }
    if (empty($datos["dias"]) || !is_array($datos["dias"])) {
        responder_error(400, "El campo 'dias' debe ser un arreglo no vacío, ej: [\"L\",\"M\"].");
    }
}
