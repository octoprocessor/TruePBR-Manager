#include "DDSUtils.h"

#include <DirectXTex.h>
#include "utils/Log.h"

#include <algorithm>
#include <cstring>
#include <fstream>

namespace tpbr
{

// ─── Helpers ───────────────────────────────────────────────

/// Convert std::filesystem::path to wide string for DirectXTex APIs
static std::wstring toWide(const std::filesystem::path& p)
{
    return p.wstring();
}

/// Get a human-readable name for common DXGI formats
static std::string dxgiFormatName(DXGI_FORMAT fmt)
{
    switch (fmt)
    {
    case DXGI_FORMAT_R8G8B8A8_UNORM:
        return "R8G8B8A8_UNORM";
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
        return "R8G8B8A8_UNORM_SRGB";
    case DXGI_FORMAT_B8G8R8A8_UNORM:
        return "B8G8R8A8_UNORM";
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
        return "B8G8R8A8_UNORM_SRGB";
    case DXGI_FORMAT_BC1_UNORM:
        return "BC1_UNORM";
    case DXGI_FORMAT_BC1_UNORM_SRGB:
        return "BC1_UNORM_SRGB";
    case DXGI_FORMAT_BC3_UNORM:
        return "BC3_UNORM";
    case DXGI_FORMAT_BC3_UNORM_SRGB:
        return "BC3_UNORM_SRGB";
    case DXGI_FORMAT_BC4_UNORM:
        return "BC4_UNORM";
    case DXGI_FORMAT_BC5_UNORM:
        return "BC5_UNORM";
    case DXGI_FORMAT_BC6H_UF16:
        return "BC6H_UF16";
    case DXGI_FORMAT_BC7_UNORM:
        return "BC7_UNORM";
    case DXGI_FORMAT_BC7_UNORM_SRGB:
        return "BC7_UNORM_SRGB";
    case DXGI_FORMAT_R8_UNORM:
        return "R8_UNORM";
    default:
        return "DXGI_FORMAT(" + std::to_string(static_cast<int>(fmt)) + ")";
    }
}

/// Approximate channel count from DXGI format
static int dxgiFormatChannels(DXGI_FORMAT fmt)
{
    switch (fmt)
    {
    case DXGI_FORMAT_R8_UNORM:
    case DXGI_FORMAT_BC4_UNORM:
    case DXGI_FORMAT_BC4_SNORM:
        return 1;
    case DXGI_FORMAT_R8G8_UNORM:
    case DXGI_FORMAT_BC5_UNORM:
    case DXGI_FORMAT_BC5_SNORM:
        return 2;
    case DXGI_FORMAT_BC6H_UF16:
    case DXGI_FORMAT_BC6H_SF16:
    default:
        return 4;
    }
}

static DXGI_FORMAT preferredRGBAFormat(DXGI_FORMAT sourceFormat)
{
    return DirectX::IsSRGB(sourceFormat) ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;
}

/// Create a ScratchImage from raw RGBA8 pixels (no copy — caller must keep pixels alive)
/// Returns a newly initialized ScratchImage with pixel data copied in.
static HRESULT createScratchFromRGBA(int width, int height, const uint8_t* rgbaPixels, DirectX::ScratchImage& scratch,
                                     bool srgb = false)
{
    HRESULT hr = scratch.Initialize2D(srgb ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM,
                                      static_cast<size_t>(width), static_cast<size_t>(height), 1, 1);
    if (FAILED(hr))
        return hr;

    const DirectX::Image* img = scratch.GetImage(0, 0, 0);
    if (!img)
        return E_FAIL;

    const size_t srcRowPitch = static_cast<size_t>(width) * 4;
    for (int y = 0; y < height; ++y)
    {
        const uint8_t* srcRow = rgbaPixels + y * srcRowPitch;
        uint8_t* dstRow = img->pixels + y * img->rowPitch;
        std::memcpy(dstRow, srcRow, srcRowPitch);
    }

    return S_OK;
}

static DirectX::TEX_ALPHA_MODE detectAlphaMode(const uint8_t* rgbaPixels, int width, int height)
{
    const size_t pixelCount = static_cast<size_t>(width) * height;
    for (size_t i = 0; i < pixelCount; ++i)
    {
        if (rgbaPixels[i * 4 + 3] != 255)
        {
            return DirectX::TEX_ALPHA_MODE_STRAIGHT;
        }
    }

    return DirectX::TEX_ALPHA_MODE_OPAQUE;
}

static bool generateMipChain(const DirectX::ScratchImage& source, DirectX::ScratchImage& mipChain)
{
    HRESULT hr = DirectX::GenerateMipMaps(source.GetImages(), source.GetImageCount(), source.GetMetadata(),
                                          DirectX::TEX_FILTER_DEFAULT, 0, mipChain);

    if (FAILED(hr))
    {
        spdlog::error("DDS mipmap generation failed: 0x{:08X}", static_cast<unsigned>(hr));
        return false;
    }

    return true;
}

/// Compress a ScratchImage and save to DDS file.
static bool compressAndSave(const DirectX::ScratchImage& source, DXGI_FORMAT targetFormat,
                            const std::filesystem::path& path,
                            DirectX::TEX_ALPHA_MODE alphaMode = DirectX::TEX_ALPHA_MODE_UNKNOWN)
{
    // Ensure parent directory exists
    std::filesystem::create_directories(path.parent_path());

    DirectX::ScratchImage mipChain;
    if (!generateMipChain(source, mipChain))
    {
        return false;
    }

    const DirectX::ScratchImage& sourceWithMips = mipChain;

    DirectX::TEX_COMPRESS_FLAGS compressFlags = DirectX::TEX_COMPRESS_PARALLEL;
    if (targetFormat == DXGI_FORMAT_BC7_UNORM || targetFormat == DXGI_FORMAT_BC7_UNORM_SRGB)
    {
        compressFlags = static_cast<DirectX::TEX_COMPRESS_FLAGS>(compressFlags | DirectX::TEX_COMPRESS_BC7_QUICK);
    }

    DirectX::ScratchImage compressed;
    HRESULT hr =
        DirectX::Compress(sourceWithMips.GetImages(), sourceWithMips.GetImageCount(), sourceWithMips.GetMetadata(),
                          targetFormat, compressFlags, DirectX::TEX_THRESHOLD_DEFAULT, compressed);

    if (FAILED(hr))
    {
        spdlog::error("DDS compress failed: 0x{:08X} -> {}", static_cast<unsigned>(hr), path.string());
        return false;
    }

    auto metadata = compressed.GetMetadata();
    metadata.SetAlphaMode(alphaMode);

    hr = DirectX::SaveToDDSFile(compressed.GetImages(), compressed.GetImageCount(), metadata, DirectX::DDS_FLAGS_NONE,
                                toWide(path).c_str());

    if (FAILED(hr))
    {
        spdlog::error("DDS save failed: 0x{:08X} -> {}", static_cast<unsigned>(hr), path.string());
        return false;
    }

    return true;
}

// ─── getDDSInfo ────────────────────────────────────────────

bool DDSUtils::getDDSInfo(const std::filesystem::path& path, DDSInfo& info)
{
    DirectX::TexMetadata metadata{};

    // Read only the DDS header — no pixel data is loaded or decompressed.
    HRESULT hr = DirectX::GetMetadataFromDDSFile(toWide(path).c_str(), DirectX::DDS_FLAGS_NONE, metadata);

    if (FAILED(hr))
    {
        spdlog::error("getDDSInfo: failed to read header {} (0x{:08X})", path.string(), static_cast<unsigned>(hr));
        return false;
    }

    info.width = static_cast<int>(metadata.width);
    info.height = static_cast<int>(metadata.height);
    info.mipLevels = metadata.mipLevels;
    info.dxgiFormat = static_cast<uint32_t>(metadata.format);
    info.hasAlpha = DirectX::HasAlpha(metadata.format);
    info.isSRGB = DirectX::IsSRGB(metadata.format);
    info.formatName = dxgiFormatName(metadata.format);
    info.channels = dxgiFormatChannels(metadata.format);

    return true;
}

// ─── loadDDS ───────────────────────────────────────────────

bool DDSUtils::loadDDS(const std::filesystem::path& path, int& width, int& height, std::vector<uint8_t>& rgbaPixels,
                       bool* isSRGB)
{
    DirectX::TexMetadata metadata{};
    DirectX::ScratchImage scratch;

    HRESULT hr = DirectX::LoadFromDDSFile(toWide(path).c_str(), DirectX::DDS_FLAGS_NONE, &metadata, scratch);

    if (FAILED(hr))
    {
        spdlog::error("loadDDS: failed to load {} (0x{:08X})", path.string(), static_cast<unsigned>(hr));
        return false;
    }

    const DXGI_FORMAT targetRGBAFormat = preferredRGBAFormat(metadata.format);
    if (isSRGB)
    {
        *isSRGB = DirectX::IsSRGB(metadata.format);
    }

    // If compressed, decompress first
    DirectX::ScratchImage decompressed;
    const DirectX::ScratchImage* source = &scratch;

    if (DirectX::IsCompressed(metadata.format))
    {
        hr = DirectX::Decompress(scratch.GetImages(), scratch.GetImageCount(), scratch.GetMetadata(),
                                 DXGI_FORMAT_UNKNOWN, decompressed);

        if (FAILED(hr))
        {
            spdlog::error("loadDDS: decompress failed for {} (0x{:08X})", path.string(), static_cast<unsigned>(hr));
            return false;
        }
        source = &decompressed;
    }

    // Convert to R8G8B8A8_UNORM if not already
    DirectX::ScratchImage converted;
    const auto& srcMeta = source->GetMetadata();

    if (srcMeta.format != targetRGBAFormat)
    {
        hr = DirectX::Convert(source->GetImages(), source->GetImageCount(), srcMeta, targetRGBAFormat,
                              DirectX::TEX_FILTER_DEFAULT, DirectX::TEX_THRESHOLD_DEFAULT, converted);

        if (FAILED(hr))
        {
            spdlog::error("loadDDS: convert failed for {} (0x{:08X})", path.string(), static_cast<unsigned>(hr));
            return false;
        }
        source = &converted;
    }

    // Extract mip 0
    const DirectX::Image* img = source->GetImage(0, 0, 0);
    if (!img || !img->pixels)
    {
        spdlog::error("loadDDS: no image data in {}", path.string());
        return false;
    }

    width = static_cast<int>(img->width);
    height = static_cast<int>(img->height);

    const size_t dstRowPitch = static_cast<size_t>(width) * 4;
    rgbaPixels.resize(dstRowPitch * height);

    for (int y = 0; y < height; ++y)
    {
        const uint8_t* srcRow = img->pixels + y * img->rowPitch;
        uint8_t* dstRow = rgbaPixels.data() + y * dstRowPitch;
        std::memcpy(dstRow, srcRow, dstRowPitch);
    }

    spdlog::debug("loadDDS: {} ({}x{}, {})", path.string(), width, height, dxgiFormatName(metadata.format));
    return true;
}

// ─── loadDDSAtMaxSize ──────────────────────────────────────

namespace
{

/// Size in bytes of the DDS container header (magic + DDS_HEADER [+ DDS_HEADER_DXT10]).
bool ddsHeaderSize(std::ifstream& in, size_t& headerSize)
{
    char magic[4] = {};
    in.seekg(0, std::ios::beg);
    if (!in.read(magic, 4))
        return false;
    if (std::memcmp(magic, "DDS ", 4) != 0)
        return false;

    // dwFourCC lives at file offset 84 (4 magic + 80 into DDS_HEADER).
    char fourCC[4] = {};
    in.seekg(84, std::ios::beg);
    if (!in.read(fourCC, 4))
        return false;

    headerSize = (std::memcmp(fourCC, "DX10", 4) == 0) ? 148 : 128;
    return true;
}

/// Read only the bytes of one mip level out of a DDS file.
/// Returns false when the container is not the simple 2D single-surface case, in which case the
/// caller must fall back to a full load.
bool extractMipBytes(std::ifstream& in, size_t headerSize, const DirectX::TexMetadata& md, size_t targetMip,
                     std::vector<uint8_t>& out, size_t& rowPitch, size_t& slicePitch)
{
    if (md.arraySize != 1 || md.depth != 1 || (md.miscFlags & DirectX::TEX_MISC_TEXTURECUBE) != 0)
        return false;
    if (targetMip >= md.mipLevels)
        return false;

    size_t offset = headerSize;
    for (size_t mip = 0; mip < targetMip; ++mip)
    {
        const size_t mipW = std::max<size_t>(1, md.width >> mip);
        const size_t mipH = std::max<size_t>(1, md.height >> mip);
        size_t rowBytes = 0;
        size_t sliceBytes = 0;
        if (FAILED(DirectX::ComputePitch(md.format, mipW, mipH, rowBytes, sliceBytes)))
            return false;
        offset += sliceBytes;
    }

    // Guard against files whose rows are padded differently from the canonical pitch: only trust the
    // computed offsets when the header plus every mip accounts for the file exactly.
    {
        size_t expected = headerSize;
        for (size_t mip = 0; mip < md.mipLevels; ++mip)
        {
            const size_t mipW = std::max<size_t>(1, md.width >> mip);
            const size_t mipH = std::max<size_t>(1, md.height >> mip);
            size_t rowBytes = 0;
            size_t sliceBytes = 0;
            if (FAILED(DirectX::ComputePitch(md.format, mipW, mipH, rowBytes, sliceBytes)))
                return false;
            expected += sliceBytes;
        }
        in.seekg(0, std::ios::end);
        const size_t actual = static_cast<size_t>(in.tellg());
        in.clear();
        if (expected != actual)
            return false;
    }

    const size_t mipW = std::max<size_t>(1, md.width >> targetMip);
    const size_t mipH = std::max<size_t>(1, md.height >> targetMip);
    if (FAILED(DirectX::ComputePitch(md.format, mipW, mipH, rowPitch, slicePitch)))
        return false;

    in.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    if (!in)
        return false;

    out.resize(slicePitch);
    if (!in.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(slicePitch)))
        return false;

