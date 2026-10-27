#include "TextureCache.h"

#include "utils/DDSUtils.h"
#include "utils/FileUtils.h"
#include "utils/ImageUtils.h"
#include "utils/Log.h"

#include <algorithm>

namespace tpbr
{

namespace
{

/// Area-average downscale of an RGBA8 buffer. Used for raster sources, which have no mip chain, and
/// as a safety net when a DDS has only one mip level. Output box is at least 1x1.
std::vector<uint8_t> downscaleRGBA(const std::vector<uint8_t>& src, int srcW, int srcH, int dstW, int dstH)
{
    if (srcW == dstW && srcH == dstH)
        return src;

    std::vector<uint8_t> dst(static_cast<size_t>(dstW) * static_cast<size_t>(dstH) * 4);
    const double scaleX = static_cast<double>(srcW) / dstW;
    const double scaleY = static_cast<double>(srcH) / dstH;

    for (int y = 0; y < dstH; ++y)
    {
        const int y0 = static_cast<int>(y * scaleY);
        const int y1 = std::max(y0 + 1, std::min(srcH, static_cast<int>((y + 1) * scaleY)));
        for (int x = 0; x < dstW; ++x)
        {
            const int x0 = static_cast<int>(x * scaleX);
            const int x1 = std::max(x0 + 1, std::min(srcW, static_cast<int>((x + 1) * scaleX)));

            uint32_t acc[4] = {0, 0, 0, 0};
            uint32_t count = 0;
            for (int sy = y0; sy < y1; ++sy)
            {
                const uint8_t* row = src.data() + (static_cast<size_t>(sy) * srcW + x0) * 4;
                for (int sx = x0; sx < x1; ++sx, row += 4)
                {
                    acc[0] += row[0];
                    acc[1] += row[1];
                    acc[2] += row[2];
                    acc[3] += row[3];
                    ++count;
                }
            }

            uint8_t* out = dst.data() + (static_cast<size_t>(y) * dstW + x) * 4;
            if (count == 0)
            {
                out[0] = out[1] = out[2] = out[3] = 0;
                continue;
            }
            out[0] = static_cast<uint8_t>(acc[0] / count);
            out[1] = static_cast<uint8_t>(acc[1] / count);
            out[2] = static_cast<uint8_t>(acc[2] / count);
            out[3] = static_cast<uint8_t>(acc[3] / count);
        }
    }

    return dst;
}

/// Largest mip dimension that still fits within maxSize, clamped to at least 1.
int clampTargetSize(int width, int height, int maxSize)
{
    const int longest = std::max(width, height);
    if (maxSize <= 0 || longest <= maxSize)
        return longest;
    return maxSize;
}

} // namespace

TextureCache& TextureCache::instance()
{
    static TextureCache s_instance;
    return s_instance;
}

std::shared_ptr<const TextureCache::Entry> TextureCache::get(const std::filesystem::path& path)
{
    if (path.empty() || !std::filesystem::exists(path))
        return nullptr;

    std::error_code ec;
    const auto canonical = std::filesystem::canonical(path, ec);
    if (ec)
        return nullptr;

    const auto writeTime = std::filesystem::last_write_time(canonical, ec);
    if (ec)
        return nullptr;

    const Key key{canonical.string(), static_cast<long long>(writeTime.time_since_epoch().count()), 0};

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_entries.find(key);
        if (it != m_entries.end())
        {
            touchLocked_(key);
            return it->second.first;
        }
    }

    // Cache miss: decode without holding the lock, so other textures can load in parallel.
    auto entry = std::make_shared<Entry>();
    const auto ext = FileUtils::getExtensionLower(canonical);

    if (ext == ".dds")
    {
        bool isSRGB = false;
        if (!DDSUtils::loadDDS(canonical, entry->width, entry->height, entry->rgbaPixels, &isSRGB))
        {
            spdlog::warn("TextureCache: failed to load DDS {}", path.string());
            return nullptr;
        }
        entry->isSRGB = isSRGB;
    }
    else
    {
        auto imgData = ImageUtils::loadImage(canonical);
        if (imgData.pixels.empty())
        {
            spdlog::warn("TextureCache: failed to load image {}", path.string());
            return nullptr;
        }
        entry->width = imgData.width;
        entry->height = imgData.height;
        entry->rgbaPixels = std::move(imgData.pixels);
        entry->isSRGB = false;
    }

    spdlog::debug("TextureCache: loaded {} ({}x{}, {} bytes)", path.filename().string(), entry->width, entry->height,
                  entry->rgbaPixels.size());

    std::lock_guard<std::mutex> lock(m_mutex);
    // Another thread may have inserted the same key while we were decoding; keep the first one.
    auto it = m_entries.find(key);
    if (it != m_entries.end())
        return it->second.first;

    insertLocked_(key, entry);
    return entry;
}

