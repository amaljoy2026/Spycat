#include "spymap.hpp"

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

// ── Constructor ───────────────────────────────────────────────────────────────

Spymap::Spymap(const std::string& name, uint32_t source_id, size_t max_size)
    : name_(name)
    , max_size_(max_size)
    , source_id_(source_id)
{
    bool created = false;

    try {
        // ── Create path ───────────────────────────────────────────────────
        shmobj_ = std::make_shared<bip::shared_memory_object>(
            bip::create_only, name_.c_str(), bip::read_write);

        // Pre-allocate full virtual range. Never grows — no pointer invalidation.
        shmobj_->truncate(static_cast<bip::offset_t>(max_size_));
        region_ = std::make_shared<bip::mapped_region>(*shmobj_, bip::read_write);

        // Placement-construct DictHeader at byte 0.
        new (region_->get_address()) DictHeader(max_size_);

        created = true;

    } catch (const bip::interprocess_exception&) {
        // ── Open path — segment already exists ───────────────────────────
        shmobj_ = std::make_shared<bip::shared_memory_object>(
            bip::open_only, name_.c_str(), bip::read_write);
        region_ = std::make_shared<bip::mapped_region>(*shmobj_, bip::read_write);

        // Read the true capacity from DictHeader — ignore caller's max_size.
        max_size_ = dict_header()->max_size;
    }

    if (!created) {
        // Populate header_cache_ in a single O(n) directory scan so that
        // steady-state set()/get() never call find().
        warm_cache();
    }
}

Spymap::~Spymap()
{
    region_.reset();
    shmobj_.reset();
}

/*static*/ void Spymap::destroy(const std::string& name)
{
    bip::shared_memory_object::remove(name.c_str());
}

// ── warm_cache ────────────────────────────────────────────────────────────────
//
// Single O(n) pass over the directory (high end of segment, growing downward).
// Builds header_cache_ completely. After this call, every set()/get() in this
// process hits the cache and never calls find().

void Spymap::warm_cache()
{
    std::unique_lock<std::shared_mutex> thread_lock(thread_mutex_);

    bip::sharable_lock<bip::interprocess_upgradable_mutex>
        dict_lock(dict_header()->dict_mutex);

    const size_t count = dir_count();
    header_cache_.reserve(count);

    for (size_t i = 0; i < count; ++i) {
        Header* h = dir_entry(i);
        header_cache_[h->key] = h;
    }
}

// ── find ──────────────────────────────────────────────────────────────────────
//
// Linear scan of the directory. O(n) in number of keys.
// Only called on a cache miss — never in steady state after warm_cache().
// Caller must hold at least a sharable dict lock.

Header* Spymap::find(const char* key) const
{
    const size_t count = dir_count();
    for (size_t i = 0; i < count; ++i) {
        Header* h = dir_entry(i);
        if (std::strncmp(h->key, key, sizeof(h->key)) == 0)
            return h;
    }
    return nullptr;
}

// ── set — core ────────────────────────────────────────────────────────────────