    return true;
}

/// Decompress one already-extracted mip and convert it to RGBA8 rows.
bool decodeSingleMip(const DirectX::TexMetadata& md, size_t mip, std::vector<uint8_t>& mipBytes, size_t rowPitch,
                     size_t slicePitch, int& width, int& height, std::vector<uint8_t>& rgbaPixels)
{
    const size_t mipW = std::max<size_t>(1, md.width >> mip);
    const size_t mipH = std::max<size_t>(1, md.height >> mip);

    DirectX::Image image{};
    image.width = mipW;
    image.height = mipH;
    image.format = md.format;
    image.rowPitch = rowPitch;
    image.slicePitch = slicePitch;
    image.pixels = mipBytes.data();

    DirectX::TexMetadata oneMip = md;
    oneMip.width = mipW;
    oneMip.height = mipH;
    oneMip.mipLevels = 1;
    oneMip.arraySize = 1;
    oneMip.depth = 1;
    oneMip.miscFlags &= ~DirectX::TEX_MISC_TEXTURECUBE;

    // Decompress if needed (BC formats), leaving the result in `current`
    DirectX::ScratchImage decompressed;
    const DirectX::Image* current = &image;

    if (DirectX::IsCompressed(image.format))
    {
        DirectX::ScratchImage scratchForDecompress;
        // Wrap the single mip in a ScratchImage so Decompress can consume it uniformly.
        HRESULT hr = scratchForDecompress.InitializeFromImage(image);
        if (FAILED(hr))
            return false;
        hr = DirectX::Decompress(scratchForDecompress.GetImages(), scratchForDecompress.GetImageCount(),
                                 scratchForDecompress.GetMetadata(), DXGI_FORMAT_UNKNOWN, decompressed);
        if (FAILED(hr))
            return false;
        current = decompressed.GetImage(0, 0, 0);
    }

    if (!current || !current->pixels)
        return false;

    const DXGI_FORMAT targetRGBAFormat = preferredRGBAFormat(current->format);

    DirectX::ScratchImage converted;
    if (current->format != targetRGBAFormat)
    {
        DirectX::ScratchImage source;
        HRESULT hr = source.InitializeFromImage(*current);
        if (FAILED(hr))
            return false;
        hr = DirectX::Convert(source.GetImages(), source.GetImageCount(), source.GetMetadata(), targetRGBAFormat,
                              DirectX::TEX_FILTER_DEFAULT, DirectX::TEX_THRESHOLD_DEFAULT, converted);
        if (FAILED(hr))
            return false;
        current = converted.GetImage(0, 0, 0);
        if (!current || !current->pixels)
            return false;
    }

    width = static_cast<int>(current->width);
    height = static_cast<int>(current->height);
    const size_t dstRowPitch = static_cast<size_t>(width) * 4;
    rgbaPixels.resize(dstRowPitch * height);
    for (int y = 0; y < height; ++y)
    {
        std::memcpy(rgbaPixels.data() + static_cast<size_t>(y) * dstRowPitch,
                    current->pixels + static_cast<size_t>(y) * current->rowPitch, dstRowPitch);
    }

    return true;
}

} // namespace

