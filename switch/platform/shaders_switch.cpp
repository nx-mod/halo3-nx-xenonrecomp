// Shaders for SDL_GPU's deko3d backend (sdl3-nx): the renderer's Vulkan GLSL
// (CompileSpirv hands it over as is on the Switch) compiled to DKSH with UAM,
// which is linked in (switch/uam). DKSH is cached beside the PC's SPIR-V
// cache, by the hash of the GLSL UAM compiles.
//
// UAM compiles OpenGL GLSL, so two Vulkan-isms are rewritten first:
//   - descriptor sets. SDL_GPU's SPIR-V puts a stage's samplers, storage
//     textures and storage buffers in one set, numbered one after the other,
//     and its uniform buffers in the next; the deko3d backend numbers each
//     kind from 0 (SDL_gpu_deko3d.c). The counts the renumbering needs are
//     the create info's, which is why this happens at creation.
//   - gl_VertexIndex and gl_InstanceIndex, which are gl_VertexID and
//     gl_InstanceID there.

#include "gpu/renderer/internal.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <regex>

extern "C" int h3_dk_compile_glsl(int stage, const char* glsl, char** dksh, size_t* size);

namespace h3::gpu::renderer::internal
{
    namespace
    {
        enum class Kind { Sampler, StorageTexture, StorageBuffer, Uniform };

        struct Counts
        {
            uint32_t samplers = 0;
            uint32_t storageTextures = 0;      // read-only ones, for compute
            uint32_t storageBuffers = 0;       // read-only ones, for compute
            uint32_t readWriteTextures = 0;    // compute
        };

        // What a `layout(...)` declares, from the text after it.
        Kind KindOf(const std::string& rest)
        {
            if (rest.find("sampler") != std::string::npos)
                return Kind::Sampler;
            if (rest.find("image") != std::string::npos)
                return Kind::StorageTexture;
            if (rest.find("buffer") != std::string::npos)
                return Kind::StorageBuffer;
            return Kind::Uniform;
        }

        // The deko3d binding of SDL_GPU's SPIR-V (set, binding), or -1 if
        // the pair doesn't fit the stage's layout.
        int DekoBinding(int stage, uint32_t set, uint32_t binding, Kind kind, const Counts& counts)
        {
            if (stage == 2)  // compute: 0 read-only, 1 read-write, 2 uniforms
            {
                switch (set)
                {
                case 0:
                    if (kind == Kind::Sampler)
                        return int(binding);
                    if (kind == Kind::StorageTexture)
                        return int(binding - counts.samplers);
                    return int(binding - counts.samplers - counts.storageTextures);
                case 1:
                    if (kind == Kind::StorageTexture)
                        return int(counts.storageTextures + binding);
                    return int(counts.storageBuffers + binding - counts.readWriteTextures);
                case 2:
                    return int(binding);
                }
                return -1;
            }
            // vertex: 0 resources, 1 uniforms; fragment: 2 and 3
            uint32_t resources = stage == 0 ? 0 : 2;
            if (set == resources + 1)
                return int(binding);
            if (set != resources)
                return -1;
            if (kind == Kind::Sampler)
                return int(binding);
            if (kind == Kind::StorageTexture)
                return int(binding - counts.samplers);
            return int(binding - counts.samplers - counts.storageTextures);
        }

        std::string ToDekoGlsl(int stage, const std::string& glsl, const Counts& counts, std::string& error)
        {
            static const std::regex layout(R"(layout\s*\(([^)]*)\))");
            static const std::regex setPart(R"(\bset\s*=\s*(\d+)\s*,?\s*)");
            static const std::regex bindingPart(R"(\bbinding\s*=\s*(\d+))");
            std::string out;
            out.reserve(glsl.size());
            auto last = glsl.cbegin();
            for (std::sregex_iterator it(glsl.begin(), glsl.end(), layout), end; it != end; ++it)
            {
                const std::smatch& match = *it;
                std::string inner = match[1].str();
                std::smatch set, binding;
                out.append(last, match[0].first);
                last = match[0].second;
                if (!std::regex_search(inner, set, setPart) || !std::regex_search(inner, binding, bindingPart))
                {
                    out += match[0].str();
                    continue;
                }
                size_t restEnd = glsl.find_first_of(";{", size_t(match[0].second - glsl.cbegin()));
                std::string rest = glsl.substr(size_t(match[0].second - glsl.cbegin()),
                    restEnd == std::string::npos ? std::string::npos : restEnd - size_t(match[0].second - glsl.cbegin()));
                int deko = DekoBinding(stage, uint32_t(std::stoul(set[1].str())), uint32_t(std::stoul(binding[1].str())),
                    KindOf(rest), counts);
                if (deko < 0)
                {
                    error = "no deko3d binding for " + match[0].str() + rest;
                    return {};
                }
                inner = std::regex_replace(inner, setPart, "", std::regex_constants::format_first_only);
                inner = std::regex_replace(inner, bindingPart, "binding = " + std::to_string(deko),
                    std::regex_constants::format_first_only);
                out += "layout(" + inner + ")";
            }
            out.append(last, glsl.cend());

            static const std::regex vertexIndex(R"(\bgl_VertexIndex\b)");
            static const std::regex instanceIndex(R"(\bgl_InstanceIndex\b)");
            out = std::regex_replace(out, vertexIndex, "gl_VertexID");
            return std::regex_replace(out, instanceIndex, "gl_InstanceID");
        }

