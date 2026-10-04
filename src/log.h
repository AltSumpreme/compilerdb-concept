#pragma once

// Storage engine: append-only binary log, one CRC32-checksummed record
// per graph mutation.
//
// Record layout (little-endian):
//   u32 magic = 0x43474442 | u32 crc | u8 type | u32 payload_len | payload
// The CRC covers type || payload_len || payload (everything after `crc`).
//
// Payloads:
//   node (type 1): u8 kind | u16 name_len | name bytes
//   edge (type 2): u8 kind | u16 from_len | from | u16 to_len | to
//
// Recovery: the log is replayed through a read-only mmap; replay stops
// silently at the first truncated/corrupt record (torn write) and the
// file is ftruncated back to the last good offset.

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <unistd.h>

#include "graph.h"

namespace cgdb {

constexpr uint32_t kLogMagic = 0x43474442;  // "CGDB"
constexpr uint8_t kRecNode = 1;
constexpr uint8_t kRecEdge = 2;
constexpr size_t kHeaderLen = 4 + 4 + 1 + 4;  // magic|crc|type|payload_len

// Decoded view of one record payload (points into caller-owned memory).
struct Record {
    uint8_t type;
    uint8_t kind;
    std::string from;  // node name, or edge source
    std::string to;    // edge target (empty for node records)
};

class Log {
public:
    Log() = default;
    ~Log();
    Log(const Log&) = delete;
    Log& operator=(const Log&) = delete;
    Log(Log&& o) noexcept : fd_(o.fd_), path_(std::move(o.path_)) { o.fd_ = -1; }
    Log& operator=(Log&& o) noexcept {
        if (this != &o) {
            if (fd_ >= 0) ::close(fd_);
            fd_ = o.fd_;
            path_ = std::move(o.path_);
            o.fd_ = -1;
        }
        return *this;
    }

    // Opens (creating if necessary) the log file. Existing complete
    // records are replayed through `visit` (in file order); a torn or
    // corrupt tail is truncated away. Throws std::runtime_error on
    // hard I/O errors.
    static Log open(const std::string& path,
                    const std::function<void(const Record&)>& visit);

    // Appends one encoded record. Write-through: returns once the bytes
    // are handed to the kernel.
    void append_node(NodeKind kind, const std::string& name);
    void append_edge(EdgeKind kind, const std::string& from, const std::string& to);

    const std::string& path() const { return path_; }

private:
    int fd_ = -1;
    std::string path_;

    void append_record(uint8_t type, const std::string& payload);
};

}  // namespace cgdb
