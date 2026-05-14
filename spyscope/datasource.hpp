// datasource.hpp
#ifndef __SPYSCOPE_DATASOURCE_HPP__
#define __SPYSCOPE_DATASOURCE_HPP__

#include <wx/wx.h>
#include <string>
#include <vector>
#include <optional>
#include <unordered_map>
#include "../spymap/spymap.hpp"

// Forward-declare SpyScope (global namespace) — full definition in app.hpp,
// included only in datasource.cpp to avoid a circular header dependency.
class SpyScope;

namespace spycat
{

class DataSource : public wxEvtHandler
{
public:
    explicit DataSource(const std::string& shmkey);

    // Rebuild cache from a fresh Spymap snapshot.
    // Also called internally by the data timer — safe to call externally too.
    void Poll();

    // Per-key lookup — O(1). Returns nullopt if key not in last snapshot.
    std::optional<Spymap::Entry> Get(const std::string& key) const;

    // Full cache — for Navigator and Watch to iterate.
    // Returns a const ref — valid until the next Poll().
    const std::unordered_map<std::string, Spymap::Entry>& GetAll() const;

    // Flat sorted key list — convenience for Navigator tree building.
    std::vector<std::string> GetKeys() const;

    // Write an override into shared memory.
    // priority=1  → asserts the value over normal producer writes (priority 0).
    // priority=-1 → clears the override; producer's next write wins again.
    // The string value is parsed to match the key's current type in cache.
    void SetOverride(const std::string& key,
                     const std::string& value,
                     int                priority);

    // True if the cache has been populated by at least one Poll().
    bool IsReady() const { return ready_; }

private:
    Spymap                                         map_;
    std::unordered_map<std::string, Spymap::Entry> cache_;
    bool                                           ready_ = false;
};

} // namespace spycat

#endif // __SPYSCOPE_DATASOURCE_HPP__
