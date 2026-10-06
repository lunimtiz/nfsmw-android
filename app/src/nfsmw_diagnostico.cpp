#include "nfsmw_diagnostico.h"

#include <rex/logging.h>

#include <chrono>
#include <cstdio>
#include <mutex>

namespace nfsmw::diag {

std::atomic<bool> g_activo{false};

namespace {
std::mutex g_mutex;
std::FILE* g_archivo = nullptr;
unsigned g_numero = 0;
}  // namespace

void Establecer(bool activo, const std::string& carpeta) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (activo == (g_archivo != nullptr)) {
    return;
  }
  if (!activo) {
    g_activo.store(false, std::memory_order_relaxed);
    std::fclose(g_archivo);
    g_archivo = nullptr;
    REXLOG_INFO("[diag] modo diagnostico APAGADO");
    return;
  }
  const auto ahora = std::chrono::system_clock::now().time_since_epoch();
  const long long segundos = std::chrono::duration_cast<std::chrono::seconds>(ahora).count();
  const std::string ruta = carpeta + "/diag_" + std::to_string(segundos) + "_" + std::to_string(++g_numero) + ".log";
  g_archivo = std::fopen(ruta.c_str(), "w");
  if (!g_archivo) {
    REXLOG_ERROR("[diag] no se pudo abrir {}", ruta);
    return;
  }
  // Large buffer: the frame trace is a few MB per second.
  std::setvbuf(g_archivo, nullptr, _IOFBF, 1 << 20);
  g_activo.store(true, std::memory_order_relaxed);
  REXLOG_INFO("[diag] modo diagnostico ENCENDIDO: {}", ruta);
}

void Escribir(const std::string& linea) {
  if (Activo()) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_archivo) {
      std::fwrite(linea.data(), 1, linea.size(), g_archivo);
      std::fputc('\n', g_archivo);
      return;
    }
  }
  REXLOG_INFO("{}", linea);
}

}  // namespace nfsmw::diag