// timestamp: nanoseconds since Unix epoch (from sender's clock for network packets).
// Pass -1 (default) to stamp with the local clock at time of write.
void Spymap::set(const char* key, const void* value, size_t size, TypeTag type,
    int64_t timestamp)
{
    // ══ Fast path — key exists ════════════════════════════════════════════
    // Shared thread lock + sharable dict lock — structure is stable.
    // Per-entry value_mutex taken exclusively to prevent torn reads.
    {
        std::shared_lock<std::shared_mutex> thread_lock(thread_mutex_);

        bip::sharable_lock<bip::interprocess_upgradable_mutex>
            dict_lock(dict_header()->dict_mutex);

        // O(1) cache lookup — no shared memory scan
        Header* h = nullptr;
        auto it = header_cache_.find(key);
        if (it != header_cache_.end()) {
            h = it->second;
        } else {
            h = find(key);
            if (h) header_cache_[key] = h;
        }

        if (h) {
            if (size > h->data_size) {
                // Value grew beyond the capacity allocated on first insert.
                // Documented limitation — pre-size with a large enough initial value.
                std::cerr << "[spycat] set(\"" << key << "\"): value size " << size
                          << " exceeds allocated capacity " << h->data_size
                          << " — update skipped.\n";
                return;
            }

            {
                bip::scoped_lock<bip::interprocess_sharable_mutex>
                    val_lock(h->value_mutex);           // exclusive on value bytes

                std::memcpy(value_ptr(h), value, size);
                h->value_size   = size;
                h->type_tag     = type;
                h->timestamp_ns = (timestamp >= 0) ? timestamp : now_ns();
                h->source_node  = source_id_;
            }
            return;
        }
    }
    // Locks released — escalate to insert path

    // ══ Slow path — new key ═══════════════════════════════════════════════
    // Unique thread lock + exclusive dict lock: sole writer in the segment.
    {
        std::unique_lock<std::shared_mutex> thread_lock(thread_mutex_);

        // Upgradable excludes other inserters while existing readers continue.
        bip::upgradable_lock<bip::interprocess_upgradable_mutex>
            up_lock(dict_header()->dict_mutex);

        // TOCTOU re-check — another process may have inserted this key
        // between our fast-path miss and now.
        Header* h = find(key);
        if (h) {
            header_cache_[key] = h;
            up_lock.unlock();
            thread_lock.unlock();
            set(key, value, size, type, timestamp);    // tail recurse — at most once
            return;
        }

        // Promote upgradable → exclusive.
        bip::scoped_lock<bip::interprocess_upgradable_mutex>
            ex_lock(std::move(up_lock));

        DictHeader* dh = dict_header();

        // ── Alignment ─────────────────────────────────────────────────────
        // Round value size up so the next data allocation is also aligned.
        const size_t aligned_value_size = align_up(size);
        const size_t dict_header_size   = align_up(sizeof(DictHeader));

        // ── Overflow check ────────────────────────────────────────────────
        // Available capacity = max_size - sizeof(DictHeader) [already aligned]
        // Used capacity      = data_size + directory_size
        // New usage          = (data_size + aligned_value_size)
        //                    + (directory_size + HEADER_SLOT_SIZE)
        const size_t available = dh->max_size - dict_header_size;
        const size_t new_data  = dh->data_size + aligned_value_size;
        const size_t new_dir   = dh->directory_size + HEADER_SLOT_SIZE;

        if (new_data + new_dir > available) {
            std::cerr << "[spycat] set(\"" << key << "\"): segment full"
                      << " (data=" << new_data
                      << " dir="   << new_dir
                      << " avail=" << available
                      << ") — insert skipped.\n";
            return;
        }

        // ── 1. Allocate value bytes in the data region (grows upward) ─────
        //
        // data_base() = shm_base + align_up(sizeof(DictHeader))
        // New value goes at data_base + current data_size.
        // data_size is always a multiple of SPYCAT_ALIGN (maintained below).
        const bip::offset_t value_offset =
            static_cast<bip::offset_t>(dh->data_size);

        std::memcpy(data_base() + value_offset, value, size);

        // ── 2. Placement-construct Header in the directory (grows downward) ─
        //
        // New header slot is at:
        //   shm_base + max_size - (dir_count + 1) * HEADER_SLOT_SIZE
        //
        // dir_count() = directory_size / HEADER_SLOT_SIZE
        // After incrementing directory_size, the new entry is at dir_entry(new_count-1).
        //
        // We write the Header BEFORE committing directory_size so it is fully
        // initialised before becoming visible to other processes.
        const size_t new_dir_count = (new_dir) / HEADER_SLOT_SIZE;
        Header* new_h = new (
            shm_base() + dh->max_size - new_dir_count * HEADER_SLOT_SIZE
        ) Header(key, aligned_value_size, type);

        new_h->data_offset  = value_offset;
        new_h->value_size   = size;             // actual written bytes
        new_h->timestamp_ns = (timestamp >= 0) ? timestamp : now_ns();
        new_h->source_node  = source_id_;

        // ── 3. Commit both cursors atomically under exclusive lock ─────────
        //
        // After these writes the new entry is visible to all processes:
        //   - data_size  advance makes the value bytes "owned"
        //   - directory_size advance makes the Header visible via dir_count()
        dh->data_size      = new_data;   // advance data cursor
        dh->directory_size = new_dir;    // advance directory cursor — entry now live

        // ── 4. Update local cache ──────────────────────────────────────────
        header_cache_[key] = new_h;
    }
}

// ── get — core ────────────────────────────────────────────────────────────────

