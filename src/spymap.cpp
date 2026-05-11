#include "spymap.hpp"

#include <cassert>
#include <cstring>
#include <ctime>
#include <iostream>
#include <algorithm>

namespace bip = boost::interprocess;
using namespace spycat;

// ── Internal helpers ──────────────────────────────────────────────────────────

static int64_t now_ns()
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return static_cast<int64_t>(ts.tv_sec) * 1'000'000'000LL + ts.tv_nsec;
}

// ── Constructor / Destructor ──────────────────────────────────────────────────

Spymap::Spymap(const std::string& name, size_t max_size)
    : name_(name)
    , max_size_(max_size)
{
    // Try to create the segment. If it already exists, open it instead.
    // The segment is pre-truncated to max_size so we never need to grow it.
    // Virtual address space is cheap; physical pages are only allocated on write.
    try {
        shmobj_ = std::make_shared<bip::shared_memory_object>(
            bip::create_only, name_.c_str(), bip::read_write);
        shmobj_->truncate(static_cast<bip::offset_t>(max_size_));
        region_ = std::make_shared<bip::mapped_region>(*shmobj_, bip::read_write);

        // Placement-construct the DictHeader at byte 0.
        // Only the creating process does this — all others find it already there.
        new (region_->get_address()) DictHeader();

    } catch (const bip::interprocess_exception&) {
        // Segment already exists — just open and map it.
        shmobj_ = std::make_shared<bip::shared_memory_object>(
            bip::open_only, name_.c_str(), bip::read_write);
        region_ = std::make_shared<bip::mapped_region>(*shmobj_, bip::read_write);
        // DictHeader was constructed by the creator — already valid.
    }
}

Spymap::~Spymap()
{
    // Unmap the region and release the shmobj handle.
    // The segment itself persists in the OS until destroy() is called.
    region_.reset();
    shmobj_.reset();
}

/*static*/ void Spymap::destroy(const std::string& name)
{
    bip::shared_memory_object::remove(name.c_str());
}

// ── find() ────────────────────────────────────────────────────────────────────
// Linear scan through all entries starting after DictHeader.
// Caller must hold at least a sharable dict lock.

bip::offset_t Spymap::find(const char* key) const
{
    const DictHeader* dh     = reinterpret_cast<const DictHeader*>(shm_base());
    const size_t      used   = dh->used_bytes;
    bip::offset_t     offset = static_cast<bip::offset_t>(sizeof(DictHeader));

    while (static_cast<size_t>(offset) < used) {
        const Header* h = header_at(offset);
        if (std::strncmp(h->key, key, sizeof(h->key)) == 0)
            return offset;
        offset += static_cast<bip::offset_t>(sizeof(Header) + h->data_size);
    }
    return bip::offset_t(-1);
}

// ── set() — core implementation ───────────────────────────────────────────────

void Spymap::set(const char* key, const void* value, size_t size, TypeTag type)
{
    // ── Fast path: key likely exists ─────────────────────────────────────
    // Take shared thread lock + sharable dict lock.
    // If found, lock the per-entry value_mutex exclusively and overwrite.
    {
        std::shared_lock thread_lock(thread_mutex_);  // (1) thread: shared

        bip::sharable_lock<bip::interprocess_upgradable_mutex>
            dict_lock(dict_header()->dict_mutex);     // (2) process: sharable

        bip::offset_t offset = bip::offset_t(-1);

        auto it = offset_cache_.find(key);
        if (it != offset_cache_.end())
            offset = it->second;
        else
            offset = find(key);

        if (offset != bip::offset_t(-1)) {
            // Cache hit — update in place
            // NOTE: offset_cache_ is per-instance so we cast away const to update.
            // This is safe: the cache is a performance hint, not shared memory.
            const_cast<Spymap*>(this)->offset_cache_[key] = offset;

            Header* h = header_at(offset);

            if (size > h->data_size) {
                // Value grew beyond allocated capacity — cannot resize in place.
                // For the initial version this is a documented limitation:
                // allocate with enough capacity on first write, or use reserve().
                std::cerr << "[spycat] set(\"" << key << "\"): new size " << size
                          << " exceeds allocated capacity " << h->data_size
                          << " — skipping update\n";
                return;
            }

            {
                bip::scoped_lock<bip::interprocess_sharable_mutex>
                    val_lock(h->value_mutex);           // (3) per-entry: exclusive

                std::memcpy(shm_base() + offset + sizeof(Header), value, size);
                h->value_size   = size;
                h->type_tag     = type;
                h->timestamp_ns = now_ns();
                // source_node left as-is; will be set by higher-level API
            }
            return; // done
        }
    } // release both locks before escalating

    // ── Slow path: key is new — need exclusive dict lock ─────────────────
    {
        std::unique_lock thread_lock(thread_mutex_);  // (1) thread: exclusive

        // Take upgradable first, then promote to exclusive.
        // Upgradable excludes other appenders while readers continue.
        bip::upgradable_lock<bip::interprocess_upgradable_mutex>
            up_lock(dict_header()->dict_mutex);        // (2a) process: upgradable

        // Re-check under lock — another process may have inserted the key
        // between our fast-path miss and now (TOCTOU prevention).
        bip::offset_t offset = find(key);
        if (offset != bip::offset_t(-1)) {
            // Appeared while we were waiting — fall back to in-place update.
            // Promote would block readers; instead downgrade by releasing upgradable
            // and re-entering the fast path recursively.
            up_lock.unlock();
            thread_lock.unlock();
            set(key, value, size, type); // tail recurse — at most once
            return;
        }

        // Promote to exclusive — all readers drain, then we proceed alone.
        bip::scoped_lock<bip::interprocess_upgradable_mutex>
            ex_lock(std::move(up_lock));               // (2b) process: exclusive

        DictHeader* dh          = dict_header();
        size_t      needed      = sizeof(Header) + size;
        size_t      new_used    = dh->used_bytes + needed;

        if (new_used > max_size_) {
            std::cerr << "[spycat] set(\"" << key << "\"): shared memory full ("
                      << max_size_ << " bytes) — skipping\n";
            return;
        }

        bip::offset_t new_offset = static_cast<bip::offset_t>(dh->used_bytes);

        // Placement-construct the Header directly in shared memory.
        // Never memcpy a Header — interprocess_sharable_mutex is not trivially copyable.
        Header* h = new (shm_base() + new_offset) Header(key, size, type);
        h->timestamp_ns = now_ns();

        // Write the value bytes immediately after the header.
        std::memcpy(shm_base() + new_offset + sizeof(Header), value, size);
        h->value_size = size;

        // Commit the bump-allocator cursor — makes the entry visible to all peers.
        dh->used_bytes = new_used;

        // Update local cache.
        offset_cache_[key] = new_offset;
    }
}

