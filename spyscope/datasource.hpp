// datasource.hpp
#ifndef __SPYSCOPE_DATASOURCE_HPP__
#define __SPYSCOPE_DATASOURCE_HPP__

#include <string>
#include <vector>
#include <optional>
#include <unordered_map>
#include "../spymap/spymap.hpp"

namespace spycat
{

class DataSource
{
public:
    explicit DataSource(Spymap* spymap);

    // Call from MainFrame timer — rebuilds cache from a fresh snapshot()
    void Poll();

    // Per-key lookup — O(1). Returns nullopt if key not in last snapshot.
    std::optional<Spymap::Entry> Get(const std::string& key) const;

    // Full cache — for Navigator and Watch to iterate.
    // Returns a const ref — valid until the next Poll().
    const std::unordered_map<std::string, Spymap::Entry>& GetAll() const;

    // Flat sorted key list — convenience for Navigator tree building.
    std::vector<std::string> GetKeys() const;

    // True if the cache has been populated by at least one Poll().
    bool IsReady() const { return ready_; }

private:
    Spymap                                         *spymap_;
    std::unordered_map<std::string, Spymap::Entry> cache_;
    bool                                           ready_ = false;
};

} // namespace spycat

#endif // __SPYSCOPE_DATASOURCE_HPP__
