// nfsmw - the game's render command queue, with the memory ordering ARM64 needs.
//
// The game's two main threads share a queue of deferred calls at 0x82909650 (the "list"):
//   +0  commands written by the preparer      +4  commands executed by the executor
//   +8  the preparer has closed the list      +12 the list is open (0 = calls run at once)
//   +20 where the next command goes           +24 the next command to run
// Each command is { function, size, arguments... } and the next one starts `size` bytes later.
//
// The preparer (sub_823C8378) copies the command, stores its function and size, moves +20 and only then
// bumps +0. The executor (sub_823C83F8) sees +0 != +4, reads the command and calls its function. On the
// Xbox 360 the stores became visible in order. On ARM64 they do not: the executor can see the new +0 before
// the command's own words, read a function pointer of 0 from memory that was never written yet, and the
// runtime aborts with "Call to invalid or unregistered function at guest address 0x00000000" (Galaxy A26,
// 4 minutes into a race, always in sub_82441CC8 -> sub_823C83F8 on the executor thread).
//
// Here the preparer publishes +0 after a release fence and the executor reads the command after an acquire
// fence. The logic is the original's, instruction for instruction. A command whose function is really 0
// (not a visibility problem: its size is there too) is skipped instead of aborting the game, and logged.

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/platform.h>
// The generated code's macros: REX_LOAD_*, REX_STORE_*, REX_RAW_ADDR and REX_CALL_INDIRECT_FUNC.
#include "generated/default/nfsmw_pch.h"

#include <atomic>
#include <cstdint>
#include <cstring>

REXCVAR_DEFINE_BOOL(nfsmw_cola_render_ordenada, REX_PLATFORM_ANDROID != 0, "NFSMW",
                    "Cola de comandos de render del juego (sub_823C8378 la llena, sub_823C83F8 la ejecuta) con barreras "
                    "de memoria de liberacion y adquisicion. Sin ellas, en ARM64 el ejecutor puede ver el contador "
                    "subido antes que el comando y llamar a la direccion 0 (cierre del juego a los minutos). false = "
                    "las originales")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

REX_EXTERN(__imp__sub_823C8378);
REX_EXTERN(__imp__sub_823C83F8);

namespace {

constexpr uint32_t kLista = 0x82909650;
constexpr uint32_t kRetornoProductor = 0x823C83B0;  // the return address of the original's indirect call
constexpr uint32_t kRetornoConsumidor = 0x823C8460;

bool Activa() {
  static const bool activa = REXCVAR_GET(nfsmw_cola_render_ordenada);
  return activa;
}

std::atomic<uint32_t> g_nulas_encoladas{0};
std::atomic<uint32_t> g_nulas_saltadas{0};

}  // namespace

// The executor: runs up to r4 commands. The same loop as the original, reading each command after an acquire
// fence on the counter.
void NfsmwColaRenderConsumir(PPCContext& ctx, uint8_t* base) {
  if (!Activa()) {
    __imp__sub_823C83F8(ctx, base);
    return;
  }
  if (REX_LOAD_U32(kLista + 12) == 0) {
    return;  // the list is not open
  }
  uint32_t restantes = ctx.r4.u32;
  if (restantes == 0) {
    return;
  }
  do {
    --restantes;
    const uint32_t escritas = REX_LOAD_U32(kLista + 0);
    std::atomic_thread_fence(std::memory_order_acquire);  // the command below is at least as new as +0
    const uint32_t ejecutadas = REX_LOAD_U32(kLista + 4);
    const uint32_t actual = REX_LOAD_U32(kLista + 24);
    const uint32_t fin = REX_LOAD_U32(kLista + 20);
    if (!(actual < fin) || escritas == ejecutadas) {
      break;
    }
    const uint32_t funcion = REX_LOAD_U32(actual + 0);
    const uint32_t tamano = REX_LOAD_U32(actual + 4);
    if (funcion == 0) {
      if (tamano == 0) {
        break;  // not written yet: the game's loop asks again
      }
      if (g_nulas_saltadas.fetch_add(1, std::memory_order_relaxed) < 8) {
        REXLOG_ERROR("[cola_render] comando con funcion 0 en {:08X} ({} bytes): se salta en vez de abortar", actual,
                     tamano);
      }
    } else {
      ctx.r3.u64 = actual;
      ctx.lr = kRetornoConsumidor;
      REX_CALL_INDIRECT_FUNC(funcion);
    }
    REX_STORE_U32(kLista + 24, REX_LOAD_U32(actual + 4) + actual);
    const uint32_t n = REX_LOAD_U32(kLista + 4) + 1;
    REX_STORE_U32(kLista + 4, n);
    ctx.r3.u64 = n;
  } while (restantes != 0);
}

// The preparer: r4 = argument block, r5 = function, r6 = size of the block.
REX_HOOK_RAW(sub_823C8378) {
  if (!Activa()) {
    __imp__sub_823C8378(ctx, base);
    return;
  }
  const uint32_t datos = ctx.r4.u32;
  const uint32_t funcion = ctx.r5.u32;
  if (funcion == 0) {
    // Nothing valid to queue or to run. The original would store it and the executor would call address 0.
    if (g_nulas_encoladas.fetch_add(1, std::memory_order_relaxed) < 8) {
      REXLOG_ERROR("[cola_render] se pide encolar la funcion 0 (llamante {:08X}): se descarta", uint32_t(ctx.lr));
    }
    return;
  }
  if (REX_LOAD_U32(kLista + 12) == 0) {  // list closed: the call runs now
    ctx.r3.u64 = datos;
    ctx.lr = kRetornoProductor;
    REX_CALL_INDIRECT_FUNC(funcion);
    return;
  }
  const uint32_t destino = REX_LOAD_U32(kLista + 20);
  const uint32_t tamano = (ctx.r6.u32 + 15) & 0xFFFFFFF0u;
  std::memmove(REX_RAW_ADDR(destino), REX_RAW_ADDR(datos), tamano);
  REX_STORE_U32(destino + 0, funcion);
  REX_STORE_U32(destino + 4, tamano);
  REX_STORE_U32(kLista + 20, destino + tamano);
  // Everything above is visible before the executor can see the new count.
  std::atomic_thread_fence(std::memory_order_release);
  REX_STORE_U32(kLista + 0, REX_LOAD_U32(kLista + 0) + 1);
  ctx.r3.u64 = destino;
}
