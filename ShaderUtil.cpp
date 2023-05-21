#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include "Util.h"

#define VK_USE_PLATFORM_WIN32_KHR
#define VULKAN_HPP_NO_EXCEPTIONS
#define VULKAN_HPP_TYPESAFE_CONVERSION
#include <vulkan/vulkan.h>
#include <glslang/Public/ShaderLang.h>
#include <glslang/SPIRV/GlslangToSpv.h>
#include <iostream>

VkShaderStageFlagBits find_shader_stage(const std::string& ext) {
    if (ext == "vert") { return VK_SHADER_STAGE_VERTEX_BIT; }
    else if (ext == "frag") { return VK_SHADER_STAGE_FRAGMENT_BIT; }
    else if (ext == "comp") { return VK_SHADER_STAGE_COMPUTE_BIT; }
    else if (ext == "geom") { return VK_SHADER_STAGE_GEOMETRY_BIT; }
    else if (ext == "tesc") { return VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT; }
    else if (ext == "tese") { return VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT; }
    else if (ext == "rgen") { return VK_SHADER_STAGE_RAYGEN_BIT_KHR; }
    else if (ext == "rahit") { return VK_SHADER_STAGE_ANY_HIT_BIT_KHR; }
    else if (ext == "rchit") { return VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR; }
    else if (ext == "rmiss") { return VK_SHADER_STAGE_MISS_BIT_KHR; }
    else if (ext == "rint") { return VK_SHADER_STAGE_INTERSECTION_BIT_KHR; }
    else if (ext == "rcall") { return VK_SHADER_STAGE_CALLABLE_BIT_KHR; }

    throw std::runtime_error("File extension `" + ext + "` does not have a vulkan shader stage.");
}

inline EShLanguage FindShaderLanguage(VkShaderStageFlagBits stage) {
    switch (stage) {
    case VK_SHADER_STAGE_VERTEX_BIT:                  return EShLangVertex;
    case VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT:    return EShLangTessControl;
    case VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT: return EShLangTessEvaluation;
    case VK_SHADER_STAGE_GEOMETRY_BIT:                return EShLangGeometry;
    case VK_SHADER_STAGE_FRAGMENT_BIT:                return EShLangFragment;
    case VK_SHADER_STAGE_COMPUTE_BIT:                 return EShLangCompute;
    case VK_SHADER_STAGE_RAYGEN_BIT_KHR:              return EShLangRayGen;
    case VK_SHADER_STAGE_ANY_HIT_BIT_KHR:             return EShLangAnyHit;
    case VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR:         return EShLangClosestHit;
    case VK_SHADER_STAGE_MISS_BIT_KHR:                return EShLangMiss;
    case VK_SHADER_STAGE_INTERSECTION_BIT_KHR:        return EShLangIntersect;
    case VK_SHADER_STAGE_CALLABLE_BIT_KHR:            return EShLangCallable;
    default:                                          return EShLangVertex;
    }
}

