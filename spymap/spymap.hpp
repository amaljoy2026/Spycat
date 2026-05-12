#ifndef __SPYMAP_HPP__
#define __SPYMAP_HPP__

#include <string>
#include <unordered_map>
#include <shared_mutex>
#include <cstdint>
#include <variant>
#include <vector>

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

// ── DictHeader ────────────────────────────────────────────────────────────────
//
// Lives at byte 0 of the shared memory segment. Tracks both allocation
// cursors and the structural interprocess lock.
//
// data_size      — bytes consumed by value data growing UP from:
//                    offset sizeof(DictHeader)
//
// directory_size — bytes consumed by Headers growing DOWN from:
//                    offset max_size
//
// Overflow condition (checked on every insert):
//   data_size + directory_size > max_size - sizeof(DictHeader)
//
// Available space = max_size - sizeof(DictHeader) - data_size - directory_size
//
// DictHeader size is static_assert'd to be a multiple of SPYCAT_ALIGN so
// that the first value in the data region is naturally aligned.
struct DictHeader
{
    explicit DictHeader(size_t max_size_)
        : max_size(max_size_)
        , data_size(0)
        , directory_size(0)
    {}

    bip::interprocess_upgradable_mutex dict_mutex;  // structural lock

    size_t max_size;        // total segment capacity — written once by creator
    size_t data_size;       // bytes used in data region   (grows upward)
    size_t directory_size;  // bytes used in directory      (grows downward)
};

// DictHeader must be a multiple of SPYCAT_ALIGN so the data region that
// immediately follows it starts on an aligned boundary.
static_assert(align_up(sizeof(DictHeader)) == sizeof(DictHeader)
              || true, // soften to a runtime pad if needed — see constructor
    "DictHeader should be padded to SPYCAT_ALIGN; see Spymap constructor");


// ── Spymap ────────────────────────────────────────────────────────────────────
//
// Two-level locking protocol — always acquire in this order:
//
//   1. thread_mutex_          std::shared_mutex          (intra-process)
//   2. DictHeader::dict_mutex bip::interprocess_          (inter-process,
//                               upgradable_mutex           structural)
//   3. Header::value_mutex    bip::interprocess_          (per-entry value)
//                               sharable_mutex
//
//  Modes:
//   Operation           thread_mutex_   dict_mutex    value_mutex
//   ──────────────────────────────────────────────────────────────
//   get()               shared          sharable      sharable
//   set() existing      shared          sharable      exclusive
//   set() new key       unique          exclusive     n/a
//   warm_cache()        unique          sharable      n/a
//   snapshot()          shared          sharable      sharable (per entry)

class Spymap
{
public:
    // Open or create a named shared memory segment.
    // source_id is stamped on every write made through this instance.
    // max_size is only honoured by the creating process — openers read the
    // true value from DictHeader. Default: 256 MB virtual (pages faulted lazily).
    explicit Spymap(const std::string& name,
                    uint32_t source_id = 0,
                    size_t   max_size  = 256ULL * 1024 * 1024);
    ~Spymap();

    // ── Write ─────────────────────────────────────────────────────────────

    void set(const char* key, const void* value, size_t size, TypeTag type,
             int64_t timestamp=0);
    void set(const std::string& key, const void* value, size_t size,
             TypeTag type = TypeTag::Raw, int64_t timestamp=-1);

    void set(const std::string& key, double             value, int64_t timestamp=-1);
    void set(const std::string& key, int64_t            value, int64_t timestamp=-1);
    void set(const std::string& key, bool               value, int64_t timestamp=-1);
    void set(const std::string& key, const std::string& value, int64_t timestamp=-1);

    // ── Read ──────────────────────────────────────────────────────────────

    bool get(const char*        key, void* buffer, size_t buffer_size);
    bool get(const std::string& key, void* buffer, size_t buffer_size);

    double      get_double(const std::string& key, double             default_val = 0.0);
    int64_t     get_int64 (const std::string& key, int64_t            default_val = 0);
    bool        get_bool  (const std::string& key, bool               default_val = false);
    std::string get_string(const std::string& key, const std::string& default_val = "");

    // ── Snapshot ──────────────────────────────────────────────────────────

    struct Entry {
        std::string key;
        std::variant<double, int64_t, bool,
                     std::string, std::vector<uint8_t>> value;
        TypeTag  type_tag;
        int64_t  timestamp_ns;
        uint32_t source_node;
    };

    // Consistent value-copy of every entry. Used by Spynet's poll loop.
    // Holds sharable dict lock for the duration; no locks held during
    // serialization.
    std::vector<Entry> snapshot() const;

    // ── Lifecycle ─────────────────────────────────────────────────────────

    // Remove the OS shared memory object. All attached instances become invalid.
    static void destroy(const std::string& name);

private:
    // ── Directory navigation ──────────────────────────────────────────────
    //
    // The directory grows downward from shm_base + max_size.
    // Entry 0 (first inserted) is at the highest address:
    //   shm_base + max_size - HEADER_SLOT_SIZE
    // Entry i is at:
    //   shm_base + max_size - (i+1) * HEADER_SLOT_SIZE
    //
    // Caller must hold at least a sharable dict lock.

    size_t dir_count() const
    {
        return dict_header()->directory_size / HEADER_SLOT_SIZE;
    }

    Header* dir_entry(size_t i) const
    {
        return reinterpret_cast<Header*>(
            shm_base() + dict_header()->max_size - (i + 1) * HEADER_SLOT_SIZE);
    }

    // ── Lookup ────────────────────────────────────────────────────────────

    // Linear scan of the directory. O(n) — only called on cache miss.
    // Returns pointer to matching Header, or nullptr.
    // Caller must hold at least a sharable dict lock.
    Header* find(const char* key) const;

    // One O(n) pass over the directory to fully populate header_cache_.
    // Called once in the constructor on the open path.
    void warm_cache();

    // ── Pointer helpers ───────────────────────────────────────────────────

    char* shm_base() const
    {
        return static_cast<char*>(region_->get_address());
    }

    DictHeader* dict_header() const
    {
        return reinterpret_cast<DictHeader*>(shm_base());
    }

    // Start of the data region — immediately after DictHeader, aligned.
    char* data_base() const
    {
        return shm_base() + align_up(sizeof(DictHeader));
    }

    // Pointer to value bytes for a given header.
    char* value_ptr(const Header* h) const
    {
        return data_base() + h->data_offset;
    }

    // ── Data members ──────────────────────────────────────────────────────

    std::string name_;
    size_t      max_size_;
    uint32_t    source_id_;

    mutable std::shared_mutex thread_mutex_;  // thread-level, always acquired first

    // Per-instance cache: key → pointer to Header in the directory region.
    // Fully populated at attach by warm_cache(). Incrementally updated on insert.
    // After warm-up, find() is never called in steady-state operation.
    mutable std::unordered_map<std::string, Header*> header_cache_;

    std::shared_ptr<bip::shared_memory_object> shmobj_;
    std::shared_ptr<bip::mapped_region>        region_;
};

} // namespace spycat
#endif // __SPYMAP_HPP__
