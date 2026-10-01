/* ==========================================================================
   Timbre Escolar Automático — INCOA
   app.js

   CAPA DE DATOS
   -------------
   Por ahora la app trabaja con datos guardados en localStorage, para poder
   desarrollar y probar toda la PWA sin depender del ESP32 ni del backend
   en PHP. Cuando la API en PHP esté lista, hay que:

     1. Cambiar USE_MOCK_DATA a false.
     2. Ajustar API_BASE a la URL real (ej. "http:///pwa-timbre/api").
     3. Confirmar que los endpoints devuelven exactamente el mismo formato
        de objeto que usan las funciones mockGetHorarios / mockGetHistorial
        de abajo (ver el contrato de datos comentado junto a cada una).

   El resto de la aplicación (render, formulario, pestañas) no debería
   necesitar cambios cuando se haga ese reemplazo.
   ========================================================================== */

const USE_MOCK_DATA = false;
const API_BASE = "api"; // ajustar cuando exista el backend

const STORAGE_KEY_HORARIOS = "timbre_horarios";
const STORAGE_KEY_HISTORIAL = "timbre_historial";
const STORAGE_KEY_ALARMA = "timbre_alarma";

// ---------------------------------------------------------------------
// Datos de ejemplo iniciales (solo se usan la primera vez que se abre)
// ---------------------------------------------------------------------
const HORARIOS_DEMO = [
  { id: "1", hora: "07:00", tipo: "clase", dias: ["L", "M", "X", "J", "V"], activo: true },
  { id: "2", hora: "09:30", tipo: "recreo", dias: ["L", "M", "X", "J", "V"], activo: true },
  { id: "3", hora: "12:00", tipo: "salida", dias: ["L", "M", "X", "J", "V"], activo: true },
];

const HISTORIAL_DEMO = [
  { id: "h1", hora: "07:00:02", fecha: "2026-09-19", tipo: "clase", origen: "Arduino Uno" },
  { id: "h2", hora: "09:30:01", fecha: "2026-09-19", tipo: "recreo", origen: "Arduino Uno" },
];

// ---------------------------------------------------------------------
// Capa de acceso a datos (mock hoy, fetch() a la API en PHP mañana)
// ---------------------------------------------------------------------

function seedIfEmpty() {
  if (!localStorage.getItem(STORAGE_KEY_HORARIOS)) {
    localStorage.setItem(STORAGE_KEY_HORARIOS, JSON.stringify(HORARIOS_DEMO));
  }
  if (!localStorage.getItem(STORAGE_KEY_HISTORIAL)) {
    localStorage.setItem(STORAGE_KEY_HISTORIAL, JSON.stringify(HISTORIAL_DEMO));
  }
}

// Contrato esperado del endpoint GET {API_BASE}/horarios.php
// -> [{ id, hora: "HH:MM", tipo: "clase|recreo|salida|emergencia",
//       dias: ["L","M","X","J","V"], activo: true|false }, ...]
async function getHorarios() {
  if (USE_MOCK_DATA) {
    return JSON.parse(localStorage.getItem(STORAGE_KEY_HORARIOS) || "[]");
  }
  const res = await fetch(`${API_BASE}/horarios.php`);
  return res.json();
}

async function guardarHorario(horario) {
  if (USE_MOCK_DATA) {
    const horarios = await getHorarios();
    if (horario.id) {
      const idx = horarios.findIndex((h) => h.id === horario.id);
      horarios[idx] = horario;
    } else {
      horario.id = crypto.randomUUID();
      horarios.push(horario);
    }
    localStorage.setItem(STORAGE_KEY_HORARIOS, JSON.stringify(horarios));
    return horario;
  }
  const res = await fetch(`${API_BASE}/horarios.php`, {
    method: horario.id ? "PUT" : "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(horario),
  });
  return res.json();
}

async function eliminarHorario(id) {
  if (USE_MOCK_DATA) {
    const horarios = (await getHorarios()).filter((h) => h.id !== id);
    localStorage.setItem(STORAGE_KEY_HORARIOS, JSON.stringify(horarios));
    return;
  }
  await fetch(`${API_BASE}/horarios.php?id=${id}`, { method: "DELETE" });
}

// Contrato esperado del endpoint GET {API_BASE}/historial.php
// -> [{ id, fecha: "YYYY-MM-DD", hora: "HH:MM:SS", tipo, origen }, ...]
async function getHistorial() {
  if (USE_MOCK_DATA) {
    return JSON.parse(localStorage.getItem(STORAGE_KEY_HISTORIAL) || "[]");
  }
  const res = await fetch(`${API_BASE}/historial.php`);
  return res.json();
}

async function registrarActivacion(entrada) {
  if (USE_MOCK_DATA) {
    const historial = await getHistorial();
    entrada.id = crypto.randomUUID();
    historial.unshift(entrada);
    localStorage.setItem(STORAGE_KEY_HISTORIAL, JSON.stringify(historial));
    return entrada;
  }
  const res = await fetch(`${API_BASE}/historial.php`, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(entrada),
  });
  return res.json();
}