static TBuiltInResource InitResources() {
    TBuiltInResource Resources;

    Resources.maxLights                                 = 32;
    Resources.maxClipPlanes                             = 6;
    Resources.maxTextureUnits                           = 32;
    Resources.maxTextureCoords                          = 32;
    Resources.maxVertexAttribs                          = 64;
    Resources.maxVertexUniformComponents                = 4096;
    Resources.maxVaryingFloats                          = 64;
    Resources.maxVertexTextureImageUnits                = 32;
    Resources.maxCombinedTextureImageUnits              = 80;
    Resources.maxTextureImageUnits                      = 32;
    Resources.maxFragmentUniformComponents              = 4096;
    Resources.maxDrawBuffers                            = 32;
    Resources.maxVertexUniformVectors                   = 128;
    Resources.maxVaryingVectors                         = 8;
    Resources.maxFragmentUniformVectors                 = 16;
    Resources.maxVertexOutputVectors                    = 16;
    Resources.maxFragmentInputVectors                   = 15;
    Resources.minProgramTexelOffset                     = -8;
    Resources.maxProgramTexelOffset                     = 7;
    Resources.maxClipDistances                          = 8;
    Resources.maxComputeWorkGroupCountX                 = 65535;
    Resources.maxComputeWorkGroupCountY                 = 65535;
    Resources.maxComputeWorkGroupCountZ                 = 65535;
    Resources.maxComputeWorkGroupSizeX                  = 1024;
    Resources.maxComputeWorkGroupSizeY                  = 1024;
    Resources.maxComputeWorkGroupSizeZ                  = 64;
    Resources.maxComputeUniformComponents               = 1024;
    Resources.maxComputeTextureImageUnits               = 16;
    Resources.maxComputeImageUniforms                   = 8;
    Resources.maxComputeAtomicCounters                  = 8;
    Resources.maxComputeAtomicCounterBuffers            = 1;
    Resources.maxVaryingComponents                      = 60;
    Resources.maxVertexOutputComponents                 = 64;
    Resources.maxGeometryInputComponents                = 64;
    Resources.maxGeometryOutputComponents               = 128;
    Resources.maxFragmentInputComponents                = 128;
    Resources.maxImageUnits                             = 8;
    Resources.maxCombinedImageUnitsAndFragmentOutputs   = 8;
    Resources.maxCombinedShaderOutputResources          = 8;
    Resources.maxImageSamples                           = 0;
    Resources.maxVertexImageUniforms                    = 0;
    Resources.maxTessControlImageUniforms               = 0;
    Resources.maxTessEvaluationImageUniforms            = 0;
    Resources.maxGeometryImageUniforms                  = 0;
    Resources.maxFragmentImageUniforms                  = 8;
    Resources.maxCombinedImageUniforms                  = 8;
    Resources.maxGeometryTextureImageUnits              = 16;
    Resources.maxGeometryOutputVertices                 = 256;
    Resources.maxGeometryTotalOutputComponents          = 1024;
    Resources.maxGeometryUniformComponents              = 1024;
    Resources.maxGeometryVaryingComponents              = 64;
    Resources.maxTessControlInputComponents             = 128;
    Resources.maxTessControlOutputComponents            = 128;
    Resources.maxTessControlTextureImageUnits           = 16;
    Resources.maxTessControlUniformComponents           = 1024;
    Resources.maxTessControlTotalOutputComponents       = 4096;
    Resources.maxTessEvaluationInputComponents          = 128;
    Resources.maxTessEvaluationOutputComponents         = 128;
    Resources.maxTessEvaluationTextureImageUnits        = 16;
    Resources.maxTessEvaluationUniformComponents        = 1024;
    Resources.maxTessPatchComponents                    = 120;
    Resources.maxPatchVertices                          = 32;
    Resources.maxTessGenLevel                           = 64;
    Resources.maxViewports                              = 16;
    Resources.maxVertexAtomicCounters                   = 0;
    Resources.maxTessControlAtomicCounters              = 0;
    Resources.maxTessEvaluationAtomicCounters           = 0;
    Resources.maxGeometryAtomicCounters                 = 0;
    Resources.maxFragmentAtomicCounters                 = 8;
    Resources.maxCombinedAtomicCounters                 = 8;
    Resources.maxAtomicCounterBindings                  = 1;
    Resources.maxVertexAtomicCounterBuffers             = 0;
    Resources.maxTessControlAtomicCounterBuffers        = 0;
    Resources.maxTessEvaluationAtomicCounterBuffers     = 0;
    Resources.maxGeometryAtomicCounterBuffers           = 0;
    Resources.maxFragmentAtomicCounterBuffers           = 1;
    Resources.maxCombinedAtomicCounterBuffers           = 1;
    Resources.maxAtomicCounterBufferSize                = 16384;
    Resources.maxTransformFeedbackBuffers               = 4;
    Resources.maxTransformFeedbackInterleavedComponents = 64;
    Resources.maxCullDistances                          = 8;
    Resources.maxCombinedClipAndCullDistances           = 8;
    Resources.maxSamples                                = 4;
    Resources.maxMeshOutputVerticesNV                   = 256;
    Resources.maxMeshOutputPrimitivesNV                 = 512;
    Resources.maxMeshWorkGroupSizeX_NV                  = 32;
    Resources.maxMeshWorkGroupSizeY_NV                  = 1;
    Resources.maxMeshWorkGroupSizeZ_NV                  = 1;
    Resources.maxTaskWorkGroupSizeX_NV                  = 32;
    Resources.maxTaskWorkGroupSizeY_NV                  = 1;
    Resources.maxTaskWorkGroupSizeZ_NV                  = 1;
    Resources.maxMeshViewCountNV                        = 4;

    Resources.maxDualSourceDrawBuffersEXT               = 1;

    Resources.limits.nonInductiveForLoops                 = 1;
    Resources.limits.whileLoops                           = 1;
    Resources.limits.doWhileLoops                         = 1;
    Resources.limits.generalUniformIndexing               = 1;
    Resources.limits.generalAttributeMatrixVectorIndexing = 1;
    Resources.limits.generalVaryingIndexing               = 1;
    Resources.limits.generalSamplerIndexing               = 1;
    Resources.limits.generalVariableIndexing              = 1;
    Resources.limits.generalConstantMatrixVectorIndexing  = 1;

    return Resources;
}

