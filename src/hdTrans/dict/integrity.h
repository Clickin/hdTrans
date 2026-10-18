#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace hdTrans::dict::internal {

class Sha256 {
public:
    void Update(std::span<const std::byte> data);
    // Consumes the state; do not call Update or Finish afterward.
    std::array<std::byte, 32> Finish();

private:
    void Compress(const std::byte* block);

    std::array<uint32_t, 8> state_ = {
        0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
        0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u,
    };
    std::array<std::byte, 64> buffer_{};
    uint64_t byteCount_ = 0;
    std::size_t buffered_ = 0;
};

std::array<std::byte, 32> Hash256(std::span<const std::byte> data);
uint32_t Crc32c(std::span<const std::byte> data);

}  // namespace hdTrans::dict::internal