bool DDSUtils::loadDDSAtMaxSize(const std::filesystem::path& path, int maxSize, int& width, int& height,
                                std::vector<uint8_t>& rgbaPixels)
{
    DirectX::TexMetadata metadata{};

    // Header only: no pixel data is read or decompressed yet.
    HRESULT hr = DirectX::GetMetadataFromDDSFile(toWide(path).c_str(), DirectX::DDS_FLAGS_NONE, metadata);
    if (FAILED(hr))
    {
        spdlog::error("loadDDSAtMaxSize: failed to read header {} (0x{:08X})", path.string(),
                      static_cast<unsigned>(hr));
        return false;
    }

    // Pick the smallest mip that is still >= maxSize (mip 0 if it already fits).
    size_t selectedMip = 0;
    if (maxSize > 0 && metadata.mipLevels > 1)
    {
        for (size_t mip = 0; mip < metadata.mipLevels; ++mip)
        {
            const size_t mipW = std::max<size_t>(1, metadata.width >> mip);
            const size_t mipH = std::max<size_t>(1, metadata.height >> mip);
            selectedMip = mip;
            if (std::max(mipW, mipH) <= static_cast<size_t>(maxSize))
                break;
        }
    }

    // Fast path: read and decompress only the selected mip. On a mip-chained 8K BC7 this touches
    // 1/16th of the file instead of all of it, which is the difference between a snappy preview and
    // a multi-second stall.
    if (selectedMip > 0)
    {
        std::ifstream in(path, std::ios::binary);
        size_t headerSize = 0;
        if (in && ddsHeaderSize(in, headerSize))
        {
            std::vector<uint8_t> mipBytes;
            size_t rowPitch = 0;
            size_t slicePitch = 0;
            if (extractMipBytes(in, headerSize, metadata, selectedMip, mipBytes, rowPitch, slicePitch) &&
                decodeSingleMip(metadata, selectedMip, mipBytes, rowPitch, slicePitch, width, height, rgbaPixels))
            {
                spdlog::debug("loadDDSAtMaxSize: {} mip {} ({}x{}, requested max {}) [single-mip]",
                              path.string(), selectedMip, width, height, maxSize);
                return true;
            }
        }
        // Fall through to the full-load path below if anything about the container was unexpected.
    }

    DirectX::ScratchImage scratch;

    hr = DirectX::LoadFromDDSFile(toWide(path).c_str(), DirectX::DDS_FLAGS_NONE, &metadata, scratch);

    if (FAILED(hr))
    {
        spdlog::error("loadDDSAtMaxSize: failed to load {} (0x{:08X})", path.string(), static_cast<unsigned>(hr));
        return false;
    }

    const DXGI_FORMAT targetRGBAFormat = preferredRGBAFormat(metadata.format);

    // Decompress if needed
    DirectX::ScratchImage decompressed;
    const DirectX::ScratchImage* source = &scratch;

    if (DirectX::IsCompressed(metadata.format))
    {
        hr = DirectX::Decompress(scratch.GetImages(), scratch.GetImageCount(), scratch.GetMetadata(),
                                 DXGI_FORMAT_UNKNOWN, decompressed);
        if (FAILED(hr))
        {
            spdlog::error("loadDDSAtMaxSize: decompress failed for {} (0x{:08X})", path.string(),
                          static_cast<unsigned>(hr));
            return false;
        }
        source = &decompressed;
    }

    // Convert to RGBA if needed
    DirectX::ScratchImage converted;
    const auto& srcMeta = source->GetMetadata();

    if (srcMeta.format != targetRGBAFormat)
    {
        hr = DirectX::Convert(source->GetImages(), source->GetImageCount(), srcMeta, targetRGBAFormat,
                              DirectX::TEX_FILTER_DEFAULT, DirectX::TEX_THRESHOLD_DEFAULT, converted);
        if (FAILED(hr))
        {
            spdlog::error("loadDDSAtMaxSize: convert failed for {} (0x{:08X})", path.string(),
                          static_cast<unsigned>(hr));
            return false;
        }
        source = &converted;
    }

    // Extract selected mip level
    const DirectX::Image* img = source->GetImage(selectedMip, 0, 0);
    if (!img || !img->pixels)
    {
        // Fallback to mip 0 if selected mip doesn't exist
        img = source->GetImage(0, 0, 0);
        if (!img || !img->pixels)
        {
            spdlog::error("loadDDSAtMaxSize: no image data in {}", path.string());
            return false;
        }
    }

    width = static_cast<int>(img->width);
    height = static_cast<int>(img->height);

    const size_t dstRowPitch = static_cast<size_t>(width) * 4;
    rgbaPixels.resize(dstRowPitch * height);

    for (int y = 0; y < height; ++y)
    {
        const uint8_t* srcRow = img->pixels + y * img->rowPitch;
        uint8_t* dstRow = rgbaPixels.data() + y * dstRowPitch;
        std::memcpy(dstRow, srcRow, dstRowPitch);
    }

    spdlog::debug("loadDDSAtMaxSize: {} mip {} ({}x{}, requested max {})", path.string(), selectedMip, width, height,
                  maxSize);
    return true;
}