// Contrato esperado del endpoint GET {API_BASE}/alarma.php
// -> { pendiente: bool, origen: string|null, fecha_hora: string|null }
async function getAlarmaEstado() {
  if (USE_MOCK_DATA) {
    return JSON.parse(
      localStorage.getItem(STORAGE_KEY_ALARMA) ||
        '{"pendiente":false,"origen":null,"fecha_hora":null}'
    );
  }
  const res = await fetch(`${API_BASE}/alarma.php`);
  return res.json();
}

// Activación remota de la alarma de emergencia (rol administrador).
// En hardware real, esto solo levanta una bandera: es el ESP32 quien la
// detecta (por polling), hace sonar el toque largo y registra el
// historial. Aquí, en modo mock, simulamos ese resultado de una vez para
// poder ver el flujo completo sin depender del ESP32.
async function activarAlarmaRemota(origen) {
  if (USE_MOCK_DATA) {
    const ahora = new Date();
    localStorage.setItem(
      STORAGE_KEY_ALARMA,
      JSON.stringify({ pendiente: false, origen, fecha_hora: ahora.toISOString() })
    );
    await registrarActivacion({
      fecha: ahora.toISOString().slice(0, 10),
      hora: ahora.toTimeString().slice(0, 8),
      tipo: "emergencia",
      origen: "PWA (remoto, simulado)",
    });
    return { pendiente: false, origen, fecha_hora: ahora.toISOString() };
  }
  const res = await fetch(`${API_BASE}/alarma.php`, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ accion: "activar", origen }),
  });
  return res.json();
}

// ---------------------------------------------------------------------
// Utilidades de presentación
// ---------------------------------------------------------------------

const ETIQUETAS_TIPO = {
  clase: "Cambio de clase",
  recreo: "Recreo",
  salida: "Salida",
  entrada: "Entrada a clases",
  emergencia: "Emergencia",
};

// Patrón sonoro asociado a cada tipo de evento (sección 4 del proyecto):
// corto = cambio de clase/entrada, medio = receso/salida, largo = alarma.
const PATRONES_SONIDO = {
  clase: "Toque corto (~3 s)",
  entrada: "Toque corto (~3 s)",
  recreo: "Toque medio (~8-10 s)",
  salida: "Toque medio (~8-10 s)",
  emergencia: "Toque largo intermitente (≥30 s)",
};

const ETIQUETAS_DIA = { L: "Lun", M: "Mar", X: "Mié", J: "Jue", V: "Vie" };

function formatearDias(dias) {
  return dias.map((d) => ETIQUETAS_DIA[d] || d).join(", ");
}

// ---------------------------------------------------------------------
// Render: Horarios
// ---------------------------------------------------------------------

async function renderHorarios() {
  const horarios = await getHorarios();
  const cuerpo = document.getElementById("cuerpoHorarios");
  const vacio = document.getElementById("estadoVacioHorarios");

  cuerpo.innerHTML = "";
  vacio.hidden = horarios.length > 0;

  horarios
    .sort((a, b) => a.hora.localeCompare(b.hora))
    .forEach((h) => {
      const tr = document.createElement("tr");
      tr.innerHTML = `
        <td>${h.hora}</td>
        <td>
          <span class="badge ${h.tipo === "emergencia" ? "emergencia" : ""}"
                title="${PATRONES_SONIDO[h.tipo] || ""}">${ETIQUETAS_TIPO[h.tipo] || h.tipo}</span>
        </td>
        <td>${formatearDias(h.dias)}</td>
        <td>${h.activo ? "Activo" : "Inactivo"}</td>
        <td class="row-actions">
          <button class="btn-secondary btn-small" data-action="editar" data-id="${h.id}">Editar</button>
          <button class="btn-danger btn-small" data-action="eliminar" data-id="${h.id}">Eliminar</button>
        </td>
      `;
      cuerpo.appendChild(tr);
    });
}

document.getElementById("cuerpoHorarios").addEventListener("click", async (ev) => {
  const btn = ev.target.closest("button[data-action]");
  if (!btn) return;
  const id = btn.dataset.id;

  if (btn.dataset.action === "eliminar") {
    if (confirm("¿Eliminar este horario?")) {
      await eliminarHorario(id);
      await renderHorarios();
    }
  }

  if (btn.dataset.action === "editar") {
    const horarios = await getHorarios();
    const h = horarios.find((x) => x.id === id);
    if (!h) return;
    document.getElementById("horarioId").value = h.id;
    document.getElementById("horarioHora").value = h.hora;
    document.getElementById("horarioTipo").value = h.tipo;
    document.querySelectorAll(".day-chip").forEach((chip) => {
      chip.classList.toggle("is-active", h.dias.includes(chip.dataset.day));
    });
    document.getElementById("btnCancelarEdicion").hidden = false;
    window.scrollTo({ top: 0, behavior: "smooth" });
  }
});

// ---------------------------------------------------------------------
// Formulario de horario
// ---------------------------------------------------------------------

document.querySelectorAll(".day-chip").forEach((chip) => {
  chip.addEventListener("click", () => chip.classList.toggle("is-active"));
});

document.getElementById("btnCancelarEdicion").addEventListener("click", () => {
  resetFormularioHorario();
});

