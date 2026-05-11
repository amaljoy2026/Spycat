#ifndef __SPYMAP_HEADER_HPP__
#define __SPYMAP_HEADER_HPP__

#include <cstring>
#include <cstdint>
#include <boost/interprocess/sync/interprocess_sharable_mutex.hpp>
#include <boost/interprocess/sync/sharable_lock.hpp>
#include <boost/interprocess/sync/scoped_lock.hpp>

namespace bip = boost::interprocess;

namespace spycat
{

// Type tags for values stored in the dict
enum class TypeTag : uint8_t {
    Raw    = 0,
    Double = 1,
    Int64  = 2,
    Bool   = 3,
    String = 4,
};

// Per-entry header, lives in shared memory immediately before the value bytes.
// Guards its own value with an interprocess_sharable_mutex:
//   - Readers take a sharable lock  (many concurrent readers allowed)
//   - Writers take an exclusive lock (no readers or writers during write)
//
// The dict-level structural lock (interprocess_upgradable_mutex) must always
// be acquired BEFORE this lock to prevent deadlocks.
struct Header
{
    // Construct a new header in-place (use placement new, never memcpy).
    Header(const char* key, size_t data_size, TypeTag type = TypeTag::Raw)
        : data_size(data_size)
        , value_size(0)
        , type_tag(type)
        , timestamp_ns(0)
        , source_node(0)
    {
        strncpy(this->key, key, sizeof(this->key) - 1);
        this->key[sizeof(this->key) - 1] = '\0';
        // value_mutex default-constructed in place — correct for interprocess use
    }

    bip::interprocess_sharable_mutex value_mutex; // guards value bytes only
    char     key[256];        // null-terminated, max 255 chars
    size_t   data_size;       // allocated capacity for value bytes
    size_t   value_size;      // actual written size (<= data_size)
    TypeTag  type_tag;        // how to interpret the value bytes
    int64_t  timestamp_ns;    // wall-clock write time, nanoseconds since epoch
    uint32_t source_node;     // display metadata — which node last wrote this
};

} // namespace spycat
#endif // __SPYMAP_HEADER_HPP__