// ─── saveDDS_BC7 ───────────────────────────────────────────

bool DDSUtils::saveDDS_BC7(const std::filesystem::path& path, int width, int height, const uint8_t* rgbaPixels,
                           bool srgb)
{
    DirectX::ScratchImage scratch;
    HRESULT hr = createScratchFromRGBA(width, height, rgbaPixels, scratch, srgb);
    if (FAILED(hr))
    {
        spdlog::error("saveDDS_BC7: init failed (0x{:08X})", static_cast<unsigned>(hr));
        return false;
    }

    bool ok = compressAndSave(scratch, srgb ? DXGI_FORMAT_BC7_UNORM_SRGB : DXGI_FORMAT_BC7_UNORM, path,
                              detectAlphaMode(rgbaPixels, width, height));
    if (ok)
        spdlog::debug("saveDDS_BC7: {} ({}x{})", path.string(), width, height);
    return ok;
}

// ─── saveDDS_BC5 ───────────────────────────────────────────

bool DDSUtils::saveDDS_BC5(const std::filesystem::path& path, int width, int height, const uint8_t* rgbaPixels)
{
    DirectX::ScratchImage scratch;
    HRESULT hr = createScratchFromRGBA(width, height, rgbaPixels, scratch, false);
    if (FAILED(hr))
    {
        spdlog::error("saveDDS_BC5: init failed (0x{:08X})", static_cast<unsigned>(hr));
        return false;
    }

    bool ok = compressAndSave(scratch, DXGI_FORMAT_BC5_UNORM, path);
    if (ok)
        spdlog::debug("saveDDS_BC5: {} ({}x{})", path.string(), width, height);
    return ok;
}

