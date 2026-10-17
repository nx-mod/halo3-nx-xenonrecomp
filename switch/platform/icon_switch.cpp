// The forwarder icon, as haloce-nx makes it: the title image every 360 game
// carries in its executable (default.xex's XDBF resource, image 0x8000, a
// 64x64 PNG), written beside the NRO as a 256x256 icon.jpg, the size and
// format Switch icons are. Only when there's none yet, so one the user
// put there stays.

#include "kernel/modules.h"
#include "memory.h"

#include <SDL3/SDL.h>
#include <jpeglib.h>
#include <xex.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <sys/stat.h>

extern "C" char** __system_argv;  // libnx: [0] is the NRO's path
extern "C" int __system_argc;

namespace h3::video
{
    namespace
    {
        constexpr int kIconSize = 256;
        constexpr uint64_t kTitleImage = 0x8000;
        constexpr uint16_t kImageNamespace = 2;

        uint32_t ReadBe32(const uint8_t* bytes)
        {
            return uint32_t(bytes[0]) << 24 | uint32_t(bytes[1]) << 16 | uint32_t(bytes[2]) << 8 | bytes[3];
        }

        // The title image's PNG inside an XDBF blob, or null.
        const uint8_t* FindTitleImage(const uint8_t* xdbf, uint32_t size, uint32_t& pngSize)
        {
            if (size < 24 || memcmp(xdbf, "XDBF", 4) != 0)
                return nullptr;
            uint32_t entryTableLength = ReadBe32(xdbf + 8), entryCount = ReadBe32(xdbf + 12);
            uint32_t freeTableLength = ReadBe32(xdbf + 16);
            uint64_t data = 24 + uint64_t(entryTableLength) * 18 + uint64_t(freeTableLength) * 8;
            for (uint32_t i = 0; i < entryCount && 24 + (i + 1) * 18 <= size; i++)
            {
                // Entry: u16 namespace, u64 id, u32 offset, u32 length.
                const uint8_t* entry = xdbf + 24 + i * 18;
                uint16_t space = uint16_t(entry[0] << 8 | entry[1]);
                uint64_t id = uint64_t(ReadBe32(entry + 2)) << 32 | ReadBe32(entry + 6);
                uint32_t offset = ReadBe32(entry + 10), length = ReadBe32(entry + 14);
                if (space == kImageNamespace && id == kTitleImage && data + offset + length <= size)
                {
                    pngSize = length;
                    return xdbf + data + offset;
                }
            }
            return nullptr;
        }

        // default.xex's title image, from its loaded resources, or null.
        const uint8_t* TitleImagePng(uint32_t& pngSize)
        {
            const auto& headers = kernel::ExecutableModule().headers;
            auto* info = static_cast<const be<uint32_t>*>(getOptHeaderPtr(headers.data(), XEX_HEADER_RESOURCE_INFO));
            if (info == nullptr)
                return nullptr;
            // A size, then 16-byte entries: char name[8], address, size.
            auto* entries = reinterpret_cast<const uint8_t*>(info + 1);
            uint32_t count = (info->get() - 4) / 16;
            for (uint32_t i = 0; i < count; i++)
            {
                const uint8_t* entry = entries + i * 16;
                const uint8_t* blob = GuestPtr<const uint8_t>(ReadBe32(entry + 8));
                if (const uint8_t* png = FindTitleImage(blob, ReadBe32(entry + 12), pngSize))
                    return png;
            }
            return nullptr;
        }

        bool WriteJpeg(const char* path, const SDL_Surface* rgb)
        {
            FILE* file = fopen(path, "wb");
            if (file == nullptr)
                return false;
            jpeg_compress_struct compress{};
            jpeg_error_mgr error{};
            compress.err = jpeg_std_error(&error);
            jpeg_create_compress(&compress);
            jpeg_stdio_dest(&compress, file);
            compress.image_width = JDIMENSION(rgb->w);
            compress.image_height = JDIMENSION(rgb->h);
            compress.input_components = 3;
            compress.in_color_space = JCS_RGB;
            jpeg_set_defaults(&compress);
            jpeg_set_quality(&compress, 92, TRUE);
            jpeg_start_compress(&compress, TRUE);
            for (int row = 0; row < rgb->h; row++)
            {
                JSAMPROW line = static_cast<uint8_t*>(rgb->pixels) + size_t(row) * rgb->pitch;
                jpeg_write_scanlines(&compress, &line, 1);
            }
            jpeg_finish_compress(&compress);
            jpeg_destroy_compress(&compress);
            fclose(file);
            return true;
        }
    }

    // Writes icon.jpg beside the NRO if there's none. After LoadExecutable.
    void WriteForwarderIcon()
    {
        if (__system_argc < 1 || __system_argv == nullptr || __system_argv[0] == nullptr)
            return;
        std::string path = __system_argv[0];
        size_t slash = path.rfind('/');
        if (slash == std::string::npos)
            return;
        path = path.substr(0, slash + 1) + "icon.jpg";
        struct stat information;
        if (stat(path.c_str(), &information) == 0)
            return;

        uint32_t pngSize = 0;
        const uint8_t* png = TitleImagePng(pngSize);
        if (png == nullptr)
        {
            fprintf(stderr, "[icon] default.xex has no title image\n");
            return;
        }
        // Scaled as RGBA (linear scaling wants 32-bit pixels), then RGB for JPEG.
        SDL_Surface* image = SDL_LoadPNG_IO(SDL_IOFromConstMem(png, pngSize), true);
        SDL_Surface* rgba = image ? SDL_ConvertSurface(image, SDL_PIXELFORMAT_RGBA32) : nullptr;
        SDL_Surface* scaled = rgba ? SDL_ScaleSurface(rgba, kIconSize, kIconSize, SDL_SCALEMODE_LINEAR) : nullptr;
        SDL_Surface* icon = scaled ? SDL_ConvertSurface(scaled, SDL_PIXELFORMAT_RGB24) : nullptr;
        if (icon && WriteJpeg(path.c_str(), icon))
            fprintf(stderr, "[icon] wrote %s\n", path.c_str());
        else
            fprintf(stderr, "[icon] couldn't make %s: %s\n", path.c_str(), SDL_GetError());
        SDL_DestroySurface(icon);
        SDL_DestroySurface(scaled);
        SDL_DestroySurface(rgba);
        SDL_DestroySurface(image);
    }
}