        // DKSH for GLSL: from the cache, or compiled and stored there.
        std::vector<uint8_t> CompileDksh(int stage, const Uint8* code, size_t size, const Counts& counts)
        {
            std::string error;
            std::string glsl = ToDekoGlsl(stage, std::string(reinterpret_cast<const char*>(code), size), counts, error);
            if (glsl.empty())
            {
                LogOnce("shader binding rewrite failed: " + error);
                return {};
            }

            std::filesystem::path path;
            if (!g_shaderCacheDirectory.empty())
            {
                char name[32];
                snprintf(name, sizeof(name), "%016llx.dksh", (unsigned long long)Hash(glsl.data(), glsl.size(), uint64_t(stage) + 1));
                path = g_shaderCacheDirectory / name;
                std::ifstream file(path, std::ios::binary);
                if (file)
                {
                    std::vector<uint8_t> dksh((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
                    if (!dksh.empty())
                        return dksh;
                }
            }

            char* dksh = nullptr;
            size_t dkshSize = 0;
            if (!h3_dk_compile_glsl(stage, glsl.c_str(), &dksh, &dkshSize))
            {
                // UAM prints the compiler's messages to stderr.
                static const bool dump = EnvFlag("H3_DUMP_GLSL");
                LogOnce("UAM failed to compile a shader" + std::string(dump ? "" : " (H3_DUMP_GLSL=1 logs its GLSL)"));
                if (dump)
                    fprintf(stderr, "[render] GLSL UAM refused:\n%s\n", glsl.c_str());
                return {};
            }
            std::vector<uint8_t> result(dksh, dksh + dkshSize);
            free(dksh);
            if (!path.empty())
            {
                // Write then rename, so a killed run never leaves half a file.
                std::filesystem::path temporary = path;
                temporary += ".tmp";
                std::ofstream(temporary, std::ios::binary).write(reinterpret_cast<const char*>(result.data()), result.size());
                std::error_code renameError;
                std::filesystem::rename(temporary, path, renameError);
            }
            return result;
        }
    }

    SDL_GPUShader* CreateGpuShader(const SDL_GPUShaderCreateInfo& info)
    {
        Counts counts;
        counts.samplers = info.num_samplers;
        counts.storageTextures = info.num_storage_textures;
        counts.storageBuffers = info.num_storage_buffers;
        int stage = info.stage == SDL_GPU_SHADERSTAGE_VERTEX ? 0 : 1;
        std::vector<uint8_t> dksh = CompileDksh(stage, info.code, info.code_size, counts);
        if (dksh.empty())
            return nullptr;
        SDL_GPUShaderCreateInfo deko = info;
        deko.code = dksh.data();
        deko.code_size = dksh.size();
        deko.format = SDL_GPU_SHADERFORMAT_PRIVATE;
        return SDL_CreateGPUShader(g_device, &deko);
    }

    SDL_GPUComputePipeline* CreateGpuComputePipeline(const SDL_GPUComputePipelineCreateInfo& info)
    {
        Counts counts;
        counts.samplers = info.num_samplers;
        counts.storageTextures = info.num_readonly_storage_textures;
        counts.storageBuffers = info.num_readonly_storage_buffers;
        counts.readWriteTextures = info.num_readwrite_storage_textures;
        std::vector<uint8_t> dksh = CompileDksh(2, info.code, info.code_size, counts);
        if (dksh.empty())
            return nullptr;
        SDL_GPUComputePipelineCreateInfo deko = info;
        deko.code = dksh.data();
        deko.code_size = dksh.size();
        deko.format = SDL_GPU_SHADERFORMAT_PRIVATE;
        return SDL_CreateGPUComputePipeline(g_device, &deko);
    }
}