bool compile_to_spirv(VkShaderStageFlagBits stage,
    const std::vector<uint8_t>& glsl_source,
    const std::string& entry_point,
    std::vector<std::uint32_t>& spirv,
    std::string& info_log)
{
    glslang::InitializeProcess();

    EShMessages messages = static_cast<EShMessages>(EShMsgDefault | EShMsgVulkanRules | EShMsgSpvRules);

    EShLanguage language = FindShaderLanguage(stage);
    std::string source = std::string(glsl_source.begin(), glsl_source.end());

    const char* file_name_list[1] = { "" };
    const char* shader_source = reinterpret_cast<const char*>(source.data());

    glslang::TShader shader(language);
    shader.setStringsWithLengthsAndNames(&shader_source, nullptr, file_name_list, 1);
    shader.setEntryPoint(entry_point.c_str());
    shader.setSourceEntryPoint(entry_point.c_str());
    //shader.setPreamble(shader_variant.get_preamble().c_str());
    //shader.addProcesses(shader_variant.get_processes());
    //if (GLSLCompiler::env_target_language != glslang::EShTargetLanguage::EShTargetNone) {
    //    shader.setEnvTarget(GLSLCompiler::env_target_language, GLSLCompiler::env_target_language_version);
    //}

    TBuiltInResource resources = InitResources();
    if (!shader.parse(&resources, 100, false, messages)) {
        info_log = std::string(shader.getInfoLog()) + "\n" + std::string(shader.getInfoDebugLog());
        return false;
    }

    // Add shader to new program object.
    glslang::TProgram program;
    program.addShader(&shader);

    // Link program.
    if (!program.link(messages)) {
        info_log = std::string(program.getInfoLog()) + "\n" + std::string(program.getInfoDebugLog());
        return false;
    }

    // Save any info log that was generated.
    if (shader.getInfoLog()) {
        info_log += std::string(shader.getInfoLog()) + "\n" + std::string(shader.getInfoDebugLog()) + "\n";
    }

    if (program.getInfoLog()) {
        info_log += std::string(program.getInfoLog()) + "\n" + std::string(program.getInfoDebugLog());
    }

    glslang::TIntermediate* intermediate = program.getIntermediate(language);

    // Translate to SPIRV.
    if (!intermediate) {
        info_log += "Failed to get shared intermediate code.\n";
        return false;
    }

    spv::SpvBuildLogger logger;

    glslang::GlslangToSpv(*intermediate, spirv, &logger);

    info_log += logger.getAllMessages() + "\n";

    // Shutdown glslang library.
    glslang::FinalizeProcess();

    return true;
}

/**
 * @brief Helper function to load a shader module.
 * @param context A Vulkan context with a device.
 * @param path The path for the shader (relative to the assets directory).
 * @returns A VkShaderModule handle. Aborts execution if shader creation fails.
 */
VkShaderModule load_shader_module(VkDevice device, const char * path) {
    auto buffer = read_binary_file(path, 0);

    std::string file_ext = path;

    // Extract extension name from the glsl shader file
    file_ext = file_ext.substr(file_ext.find_last_of(".") + 1);

    std::vector<uint32_t> spirv;
    std::string           info_log;

    // Compile the GLSL source
    if (!compile_to_spirv(find_shader_stage(file_ext), buffer, "main", /*{},*/ spirv, info_log)) {
        std::cout << "Failed to compile shader, Error: " << info_log.c_str() << std::endl;
        return VK_NULL_HANDLE;
    }

    VkShaderModuleCreateInfo createInfo{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
    createInfo.codeSize = spirv.size() * sizeof(uint32_t);
    createInfo.pCode = spirv.data();

    VkShaderModule shaderModule;
    VkResult res = vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule);
    if (res != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }
    return shaderModule;
}
