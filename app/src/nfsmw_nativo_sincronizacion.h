#pragma once

#include <rex/cvar.h>
#include <vulkan/vulkan.h>

#include <atomic>
#include <cstdint>
#include <mutex>

REXCVAR_DECLARE(bool, nfsmw_nativo_sincronizacion_gpu);
REXCVAR_DECLARE(int32_t, nfsmw_nativo_sincronizacion_total);

namespace nfsmw::nativo {

// GENERAL is a layout, not a memory dependency. The native renderer shares
// images between uploads, resolves, reflections and subsequent render passes.
inline constexpr VkPipelineStageFlags kEtapasImagenes =
    VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
    VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
inline constexpr VkAccessFlags kAccesosImagenes =
    VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT |
    VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
    VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
inline constexpr VkPipelineStageFlags kEtapasPase =
    kEtapasImagenes & ~VK_PIPELINE_STAGE_TRANSFER_BIT;
inline constexpr VkAccessFlags kAccesosPase =
    kAccesosImagenes & ~(VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT);

// nfsmw_nativo_sincronizacion_total: every dependency is made as wide as Vulkan allows (all stages, all
// memory accesses) and every transfer command is followed by a full barrier. It tells a missing dependency
// from any other cause of wrong image content, and on Mali it is what makes the reflections stable.
inline std::atomic<bool> g_sincronizacion_total{false};
inline bool SincronizacionTotal() { return g_sincronizacion_total.load(std::memory_order_relaxed); }
// Decides once, with the GPU vendor: -1 (default) = on for Arm GPUs (Mali), off elsewhere; 0 = off; 1 = on.
// On a Mali-G68 the narrower dependencies left the car reflections and the rear-view mirror with valid
// content in ~5% of the frames; with this on, in 100% of them, at no measurable cost in FPS.
inline void ConfigurarSincronizacionTotal(uint32_t vendor_id) {
  static std::once_flag una_vez;
  std::call_once(una_vez, [vendor_id] {
    const int32_t modo = REXCVAR_GET(nfsmw_nativo_sincronizacion_total);
    g_sincronizacion_total.store(modo == 1 || (modo < 0 && vendor_id == 0x13B5), std::memory_order_relaxed);
  });
}
constexpr VkAccessFlags kAccesosTodos = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;

inline void DependenciasImagenes(VkSubpassDependency (&dependencias)[2]) {
  if (SincronizacionTotal()) {
    dependencias[0] = {VK_SUBPASS_EXTERNAL, 0, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                       VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, kAccesosTodos, kAccesosTodos, 0};
    dependencias[1] = {0, VK_SUBPASS_EXTERNAL, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                       VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, kAccesosTodos, kAccesosTodos, 0};
    return;
  }
  dependencias[0] = {VK_SUBPASS_EXTERNAL, 0, kEtapasImagenes, kEtapasPase,
                     kAccesosImagenes, kAccesosPase, 0};
  dependencias[1] = {0, VK_SUBPASS_EXTERNAL, kEtapasPase, kEtapasImagenes,
                     kAccesosPase, kAccesosImagenes, 0};
}

// A full barrier after a transfer command (no-op unless nfsmw_nativo_sincronizacion_total).
inline void BarreraTotal(PFN_vkCmdPipelineBarrier barrera, VkCommandBuffer cmd) {
  if (!SincronizacionTotal() || !barrera || cmd == VK_NULL_HANDLE) {
    return;
  }
  VkMemoryBarrier b{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
  b.srcAccessMask = kAccesosTodos;
  b.dstAccessMask = kAccesosTodos;
  barrera(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &b, 0, nullptr,
          0, nullptr);
}

// Function-pointer commands (obtained with vkGetDeviceProcAddr) are wrapped so each is followed by that barrier.
inline PFN_vkCmdPipelineBarrier g_barrera_fn = nullptr;
inline PFN_vkCmdCopyImage g_copiar_real = nullptr;
inline PFN_vkCmdBlitImage g_blit_real = nullptr;
inline PFN_vkCmdClearDepthStencilImage g_borrar_prof_real = nullptr;
inline void VKAPI_CALL CopiarYBarrera(VkCommandBuffer cmd, VkImage origen, VkImageLayout lo, VkImage destino,
                                      VkImageLayout ld, uint32_t n, const VkImageCopy* r) {
  g_copiar_real(cmd, origen, lo, destino, ld, n, r);
  BarreraTotal(g_barrera_fn, cmd);
}
inline void VKAPI_CALL BlitYBarrera(VkCommandBuffer cmd, VkImage origen, VkImageLayout lo, VkImage destino,
                                    VkImageLayout ld, uint32_t n, const VkImageBlit* r, VkFilter filtro) {
  g_blit_real(cmd, origen, lo, destino, ld, n, r, filtro);
  BarreraTotal(g_barrera_fn, cmd);
}
inline void VKAPI_CALL BorrarProfundidadYBarrera(VkCommandBuffer cmd, VkImage imagen, VkImageLayout layout,
                                                 const VkClearDepthStencilValue* valor, uint32_t n,
                                                 const VkImageSubresourceRange* rangos) {
  g_borrar_prof_real(cmd, imagen, layout, valor, n, rangos);
  BarreraTotal(g_barrera_fn, cmd);
}

}  // namespace nfsmw::nativo
