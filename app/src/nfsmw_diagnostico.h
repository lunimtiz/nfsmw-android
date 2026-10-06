// Wide diagnostic mode, toggled at run time (the DIAG button of the touch pad on Android).
// While it is on, the renderer's frame trace runs every few frames with no line limit and goes to a file in the
// game folder instead of the log, so it can be read afterwards without logcat dropping lines.
#pragma once

#include <atomic>
#include <string>

namespace nfsmw::diag {

extern std::atomic<bool> g_activo;

inline bool Activo() { return g_activo.load(std::memory_order_relaxed); }

// Lines a traced frame may have: 4000 normally, effectively unlimited in the wide mode.
inline unsigned TrazaMax() { return Activo() ? 200000u : 4000u; }

// Opens (on) or closes (off) the output file `<dir>/diag_<n>.log`. Safe to call from any thread.
void Establecer(bool activo, const std::string& carpeta);

// One line to the file when the mode is on, to the log otherwise.
void Escribir(const std::string& linea);

}  // namespace nfsmw::diag