bool Spymap::get(const char* key, void* buffer, size_t buffer_size)
{
    std::shared_lock<std::shared_mutex> thread_lock(thread_mutex_);

    bip::sharable_lock<bip::interprocess_upgradable_mutex>
        dict_lock(dict_header()->dict_mutex);

    Header* h = nullptr;
    auto it = header_cache_.find(key);
    if (it != header_cache_.end()) {
        h = it->second;
    } else {
        h = find(key);
        if (h) header_cache_[key] = h;
    }

    if (!h) {
        std::memset(buffer, 0, buffer_size);
        return false;
    }

    {
        bip::sharable_lock<bip::interprocess_sharable_mutex>
            val_lock(h->value_mutex);                   // shared — concurrent reads ok

        const size_t copy_size = std::min(buffer_size, h->value_size);
        std::memcpy(buffer, value_ptr(h), copy_size);

        if (copy_size < buffer_size)
            std::memset(static_cast<char*>(buffer) + copy_size,
                        0, buffer_size - copy_size);
    }
    return true;
}

// ── snapshot ──────────────────────────────────────────────────────────────────
//
// Walks the directory (dense, contiguous, downward from shm end).
// Reads all metadata directly from each Header — only touches the data region
// to copy value bytes under the per-entry value lock.

std::vector<Spymap::Entry> Spymap::snapshot() const
{
    std::shared_lock<std::shared_mutex> thread_lock(thread_mutex_);

    bip::sharable_lock<bip::interprocess_upgradable_mutex>
        dict_lock(dict_header()->dict_mutex);

    const size_t count = dir_count();
    std::vector<Entry> result;
    result.reserve(count);

    for (size_t i = 0; i < count; ++i) {
        Header* h = dir_entry(i);

        Entry e;
        e.key          = h->key;
        e.type_tag     = h->type_tag;
        e.timestamp_ns = h->timestamp_ns;
        e.source_node  = h->source_node;

        {
            bip::sharable_lock<bip::interprocess_sharable_mutex>
                val_lock(h->value_mutex);               // shared, held only for copy

            const char* vptr = value_ptr(h);

            switch (h->type_tag) {
                case TypeTag::Double:
                    e.value.emplace<double>(*reinterpret_cast<const double*>(vptr));
                    break;
                case TypeTag::Int64:
                    e.value.emplace<int64_t>(*reinterpret_cast<const int64_t*>(vptr));
                    break;
                case TypeTag::Bool:
                    e.value.emplace<bool>(*reinterpret_cast<const bool*>(vptr));
                    break;
                case TypeTag::String:
                    e.value.emplace<std::string>(vptr, strnlen(vptr, h->value_size));
                    break;
                default: {
                    const auto* u = reinterpret_cast<const uint8_t*>(vptr);
                    e.value.emplace<std::vector<uint8_t>>(u, u + h->value_size);
                    break;
                }
            }
        }   // val_lock released — writer unblocked immediately

        result.push_back(std::move(e));
    }

    return result;  // dict_lock and thread_lock released here
}

// ── Typed set overloads ───────────────────────────────────────────────────────

void Spymap::set(const std::string& key, const void* value, size_t size,
    TypeTag type, int64_t timestamp)
{
    set(key.c_str(), value, size, type, timestamp);
}

void Spymap::set(const std::string& key, double value, int64_t timestamp)
{
    set(key.c_str(), &value, sizeof(double), TypeTag::Double, timestamp);
}

void Spymap::set(const std::string& key, int64_t value, int64_t timestamp)
{
    set(key.c_str(), &value, sizeof(int64_t), TypeTag::Int64, timestamp);
}

void Spymap::set(const std::string& key, bool value, int64_t timestamp)
{
    set(key.c_str(), &value, sizeof(bool), TypeTag::Bool, timestamp);
}

void Spymap::set(const std::string& key, const std::string& value, int64_t timestamp)
{
    set(key.c_str(), value.data(), value.size() + 1, TypeTag::String, timestamp);
}

// ── Typed get overloads ───────────────────────────────────────────────────────

bool Spymap::get(const std::string& key, void* buffer, size_t buffer_size)
{
    return get(key.c_str(), buffer, buffer_size);
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
    std::shared_lock<std::shared_mutex> thread_lock(thread_mutex_);

    bip::sharable_lock<bip::interprocess_upgradable_mutex>
        dict_lock(dict_header()->dict_mutex);

    Header* h = nullptr;
    auto it = header_cache_.find(key);
    if (it != header_cache_.end()) h = it->second;
    else                           h = find(key.c_str());

    if (!h) return default_val;

    bip::sharable_lock<bip::interprocess_sharable_mutex> val_lock(h->value_mutex);
    const char* vptr = value_ptr(h);
    return std::string(vptr, strnlen(vptr, h->value_size));
}