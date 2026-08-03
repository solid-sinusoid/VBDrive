#include <cstdint>

namespace
{

struct __attribute__((packed)) FirmwareManifest
{
    std::uint32_t magic;
    std::uint16_t format_version;
    std::uint16_t header_size;
    std::uint32_t board_id;
    std::uint32_t config_abi;
    std::uint32_t boot_protocol;
    std::uint32_t app_start;
    std::uint32_t app_end;
    std::uint32_t flags;
};

static_assert(sizeof(FirmwareManifest) == 32U);

// This record is intentionally placed at a fixed address by the linker. The
// host updater and VBBoot both reject application images without an exact
// match, preventing an image built from an incompatible historical branch
// from erasing a working drive.
[[gnu::used, gnu::section(".app_manifest")]]
const FirmwareManifest firmware_manifest{
    .magic = 0x50414256UL,          // "VBAP" in little-endian byte order.
    .format_version = 1U,
    .header_size = sizeof(FirmwareManifest),
    .board_id = 0x31444256UL,       // "VBD1" (VBDrive STM32G431 hardware).
    .config_abi = 0x44AAABFFUL,
    .boot_protocol = 1U,
    .app_start = 0x08003000UL,
    .app_end = 0x0801F800UL,
    .flags = 0U,
};

}  // namespace