// ─── saveDDS_BC3 ───────────────────────────────────────────

bool DDSUtils::saveDDS_BC3(const std::filesystem::path& path, int width, int height, const uint8_t* rgbaPixels,
                           bool srgb)
{
    DirectX::ScratchImage scratch;
    HRESULT hr = createScratchFromRGBA(width, height, rgbaPixels, scratch, srgb);
    if (FAILED(hr))
    {
        spdlog::error("saveDDS_BC3: init failed (0x{:08X})", static_cast<unsigned>(hr));
        return false;
    }

    bool ok = compressAndSave(scratch, srgb ? DXGI_FORMAT_BC3_UNORM_SRGB : DXGI_FORMAT_BC3_UNORM, path,
                              detectAlphaMode(rgbaPixels, width, height));
    if (ok)
        spdlog::debug("saveDDS_BC3: {} ({}x{})", path.string(), width, height);
    return ok;
}

// ─── saveDDS_BC6H ──────────────────────────────────────────

bool DDSUtils::saveDDS_BC6H(const std::filesystem::path& path, int width, int height, const uint8_t* rgbaPixels)
{
    DirectX::ScratchImage rgbaScratch;
    HRESULT hr = createScratchFromRGBA(width, height, rgbaPixels, rgbaScratch, false);
    if (FAILED(hr))
    {
        spdlog::error("saveDDS_BC6H: init failed (0x{:08X})", static_cast<unsigned>(hr));
        return false;
    }

    DirectX::ScratchImage hdrScratch;
    hr = DirectX::Convert(rgbaScratch.GetImages(), rgbaScratch.GetImageCount(), rgbaScratch.GetMetadata(),
                          DXGI_FORMAT_R16G16B16A16_FLOAT, DirectX::TEX_FILTER_DEFAULT, DirectX::TEX_THRESHOLD_DEFAULT,
                          hdrScratch);

    if (FAILED(hr))
    {
        spdlog::error("saveDDS_BC6H: convert failed (0x{:08X})", static_cast<unsigned>(hr));
        return false;
    }

    bool ok = compressAndSave(hdrScratch, DXGI_FORMAT_BC6H_UF16, path);
    if (ok)
        spdlog::debug("saveDDS_BC6H: {} ({}x{})", path.string(), width, height);
    return ok;
}

