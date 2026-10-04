#pragma once

// Hand-rolled CRC32 (IEEE 802.3 polynomial, table-driven).
// No third-party code: the 256-entry table is generated at first use
// from the polynomial 0xEDB88320 (reflected form of 0x04C11DB7).

#include <cstddef>
#include <cstdint>

namespace cgdb {

// One-shot CRC32 over a byte range.
uint32_t crc32(const void* data, size_t len);

// Incremental CRC32: pass the value returned by the previous call as
// `crc` to continue a running checksum. Initial call uses crc = 0.
uint32_t crc32_update(uint32_t crc, const void* data, size_t len);

}  // namespace cgdb
