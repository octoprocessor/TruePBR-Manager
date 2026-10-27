#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace tpbr
{

/// In-memory cache for decoded RGBA8 texture data.
///
/// Two tiers share one byte budget:
///   * full    -- `get()` decodes mip 0 at native size. Used by export and anything needing exact
///                pixels.
///   * preview -- `getPreview(path, maxSize)` decodes only the mip level needed to stay under
///                maxSize, so an 8K texture costs ~16 MB instead of ~268 MB. Used by the 2D preview
///                and the 3D viewport.
///
/// Entries are keyed by (canonical path, last write time, tier) so an edited file is picked up
/// automatically. Decoding happens outside the lock; the LRU list and entries are guarded by a
/// mutex. Pointers are shared_ptr so an eviction cannot dangle underneath a caller.
class TextureCache
{
  public:
    /// Get the singleton instance.
    static TextureCache& instance();

    /// Cached texture data (RGBA8).
    struct Entry
    {
        int width = 0;
        int height = 0;
        std::vector<uint8_t> rgbaPixels;
        bool isSRGB = false;
    };

    /// Load or retrieve cached full-resolution RGBA8 pixel data for a texture file.
    /// Supports DDS (via DDSUtils) and raster images (via stb_image).
    /// Returns nullptr if the file cannot be loaded.
    std::shared_ptr<const Entry> get(const std::filesystem::path& path);

    /// Load or retrieve a copy no larger than `maxSize` on its longest edge.
    /// For DDS this selects a mip level and never decompresses the rest of the chain; for raster
    /// images it decodes then area-averages down. `maxSize <= 0` is equivalent to get().
    std::shared_ptr<const Entry> getPreview(const std::filesystem::path& path, int maxSize);

    /// Evict a single path from the cache (e.g., after the file changes on disk).
    void evict(const std::filesystem::path& path);

    /// Clear all cached entries.
    void clear();

    /// Current number of cached entries.
    size_t size() const;

    /// Approximate total memory used by cached pixel data (in bytes).
    size_t memoryUsage() const;

    /// Upper bound on retained pixel bytes; least-recently-used entries are dropped to stay under
    /// it. Defaults to 1.5 GB, because a few 8K sets would otherwise exhaust RAM.
    void setBudgetBytes(size_t bytes);

    /// Current budget in bytes.
    size_t budgetBytes() const;

  private:
    TextureCache() = default;
    TextureCache(const TextureCache&) = delete;
    TextureCache& operator=(const TextureCache&) = delete;

    struct Key
    {
        std::string canonicalPath;
        long long writeTicks = 0;
        int tier = 0; ///< 0 = full resolution, otherwise the preview maxSize

        bool operator==(const Key& rhs) const
        {
            return tier == rhs.tier && writeTicks == rhs.writeTicks && canonicalPath == rhs.canonicalPath;
        }
    };

    struct KeyHash
    {
        size_t operator()(const Key& key) const noexcept
        {
            size_t h = std::hash<std::string>{}(key.canonicalPath);
            h ^= static_cast<size_t>(key.writeTicks) * 1099511628211ull;
            h ^= static_cast<size_t>(key.tier) * 1469598103934665603ull;
            return h;
        }
    };

    using EntryPtr = std::shared_ptr<Entry>;

    void insertLocked_(const Key& key, EntryPtr entry);
    void trimToBudgetLocked_();
    void touchLocked_(const Key& key);

    mutable std::mutex m_mutex;
    std::unordered_map<Key, std::pair<EntryPtr, std::list<Key>::iterator>, KeyHash> m_entries;
    std::list<Key> m_lru; ///< front = most recently used
    size_t m_bytes = 0;
    size_t m_budgetBytes = static_cast<size_t>(1536) * 1024 * 1024;
};

} // namespace tpbr
