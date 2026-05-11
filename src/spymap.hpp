#ifndef __SPYMAP_HPP__
#define __SPYMAP_HPP__

#include <string>
#include <unordered_map>
#include <shared_mutex>   // std::shared_mutex — thread-level safety
#include <cstdint>

#include <boost/interprocess/shared_memory_object.hpp>
#include <boost/interprocess/mapped_region.hpp>
#include <boost/interprocess/sync/interprocess_upgradable_mutex.hpp>
#include <boost/interprocess/sync/sharable_lock.hpp>
#include <boost/interprocess/sync/upgradable_lock.hpp>
#include <boost/interprocess/sync/scoped_lock.hpp>

#include "header.hpp"

namespace bip = boost::interprocess;

namespace spycat
{

// DictHeader lives at byte 0 of the shared memory segment.
// It holds the structural (dict-level) lock and bookkeeping metadata.
//
// Two-level locking protocol — always acquire in this order:
//
//   1. Thread-level  : Spymap::thread_mutex_  (std::shared_mutex)
//   2. Process-level : DictHeader::dict_mutex (bip::interprocess_upgradable_mutex)
//   3. Per-entry     : Header::value_mutex    (bip::interprocess_sharable_mutex)
//
// Lock modes per operation:
//
//   Operation          thread_mutex_   dict_mutex    value_mutex
//   ─────────────────────────────────────────────────────────────
//   get()              shared          sharable      sharable
//   set() existing     shared          sharable      exclusive
//   set() new key      unique          exclusive     n/a (not yet visible)
struct DictHeader
{
    DictHeader() : used_bytes(sizeof(DictHeader)) {}

    bip::interprocess_upgradable_mutex dict_mutex; // structural lock
    size_t used_bytes;   // bump-allocator cursor (bytes used so far)
};

class Spymap
{
public:
    // Opens or creates a shared memory segment with the given name.
    // The segment is preallocated to max_size bytes of virtual address
    // space — no growing or remapping ever occurs.
    // Default 256 MB: large virtual range, but physical pages are only
    // faulted in as entries are actually written.
    explicit Spymap(const std::string& name,
                    size_t max_size = 256 * 1024 * 1024);
    ~Spymap();

    // Store a raw value under key.
    // If the key exists and new size <= allocated capacity, updates in place.
    // If the key is new, appends to the segment (exclusive lock).
    // If the key exists but new size > capacity, logs a warning and no-ops
    // (fixed-capacity entries — preallocate with reserve() for variable data).
    void set(const char*        key,
             const void*        value,
             size_t             size,
             TypeTag            type = TypeTag::Raw);

    void set(const std::string& key,
             const void*        value,
             size_t             size,
             TypeTag            type = TypeTag::Raw);

    // Convenience typed setters
    void set(const std::string& key, double      value);
    void set(const std::string& key, int64_t     value);
    void set(const std::string& key, bool        value);
    void set(const std::string& key, const std::string& value);

    // Copy value into buffer. Returns false if key not found.
    bool get(const char*        key, void* buffer, size_t buffer_size);
    bool get(const std::string& key, void* buffer, size_t buffer_size);

    // Typed getters — return default_val if key not found or type mismatch
    double      get_double(const std::string& key, double      default_val = 0.0);
    int64_t     get_int64 (const std::string& key, int64_t     default_val = 0);
    bool        get_bool  (const std::string& key, bool        default_val = false);
    std::string get_string(const std::string& key, const std::string& default_val = "");

    // Remove the named shared memory segment from the OS.
    // Call once at program exit or from the inspector cleanup action.
    // After this all attached Spymap instances become invalid.
    static void destroy(const std::string& name);

private:
    // Returns byte offset of the Header for key, or -1 if not found.
    // Caller must hold at least a sharable dict lock before calling.
    bip::offset_t find(const char* key) const;

    // Raw pointer helpers — all arithmetic in char* to avoid scaling
    char*       shm_base()       { return static_cast<char*>(region_->get_address()); }
    const char* shm_base() const { return static_cast<const char*>(region_->get_address()); }

    DictHeader* dict_header() {
        return reinterpret_cast<DictHeader*>(shm_base());
    }

    Header* header_at(bip::offset_t offset) {
        return reinterpret_cast<Header*>(shm_base() + offset);
    }

    const Header* header_at(bip::offset_t offset) const {
        return reinterpret_cast<const Header*>(shm_base() + offset);
    }

    // ── Data members ──────────────────────────────────────────────────────
    std::string  name_;
    size_t       max_size_;

    // Thread-level lock — first in acquisition order
    mutable std::shared_mutex thread_mutex_;

    // Per-process offset cache — keyed by entry string, value is byte offset
    // to the Header in shared memory. Treated as a hint: always fall back to
    // find() on a miss since another process may have added entries.
    std::unordered_map<std::string, bip::offset_t> offset_cache_;

    std::shared_ptr<bip::shared_memory_object> shmobj_;
    std::shared_ptr<bip::mapped_region>        region_;
};

} // namespace spycat
#endif // __SPYMAP_HPP__
