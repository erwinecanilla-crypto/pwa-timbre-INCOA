<?php
/* ==========================================================================
   Timbre Escolar Automático — INCOA
   api/alarma.php

   "Bandeja de salida" de una orden de alarma de emergencia activada de
   forma remota desde la PWA (rol administrador). No es la única vía de
   activación: el pulsador físico conectado directamente al ESP32 sigue
   funcionando aunque este endpoint (o el WiFi) no responda.

   Contrato:
     GET  -> { pendiente: bool, origen: string|null, fecha_hora: string|null }
             El ESP32 hace polling de esto cada cierto tiempo (ej. cada
             5-10 s) mientras tiene WiFi.

     POST body { "accion": "activar", "origen": "admin.direccion" }
             -> la PWA la usa para levantar la bandera. Responde el
                estado ya actualizado.

     POST body { "accion": "reconocer" }
             -> el ESP32 la usa apenas termina de sonar el toque largo,
                para bajar la bandera. El ESP32 es quien debe registrar
                la activación real en historial.php (origen "ESP32 (remoto)"),
                este endpoint solo coordina la orden, no reemplaza ese registro.
   ========================================================================== */

declare(strict_types=1);
require __DIR__ . "/config.php";

$pdo = get_db();
$metodo = $_SERVER["REQUEST_METHOD"];

function formatear_alarma(array $fila): array
{
    return [
        "pendiente"  => (bool)$fila["pendiente"],
        "origen"     => $fila["origen"],
        "fecha_hora" => $fila["fecha_hora"],
    ];
}

switch ($metodo) {

    case "GET":
        $fila = $pdo->query("SELECT * FROM alarma_estado WHERE id = 1")->fetch();
        responder(200, formatear_alarma($fila));
        break;

    case "POST":
        $datos = leer_body_json();
        $accion = $datos["accion"] ?? "";

        if ($accion === "activar") {
            $origen = $datos["origen"] ?? "PWA (remoto)";
            $stmt = $pdo->prepare(
                "UPDATE alarma_estado SET pendiente = 1, origen = :origen, fecha_hora = NOW() WHERE id = 1"
            );
            $stmt->execute([":origen" => $origen]);

            // Registro permanente de que la orden fue emitida (queda en el
            // historial aunque el ESP32 todavía no exista/responda). Cuando
            // el ESP32 ejecute el toque largo, agregará su PROPIA fila en
            // historial.php con origen "ESP32 (remoto)": son dos eventos
            // distintos (orden emitida vs. orden ejecutada) y ambos quedan.
            $stmtHist = $pdo->prepare(
                "INSERT INTO historial (fecha, hora, tipo, origen) VALUES (CURDATE(), CURTIME(), 'emergencia', :origen)"
            );
            $origenHistorial = substr("PWA (orden emitida por: {$origen})", 0, 60);
            $stmtHist->execute([":origen" => $origenHistorial]);
        } elseif ($accion === "reconocer") {
            $pdo->exec("UPDATE alarma_estado SET pendiente = 0 WHERE id = 1");
        } else {
            responder_error(400, "El campo 'accion' debe ser 'activar' o 'reconocer'.");
        }

        $fila = $pdo->query("SELECT * FROM alarma_estado WHERE id = 1")->fetch();
        responder(200, formatear_alarma($fila));
        break;

    default:
        responder_error(405, "Método no permitido.");
}