// ─── saveDDS_BC4 ───────────────────────────────────────────

bool DDSUtils::saveDDS_BC4(const std::filesystem::path& path, int width, int height, const uint8_t* singleChannelPixels)
{
    // Create R8_UNORM scratch image
    DirectX::ScratchImage scratch;
    HRESULT hr =
        scratch.Initialize2D(DXGI_FORMAT_R8_UNORM, static_cast<size_t>(width), static_cast<size_t>(height), 1, 1);
    if (FAILED(hr))
    {
        spdlog::error("saveDDS_BC4: init failed (0x{:08X})", static_cast<unsigned>(hr));
        return false;
    }

    const DirectX::Image* img = scratch.GetImage(0, 0, 0);
    if (!img)
        return false;

    const size_t srcRowPitch = static_cast<size_t>(width);
    for (int y = 0; y < height; ++y)
    {
        const uint8_t* srcRow = singleChannelPixels + y * srcRowPitch;
        uint8_t* dstRow = img->pixels + y * img->rowPitch;
        std::memcpy(dstRow, srcRow, srcRowPitch);
    }

    bool ok = compressAndSave(scratch, DXGI_FORMAT_BC4_UNORM, path);
    if (ok)
        spdlog::debug("saveDDS_BC4: {} ({}x{})", path.string(), width, height);
    return ok;
}

