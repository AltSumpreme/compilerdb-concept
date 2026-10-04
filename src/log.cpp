#include "log.h"

#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <stdexcept>

#include "crc32.h"

namespace cgdb {

namespace {

// Little-endian encode/decode helpers (explicit, so the format does not
// depend on host endianness).
void put_u32(std::string& out, uint32_t v) {
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<char>((v >> (8 * i)) & 0xFF));
}
void put_u16(std::string& out, uint16_t v) {
    out.push_back(static_cast<char>(v & 0xFF));
    out.push_back(static_cast<char>((v >> 8) & 0xFF));
}
uint32_t get_u32(const uint8_t* p) {
    return uint32_t{p[0]} | (uint32_t{p[1]} << 8) | (uint32_t{p[2]} << 16) |
           (uint32_t{p[3]} << 24);
}
uint16_t get_u16(const uint8_t* p) { return uint16_t{p[0]} | uint16_t(p[1] << 8); }

std::string encode_node_payload(NodeKind kind, const std::string& name) {
    std::string p;
    p.push_back(static_cast<char>(kind));
    put_u16(p, static_cast<uint16_t>(name.size()));
    p += name;
    return p;
}

std::string encode_edge_payload(EdgeKind kind, const std::string& from,
                                const std::string& to) {
    std::string p;
    p.push_back(static_cast<char>(kind));
    put_u16(p, static_cast<uint16_t>(from.size()));
    p += from;
    put_u16(p, static_cast<uint16_t>(to.size()));
    p += to;
    return p;
}

// Parses a node/edge payload into `rec`. Returns false if malformed.
bool decode_payload(uint8_t type, const uint8_t* p, size_t len, Record& rec) {
    if (len < 1) return false;
    rec.type = type;
    rec.kind = p[0];
    if (type == kRecNode) {
        if (len < 3) return false;
        uint16_t n = get_u16(p + 1);
        if (len != size_t{3} + n) return false;
        rec.from.assign(reinterpret_cast<const char*>(p + 3), n);
        return true;
    }
    if (type == kRecEdge) {
        if (len < 3) return false;
        uint16_t nf = get_u16(p + 1);
        if (len < size_t{3} + nf + 2) return false;
        rec.from.assign(reinterpret_cast<const char*>(p + 3), nf);
        const uint8_t* q = p + 3 + nf;
        uint16_t nt = get_u16(q);
        if (len != size_t{3} + nf + 2 + nt) return false;
        rec.to.assign(reinterpret_cast<const char*>(q + 2), nt);
        return true;
    }
    return false;  // unknown record type
}

void write_all(int fd, const uint8_t* p, size_t len) {
    while (len > 0) {
        ssize_t n = ::write(fd, p, len);
        if (n < 0) {
            if (errno == EINTR) continue;
            throw std::runtime_error(std::string("write: ") + std::strerror(errno));
        }
        p += n;
        len -= static_cast<size_t>(n);
    }
}

}  // namespace

Log::~Log() {
    if (fd_ >= 0) ::close(fd_);
}

Log Log::open(const std::string& path,
              const std::function<void(const Record&)>& visit) {
    Log log;
    log.path_ = path;
    log.fd_ = ::open(path.c_str(), O_RDWR | O_CREAT, 0644);
    if (log.fd_ < 0)
        throw std::runtime_error("open " + path + ": " + std::strerror(errno));

    struct stat st;
    if (::fstat(log.fd_, &st) < 0)
        throw std::runtime_error("fstat: " + std::string(std::strerror(errno)));
    const size_t size = static_cast<size_t>(st.st_size);

    size_t good = 0;  // offset just past the last complete, valid record
    if (size > 0) {
        // Read-only mmap replay; the file is never written through the map.
        void* map = ::mmap(nullptr, size, PROT_READ, MAP_PRIVATE, log.fd_, 0);
        if (map == MAP_FAILED)
            throw std::runtime_error("mmap: " + std::string(std::strerror(errno)));
        const auto* base = static_cast<const uint8_t*>(map);
        size_t off = 0;
        while (off + kHeaderLen <= size) {
            if (get_u32(base + off) != kLogMagic) break;  // corrupt magic
            uint32_t crc = get_u32(base + off + 4);
            uint8_t type = base[off + 8];
            uint32_t plen = get_u32(base + off + 9);
            if (off + kHeaderLen + plen > size) break;    // torn write
            const uint8_t* rec = base + off;
            uint32_t want = crc32_update(0, rec + 8, 1);                 // type
            want = crc32_update(want, rec + 9, 4);                       // payload_len
            want = crc32_update(want, rec + kHeaderLen, plen);           // payload
            if (want != crc) break;                                      // corrupt payload
            Record r;
            if (!decode_payload(type, rec + kHeaderLen, plen, r)) break;
            visit(r);
            off += kHeaderLen + plen;
            good = off;
        }
        ::munmap(map, size);
        if (good < size) {
            // Torn/corrupt tail: drop it so later appends start clean.
            if (::ftruncate(log.fd_, static_cast<off_t>(good)) < 0)
                throw std::runtime_error("ftruncate: " +
                                         std::string(std::strerror(errno)));
        }
    }
    if (::lseek(log.fd_, 0, SEEK_END) < 0)
        throw std::runtime_error("lseek: " + std::string(std::strerror(errno)));
    return log;
}

void Log::append_record(uint8_t type, const std::string& payload) {
    if (payload.size() > 0xFFFFFFFFu) throw std::runtime_error("payload too large");
    std::string buf;
    buf.reserve(kHeaderLen + payload.size());
    put_u32(buf, kLogMagic);
    uint32_t crc = crc32_update(0, &type, 1);
    uint8_t len_le[4];
    for (int i = 0; i < 4; ++i)
        len_le[i] = static_cast<uint8_t>((payload.size() >> (8 * i)) & 0xFF);
    crc = crc32_update(crc, len_le, 4);
    crc = crc32_update(crc, payload.data(), payload.size());
    put_u32(buf, crc);
    buf.push_back(static_cast<char>(type));
    for (int i = 0; i < 4; ++i) buf.push_back(static_cast<char>(len_le[i]));
    buf += payload;
    write_all(fd_, reinterpret_cast<const uint8_t*>(buf.data()), buf.size());
}

void Log::append_node(NodeKind kind, const std::string& name) {
    if (name.size() > 0xFFFFu) throw std::runtime_error("node name too long");
    append_record(kRecNode, encode_node_payload(kind, name));
}

void Log::append_edge(EdgeKind kind, const std::string& from, const std::string& to) {
    if (from.size() > 0xFFFFu || to.size() > 0xFFFFu)
        throw std::runtime_error("edge endpoint too long");
    append_record(kRecEdge, encode_edge_payload(kind, from, to));
}

}  // namespace cgdb