std::shared_ptr<const TextureCache::Entry> TextureCache::getPreview(const std::filesystem::path& path, int maxSize)
{
    if (maxSize <= 0)
        return get(path);

    if (path.empty() || !std::filesystem::exists(path))
        return nullptr;

    std::error_code ec;
    const auto canonical = std::filesystem::canonical(path, ec);
    if (ec)
        return nullptr;

    const auto writeTime = std::filesystem::last_write_time(canonical, ec);
    if (ec)
        return nullptr;

    const Key key{canonical.string(), static_cast<long long>(writeTime.time_since_epoch().count()), maxSize};

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_entries.find(key);
        if (it != m_entries.end())
        {
            touchLocked_(key);
            return it->second.first;
        }
    }

    auto entry = std::make_shared<Entry>();
    const auto ext = FileUtils::getExtensionLower(canonical);

    if (ext == ".dds")
    {
        if (!DDSUtils::loadDDSAtMaxSize(canonical, maxSize, entry->width, entry->height, entry->rgbaPixels))
        {
            spdlog::warn("TextureCache: failed to load DDS preview {}", path.string());
            return nullptr;
        }
        DDSUtils::DDSInfo info;
        entry->isSRGB = DDSUtils::getDDSInfo(canonical, info) ? info.isSRGB : false;
    }
    else
    {
        auto imgData = ImageUtils::loadImage(canonical);
        if (imgData.pixels.empty())
        {
            spdlog::warn("TextureCache: failed to load image preview {}", path.string());
            return nullptr;
        }
        entry->width = imgData.width;
        entry->height = imgData.height;
        entry->isSRGB = false;

        const int target = clampTargetSize(imgData.width, imgData.height, maxSize);
        if (target < std::max(imgData.width, imgData.height))
        {
            const int dstW = std::max(1, imgData.width * target / std::max(imgData.width, imgData.height));
            const int dstH = std::max(1, imgData.height * target / std::max(imgData.width, imgData.height));
            entry->rgbaPixels = downscaleRGBA(imgData.pixels, imgData.width, imgData.height, dstW, dstH);
            entry->width = dstW;
            entry->height = dstH;
        }
        else
        {
            entry->rgbaPixels = std::move(imgData.pixels);
        }
    }

    // A DDS with no mip chain still comes back at full size; trim it so the preview really is small.
    if (maxSize > 0 && std::max(entry->width, entry->height) > maxSize && !entry->rgbaPixels.empty())
    {
        const int longest = std::max(entry->width, entry->height);
        const int dstW = std::max(1, entry->width * maxSize / longest);
        const int dstH = std::max(1, entry->height * maxSize / longest);
        entry->rgbaPixels = downscaleRGBA(entry->rgbaPixels, entry->width, entry->height, dstW, dstH);
        entry->width = dstW;
        entry->height = dstH;
    }

    spdlog::debug("TextureCache: loaded preview {} ({}x{}, max {}, {} bytes)", path.filename().string(),
                  entry->width, entry->height, maxSize, entry->rgbaPixels.size());

    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_entries.find(key);
    if (it != m_entries.end())
        return it->second.first;

    insertLocked_(key, entry);
    return entry;
}

void TextureCache::insertLocked_(const Key& key, EntryPtr entry)
{
    m_lru.push_front(key);
    m_entries.emplace(key, std::make_pair(std::move(entry), m_lru.begin()));
    m_bytes += m_entries[key].first->rgbaPixels.size();
    trimToBudgetLocked_();
}

void TextureCache::touchLocked_(const Key& key)
{
    auto it = m_entries.find(key);
    if (it == m_entries.end())
        return;
    m_lru.erase(it->second.second);
    m_lru.push_front(key);
    it->second.second = m_lru.begin();
}

void TextureCache::trimToBudgetLocked_()
{
    while (m_bytes > m_budgetBytes && m_entries.size() > 1)
    {
        const Key victim = m_lru.back();
        auto it = m_entries.find(victim);
        if (it == m_entries.end())
        {
            m_lru.pop_back();
            continue;
        }
        if (it->second.first)
            m_bytes -= it->second.first->rgbaPixels.size();
        m_entries.erase(it);
        m_lru.pop_back();
    }
}

void TextureCache::evict(const std::filesystem::path& path)
{
    std::error_code ec;
    const auto canonical = std::filesystem::canonical(path, ec);
    if (ec)
        return;

    const std::string canonicalStr = canonical.string();

    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto it = m_entries.begin(); it != m_entries.end();)
    {
        if (it->first.canonicalPath == canonicalStr)
        {
            if (it->second.first)
                m_bytes -= it->second.first->rgbaPixels.size();
            m_lru.erase(it->second.second);
            it = m_entries.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

void TextureCache::clear()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_entries.clear();
    m_lru.clear();
    m_bytes = 0;
}

size_t TextureCache::size() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_entries.size();
}

size_t TextureCache::memoryUsage() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_bytes;
}

void TextureCache::setBudgetBytes(size_t bytes)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_budgetBytes = bytes;
    trimToBudgetLocked_();
}

size_t TextureCache::budgetBytes() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_budgetBytes;
}

} // namespace tpbr