// ─── saveDDS_BC1 ───────────────────────────────────────────

bool DDSUtils::saveDDS_BC1(const std::filesystem::path& path, int width, int height, const uint8_t* rgbaPixels,
                           bool srgb)
{
    DirectX::ScratchImage scratch;
    HRESULT hr = createScratchFromRGBA(width, height, rgbaPixels, scratch, srgb);
    if (FAILED(hr))
    {
        spdlog::error("saveDDS_BC1: init failed (0x{:08X})", static_cast<unsigned>(hr));
        return false;
    }

    bool ok = compressAndSave(scratch, srgb ? DXGI_FORMAT_BC1_UNORM_SRGB : DXGI_FORMAT_BC1_UNORM, path,
                              detectAlphaMode(rgbaPixels, width, height));
    if (ok)
        spdlog::debug("saveDDS_BC1: {} ({}x{})", path.string(), width, height);
    return ok;
}

// ─── saveDDS_RGBA ──────────────────────────────────────────

bool DDSUtils::saveDDS_RGBA(const std::filesystem::path& path, int width, int height, const uint8_t* rgbaPixels,
                            bool srgb)
{
    DirectX::ScratchImage scratch;
    HRESULT hr = createScratchFromRGBA(width, height, rgbaPixels, scratch, srgb);
    if (FAILED(hr))
    {
        spdlog::error("saveDDS_RGBA: init failed (0x{:08X})", static_cast<unsigned>(hr));
        return false;
    }

    DirectX::ScratchImage mipChain;
    if (!generateMipChain(scratch, mipChain))
    {
        return false;
    }

    auto metadata = mipChain.GetMetadata();
    metadata.SetAlphaMode(detectAlphaMode(rgbaPixels, width, height));

    std::filesystem::create_directories(path.parent_path());

    hr = DirectX::SaveToDDSFile(mipChain.GetImages(), mipChain.GetImageCount(), metadata, DirectX::DDS_FLAGS_NONE,
                                toWide(path).c_str());

    if (FAILED(hr))
    {
        spdlog::error("saveDDS_RGBA: save failed (0x{:08X}) -> {}", static_cast<unsigned>(hr), path.string());
        return false;
    }

    spdlog::debug("saveDDS_RGBA: {} ({}x{})", path.string(), width, height);
    return true;
}

// ─── copyDDS ───────────────────────────────────────────────

bool DDSUtils::copyDDS(const std::filesystem::path& src, const std::filesystem::path& dst)
{
    if (!std::filesystem::exists(src))
    {
        spdlog::error("copyDDS: source not found: {}", src.string());
        return false;
    }

    std::filesystem::create_directories(dst.parent_path());

    std::error_code ec;
    std::filesystem::copy_file(src, dst, std::filesystem::copy_options::overwrite_existing, ec);
    if (ec)
    {
        spdlog::error("copyDDS: copy failed: {} -> {} ({})", src.string(), dst.string(), ec.message());
        return false;
    }

    spdlog::debug("copyDDS: {} -> {}", src.string(), dst.string());
    return true;
}

} // namespace tpbr
