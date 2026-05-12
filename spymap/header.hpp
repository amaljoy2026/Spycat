#ifndef __SPYCAT_HEADER_HPP__
#define __SPYCAT_HEADER_HPP__

#include <cstring>
#include <cstdint>

#include <boost/interprocess/sync/interprocess_sharable_mutex.hpp>
#include <boost/interprocess/sync/sharable_lock.hpp>
#include <boost/interprocess/sync/scoped_lock.hpp>
#include <boost/interprocess/interprocess_fwd.hpp>

namespace bip = boost::interprocess;

namespace spycat
{

// ── Alignment ─────────────────────────────────────────────────────────────────
//
// All allocations in shared memory are rounded up to SPYCAT_ALIGN bytes.
// This guarantees that every Header and every value buffer starts on an
// address that satisfies the strictest fundamental-type alignment requirement
// of the platform (typically 16 bytes on 64-bit systems), preventing bus
// errors on strict-alignment architectures (ARM, MIPS) and avoiding
// performance penalties on x86.
static constexpr size_t SPYCAT_ALIGN = alignof(std::max_align_t);

inline constexpr size_t align_up(size_t n)
{
    return (n + SPYCAT_ALIGN - 1) & ~(SPYCAT_ALIGN - 1);
}

// ── TypeTag ───────────────────────────────────────────────────────────────────

enum class TypeTag : uint8_t {
    Raw    = 0,
    Double = 1,
    Int64  = 2,
    Bool   = 3,
    String = 4,
};

// ── Header ────────────────────────────────────────────────────────────────────
//
// Represents one entry in the Spycat dictionary.
//
// Headers live in the DIRECTORY — a region that grows DOWNWARD from the top
// of the shared memory segment. Each new header is placed at:
//
//   shm_base + max_size - directory_size   (after directory_size is grown)
//
// The raw value bytes live in the DATA region, which grows UPWARD from just
// after DictHeader. Each Header stores a data_offset pointing into that region.
//
//  shm layout:
//
//  ┌──────────────────────────────────────────┐  offset 0
//  │ DictHeader                               │
//  ├──────────────────────────────────────────┤  offset sizeof(DictHeader)
//  │ Data region  (grows →)                   │
//  │   value bytes entry 0  (align_up sized)  │
//  │   value bytes entry 1                    │
//  │   ...                                    │
//  ├╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌┤  guard gap
//  │ Directory region  (grows ←)              │
//  │   ...                                    │
//  │   Header entry 1                         │
//  │   Header entry 0  (highest address)      │
//  └──────────────────────────────────────────┘  offset max_size
//
//  Overflow condition:
//    data_size + directory_size > max_size - sizeof(DictHeader)
//
// IMPORTANT: Header must always be constructed with placement new.
// It contains an interprocess_sharable_mutex which is NOT trivially
// copyable — never memcpy a Header.
//
// sizeof(Header) is itself align_up'd so that consecutive headers in the
// directory are all naturally aligned.

struct Header
{
    Header(const char* key_, size_t data_size_, TypeTag type_)
        : data_offset(0)
        , data_size(data_size_)
        , value_size(0)
        , type_tag(type_)
        , timestamp_ns(0)
        , source_node(0)
        , override_priority(0)
    {
        std::strncpy(key, key_, sizeof(key) - 1);
        key[sizeof(key) - 1] = '\0';
        // value_mutex is default-constructed in place — correct for interprocess use
    }

    // ── Value lock ────────────────────────────────────────────────────────
    // Guards the value bytes in the data region at data_offset.
    //   Readers: sharable_lock  — many concurrent readers allowed
    //   Writers: scoped_lock    — exclusive, blocks all readers
    // The dict-level structural lock must be acquired before this lock.
    bip::interprocess_sharable_mutex value_mutex;

    // ── Key ───────────────────────────────────────────────────────────────
    char key[256];              // null-terminated, max 255 chars

    // ── Data pointer ──────────────────────────────────────────────────────
    bip::offset_t data_offset;  // byte offset of value bytes from shm_base

    // ── Sizing ────────────────────────────────────────────────────────────
    size_t  data_size;          // allocated capacity in data region (bytes, align_up'd)
    size_t  value_size;         // actual written size (<= data_size)

    // ── Metadata ──────────────────────────────────────────────────────────
    TypeTag  type_tag;          // how to interpret the value bytes
    int64_t  timestamp_ns;      // wall-clock write time, ns since Unix epoch (NTP)
    uint32_t source_node;       // which node last wrote — display metadata only
    int      override_priority; // minimum priority required to overwrite this entry
};

// ── Size of one directory slot ────────────────────────────────────────
// Each slot in the directory is align_up(sizeof(Header)) bytes so that
// all headers are naturally aligned regardless of their position.
static const size_t HEADER_SLOT_SIZE = align_up(sizeof(Header));

// Compile-time check: DictHeader must itself be a multiple of SPYCAT_ALIGN
// so the data region starts aligned. Enforced in spymap.hpp via static_assert
// on DictHeader, but the same rule applies to Header slot sizing above.
static_assert(HEADER_SLOT_SIZE % SPYCAT_ALIGN == 0,
    "Header::slot_size must be a multiple of SPYCAT_ALIGN");

} // namespace spycat
#endif // __SPYCAT_HEADER_HPP__
