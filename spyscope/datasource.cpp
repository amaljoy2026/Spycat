// datasource.cpp
#include "datasource.hpp"
#include "app.hpp"      // full SpyScope definition — needed for GetSpymap()
#include <algorithm>
#include <stdexcept>

namespace spycat
{

DataSource::DataSource(const std::string& shmkey)
    : map_(shmkey)
    , timer_(this)
{
    Bind(wxEVT_TIMER, &DataSource::OnTimer, this);
    timer_.Start(8);
}

void DataSource::OnTimer(wxTimerEvent&)
{
    Poll();
}

void DataSource::Poll()
{    
    // Full snapshot from shared memory — one read, fans out to all consumers.
    std::vector<Spymap::Entry> entries = map_.snapshot();

    // Rebuild cache — last-write-wins on key collision (shouldn't occur in
    // a well-formed Spymap, but defensive to handle it).
    cache_.clear();
    cache_.reserve(entries.size());

    for (auto& entry : entries)
        cache_.emplace(entry.key, std::move(entry));

    ready_ = true;
}

std::optional<Spymap::Entry> DataSource::Get(const std::string& key) const
{
    auto it = cache_.find(key);
    if (it == cache_.end())
        return std::nullopt;
    return it->second;
}

const std::unordered_map<std::string, Spymap::Entry>& DataSource::GetAll() const
{
    return cache_;
}

std::vector<std::string> DataSource::GetKeys() const
{
    std::vector<std::string> keys;
    keys.reserve(cache_.size());

    for (const auto& [key, _] : cache_)
        keys.push_back(key);

    std::sort(keys.begin(), keys.end());
    return keys;
}

void DataSource::SetOverride(const std::string& key,
                             const std::string& value,
                             int                priority)
{
    // Determine type from cache; default to double if key unknown yet.
    TypeTag tag = TypeTag::Double;
    auto it = cache_.find(key);
    if (it != cache_.end())
        tag = it->second.type_tag;

    try {
        switch (tag) {
            case TypeTag::Double:
                map_.set(key, std::stod(value),             priority);  break;
            case TypeTag::Float:
                map_.set(key, (float)std::stod(value),      priority);  break;
            case TypeTag::Int64:
                map_.set(key, (int64_t)std::stoll(value),   priority);  break;
            case TypeTag::Int32:
                map_.set(key, (int32_t)std::stol(value),    priority);  break;
            case TypeTag::Bool:
                map_.set(key, (value == "1" || value == "true"), priority); break;
            case TypeTag::String:
                map_.set(key, value,                         priority);  break;
            default:
                // Raw / unknown — treat as double
                map_.set(key, std::stod(value),              priority);  break;
        }
    } catch (const std::invalid_argument&) {
        // Unparseable string — silently ignore; user is still typing
    } catch (const std::out_of_range&) {
        // Value out of range for the type — silently ignore
    }
}

} // namespace spycat