function resetFormularioHorario() {
  document.getElementById("formHorario").reset();
  document.getElementById("horarioId").value = "";
  document.querySelectorAll(".day-chip").forEach((chip) => chip.classList.remove("is-active"));
  document.getElementById("btnCancelarEdicion").hidden = true;
}

document.getElementById("formHorario").addEventListener("submit", async (ev) => {
  ev.preventDefault();

  const dias = Array.from(document.querySelectorAll(".day-chip.is-active")).map((c) => c.dataset.day);
  if (dias.length === 0) {
    alert("Seleccione al menos un día.");
    return;
  }

  const horario = {
    id: document.getElementById("horarioId").value || null,
    hora: document.getElementById("horarioHora").value,
    tipo: document.getElementById("horarioTipo").value,
    dias,
    activo: true,
  };

  await guardarHorario(horario);
  resetFormularioHorario();
  await renderHorarios();
});

// ---------------------------------------------------------------------
// Render: Historial
// ---------------------------------------------------------------------

async function renderHistorial() {
  const historial = await getHistorial();
  const contenedor = document.getElementById("listaHistorial");
  const vacio = document.getElementById("estadoVacioHistorial");

  contenedor.innerHTML = "";
  vacio.hidden = historial.length > 0;

  historial.forEach((entrada) => {
    const div = document.createElement("div");
    div.className = "history-item";
    div.innerHTML = `
      <div>
        <div>${ETIQUETAS_TIPO[entrada.tipo] || entrada.tipo}</div>
        <div class="history-item__meta">${entrada.fecha} · ${entrada.hora} · ${entrada.origen}</div>
      </div>
    `;
    contenedor.appendChild(div);
  });
}

document.getElementById("btnSimularActivacion").addEventListener("click", async () => {
  const ahora = new Date();
  await registrarActivacion({
    fecha: ahora.toISOString().slice(0, 10),
    hora: ahora.toTimeString().slice(0, 8),
    tipo: "clase",
    origen: "Simulación manual",
  });
  await renderHistorial();
});

// ---------------------------------------------------------------------
// Alarma de emergencia (activación remota, botón siempre visible)
// ---------------------------------------------------------------------

document.getElementById("btnActivarAlarma").addEventListener("click", async () => {
  const confirmado = confirm(
    "¿Confirma la activación de la ALARMA DE EMERGENCIA?\n\n" +
      "Esto hará sonar el toque largo intermitente en todo el plantel. " +
      "Úsela solo ante un sismo, evacuación u otra emergencia real, o para " +
      "un simulacro coordinado."
  );
  if (!confirmado) return;

  const usuario = prompt("Nombre o cargo de quien activa la alarma (para el registro):", "Dirección");
  if (usuario === null) return; // canceló

  const btn = document.getElementById("btnActivarAlarma");
  btn.disabled = true;
  try {
    await activarAlarmaRemota(usuario || "No especificado");
    alert(
      USE_MOCK_DATA
        ? "Alarma simulada y registrada en el historial."
        : "Orden de alarma enviada. El ESP32 la ejecutará en su próximo ciclo de " +
            "revisión (unos segundos) si tiene conexión; el pulsador físico en el " +
            "plantel sigue siendo la vía inmediata si la red llegara a fallar."
    );
    if (document.getElementById("panelHistorial").classList.contains("is-active")) {
      await renderHistorial();
    }
  } catch (err) {
    alert("No se pudo enviar la orden de alarma. Verifique la conexión con la API.");
    console.error(err);
  } finally {
    btn.disabled = false;
  }
});

// ---------------------------------------------------------------------
// Pestañas
// ---------------------------------------------------------------------

const tabs = {
  tabHorarios: "panelHorarios",
  tabHistorial: "panelHistorial",
};

Object.keys(tabs).forEach((tabId) => {
  document.getElementById(tabId).addEventListener("click", () => {
    Object.entries(tabs).forEach(([id, panelId]) => {
      const isActive = id === tabId;
      document.getElementById(id).setAttribute("aria-selected", isActive);
      document.getElementById(panelId).classList.toggle("is-active", isActive);
    });
    if (tabId === "tabHistorial") renderHistorial();
  });
});

// ---------------------------------------------------------------------
// Estado de conexión (útil para probar el comportamiento offline)
// ---------------------------------------------------------------------

function actualizarEstadoConexion() {
  const dot = document.getElementById("statusDot");
  const texto = document.getElementById("statusText");
  const online = navigator.onLine;
  dot.classList.toggle("offline", !online);
  texto.textContent = online ? "Conectado" : "Sin conexión (modo offline)";
}

window.addEventListener("online", actualizarEstadoConexion);
window.addEventListener("offline", actualizarEstadoConexion);

// ---------------------------------------------------------------------
// Inicialización
// ---------------------------------------------------------------------

if (USE_MOCK_DATA) seedIfEmpty();
actualizarEstadoConexion();
renderHorarios();

if ("serviceWorker" in navigator) {
  navigator.serviceWorker.register("sw.js").catch((err) => {
    console.warn("No se pudo registrar el service worker:", err);
  });
}