// ── get() — core implementation ───────────────────────────────────────────────

bool Spymap::get(const char* key, void* buffer, size_t buffer_size)
{
    std::shared_lock thread_lock(thread_mutex_);          // (1) thread: shared

    bip::sharable_lock<bip::interprocess_upgradable_mutex>
        dict_lock(dict_header()->dict_mutex);             // (2) process: sharable

    bip::offset_t offset = bip::offset_t(-1);

    auto it = offset_cache_.find(key);
    if (it != offset_cache_.end())
        offset = it->second;
    else
        offset = find(key);

    if (offset == bip::offset_t(-1)) {
        std::memset(buffer, 0, buffer_size);
        return false;
    }

    offset_cache_[key] = offset; // update cache hint

    Header* h = header_at(offset);
    {
        bip::sharable_lock<bip::interprocess_sharable_mutex>
            val_lock(h->value_mutex);                     // (3) per-entry: shared

        size_t copy_size = std::min(buffer_size, h->value_size);
        std::memcpy(buffer, shm_base() + offset + sizeof(Header), copy_size);

        // Zero any remaining buffer bytes if buffer is larger than the value
        if (copy_size < buffer_size)
            std::memset(static_cast<char*>(buffer) + copy_size,
                        0, buffer_size - copy_size);
    }
    return true;
}

// ── String / typed overloads ──────────────────────────────────────────────────

void Spymap::set(const std::string& key, const void* value, size_t size, TypeTag type)
{
    set(key.c_str(), value, size, type);
}

bool Spymap::get(const std::string& key, void* buffer, size_t buffer_size)
{
    return get(key.c_str(), buffer, buffer_size);
}

void Spymap::set(const std::string& key, double value)
{
    set(key.c_str(), &value, sizeof(double), TypeTag::Double);
}

void Spymap::set(const std::string& key, int64_t value)
{
    set(key.c_str(), &value, sizeof(int64_t), TypeTag::Int64);
}

void Spymap::set(const std::string& key, bool value)
{
    set(key.c_str(), &value, sizeof(bool), TypeTag::Bool);
}

void Spymap::set(const std::string& key, const std::string& value)
{
    set(key.c_str(), value.data(), value.size() + 1, TypeTag::String); // +1 for null
}

double Spymap::get_double(const std::string& key, double default_val)
{
    double v = default_val;
    get(key.c_str(), &v, sizeof(double));
    return v;
}

int64_t Spymap::get_int64(const std::string& key, int64_t default_val)
{
    int64_t v = default_val;
    get(key.c_str(), &v, sizeof(int64_t));
    return v;
}

bool Spymap::get_bool(const std::string& key, bool default_val)
{
    bool v = default_val;
    get(key.c_str(), &v, sizeof(bool));
    return v;
}

std::string Spymap::get_string(const std::string& key, const std::string& default_val)
{
    // Peek at the header to find the stored size before allocating
    std::string result;
    {
        std::shared_lock thread_lock(thread_mutex_);
        bip::sharable_lock<bip::interprocess_upgradable_mutex>
            dict_lock(dict_header()->dict_mutex);

        bip::offset_t offset = bip::offset_t(-1);
        auto it = offset_cache_.find(key);
        if (it != offset_cache_.end()) offset = it->second;
        else                           offset = find(key.c_str());

        if (offset == bip::offset_t(-1))
            return default_val;

        Header* h = header_at(offset);
        bip::sharable_lock<bip::interprocess_sharable_mutex> val_lock(h->value_mutex);

        const char* data = shm_base() + offset + sizeof(Header);
        result.assign(data, strnlen(data, h->value_size));
    }
    return result;
}
