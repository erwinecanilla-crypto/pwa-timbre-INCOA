/* Service Worker — Timbre Escolar Automático (INCOA)
   Cachea el "app shell" (HTML, CSS, JS, manifest, icono) para que la
   interfaz de administración pueda abrirse sin conexión. Las llamadas a
   la API en PHP (horarios/historial) NO se cachean aquí a propósito:
   deben ir siempre a la red para reflejar el estado real del backend.
   Suban el número de CACHE_VERSION cada vez que cambien estos archivos,
   para que los navegadores descarguen la versión nueva. */

const CACHE_VERSION = "timbre-shell-v4";
const APP_SHELL = [
  "./",
  "./index.html",
  "./css/style.css",
  "./js/app.js",
  "./manifest.json",
  "./icons/icon.svg",
];

self.addEventListener("install", (event) => {
  event.waitUntil(
    caches.open(CACHE_VERSION).then((cache) => cache.addAll(APP_SHELL))
  );
  self.skipWaiting();
});

self.addEventListener("activate", (event) => {
  event.waitUntil(
    caches.keys().then((keys) =>
      Promise.all(keys.filter((k) => k !== CACHE_VERSION).map((k) => caches.delete(k)))
    )
  );
  self.clients.claim();
});

self.addEventListener("fetch", (event) => {
  const url = new URL(event.request.url);

  // No interceptar llamadas a la API: siempre red.
  if (url.pathname.includes("/api/")) return;

  event.respondWith(
    caches.match(event.request).then((cached) => cached || fetch(event.request))
  );
});
