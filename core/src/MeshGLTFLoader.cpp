#include "Manager.hpp"
#include "Animation.hpp"
#include "Utils.hpp"
#include "cgltf.h"

#include <SDL2/SDL.h>

#include <algorithm>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/gtc/type_ptr.hpp>

namespace
{
glm::mat4 makeMat4(const float *m)
{
    return glm::make_mat4(m);
}

std::string gltfTexturePath(const std::string &meshPath,
                            const std::string &textureDir,
                            const cgltf_texture_view &view)
{
    if (!view.texture || !view.texture->image || !view.texture->image->uri)
        return std::string();

    const std::string baseDir = textureDir.empty() ? PathDirectory(meshPath) : textureDir;
    return ResolveTexturePath(baseDir, view.texture->image->uri);
}

Texture *loadGltfBaseColorTexture(const std::string &meshName,
                                  const std::string &meshPath,
                                  const std::string &textureDir,
                                  const cgltf_material *material)
{
    if (!material || !material->has_pbr_metallic_roughness)
        return TextureManager::instance().getWhite();

    const std::string texPath = gltfTexturePath(meshPath, textureDir,
                                                material->pbr_metallic_roughness.base_color_texture);
    if (texPath.empty())
        return TextureManager::instance().getWhite();

    const std::string texName = meshName + "::" + PathFilename(texPath);
    if (Texture *texture = TextureManager::instance().load(texName, texPath))
        return texture;
    return TextureManager::instance().getWhite();
}

Material *makeGltfMaterial(const std::string &meshName,
                           const std::string &meshPath,
                           const std::string &textureDir,
                           const cgltf_material *material,
                           int fallbackIndex)
{
    Material *mat = new Material();
    mat->type = MaterialType::Custom;
    mat->name = meshName + "::mat_" + std::to_string(fallbackIndex);

    glm::vec4 baseColor(1.0f);
    if (material && material->name && material->name[0] != '\0')
        mat->name = material->name;
    if (material && material->has_pbr_metallic_roughness)
    {
        const float *c = material->pbr_metallic_roughness.base_color_factor;
        baseColor = glm::vec4(c[0], c[1], c[2], c[3]);
        if (material->alpha_mode == cgltf_alpha_mode_blend || baseColor.a < 0.999f)
            mat->setBlend(true);
        if (material->double_sided)
            mat->setCullFace(false);
    }

    mat->setVec4("u_color", baseColor);
    mat->setTexture("u_albedo", loadGltfBaseColorTexture(meshName, meshPath, textureDir, material));
    return mat;
}

bool loadGLTFAnimated(const std::string &name,
                      const std::string &path,
                      const std::string &textureDir,
                      AnimatedMesh *outMesh)
{
    if (!outMesh)
        return false;

    cgltf_options opts{};
    cgltf_data *data = nullptr;
    if (cgltf_parse_file(&opts, path.c_str(), &data) != cgltf_result_success)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[GLTF] Failed to parse %s", path.c_str());
        return false;
    }

    if (cgltf_load_buffers(&opts, data, path.c_str()) != cgltf_result_success)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[GLTF] Failed to load buffers for %s", path.c_str());
        cgltf_free(data);
        return false;
    }

    outMesh->name = name;
    outMesh->buffer.vertices.clear();
    outMesh->buffer.indices.clear();
    outMesh->surfaces.clear();
    outMesh->release_materials();
    outMesh->releaseAnimations();
    outMesh->bones.clear();
    outMesh->finalMatrices.clear();

    const cgltf_skin *skinUsed = nullptr;
    const cgltf_node *meshNodeUsed = nullptr;
    for (cgltf_size i = 0; i < data->nodes_count; ++i)
    {
        const cgltf_node &node = data->nodes[i];
        if (node.mesh && node.skin && node.skin->joints_count > 0)
        {
            skinUsed = node.skin;
            meshNodeUsed = &node;
            break;
        }
    }

    if (!skinUsed || !meshNodeUsed)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "[GLTF] No skinned node found in %s", path.c_str());
        cgltf_free(data);
        return false;
    }

    std::unordered_map<const cgltf_node *, int> boneIndexByNode;
    boneIndexByNode.reserve(skinUsed->joints_count);

    float meshWorldM[16];
    cgltf_node_transform_world(meshNodeUsed, meshWorldM);
    const glm::mat4 meshWorld = makeMat4(meshWorldM);
    const glm::mat4 invMeshWorld = glm::inverse(meshWorld);

    outMesh->bones.resize(skinUsed->joints_count);
    for (cgltf_size i = 0; i < skinUsed->joints_count; ++i)
    {
        const cgltf_node *joint = skinUsed->joints[i];
        boneIndexByNode[joint] = (int)i;
    }

    for (cgltf_size i = 0; i < skinUsed->joints_count; ++i)
    {
        const cgltf_node *joint = skinUsed->joints[i];
        Bone &bone = outMesh->bones[i];
        bone.name = (joint && joint->name) ? joint->name : ("joint_" + std::to_string(i));

        if (joint && joint->parent)
        {
            auto parentIt = boneIndexByNode.find(joint->parent);
            bone.parent = (parentIt != boneIndexByNode.end()) ? parentIt->second : -1;
        }
        else
        {
            bone.parent = -1;
        }

        float localM[16];
        float worldM[16];
        cgltf_node_transform_local(joint, localM);
        cgltf_node_transform_world(joint, worldM);

        const glm::mat4 local = makeMat4(localM);
        const glm::mat4 jointMeshSpace = invMeshWorld * makeMat4(worldM);

        bone.localPose = (bone.parent >= 0) ? local : jointMeshSpace;
        bone.offset = glm::inverse(jointMeshSpace);
    }

    if (skinUsed->inverse_bind_matrices)
    {
        float ibm[16];
        const cgltf_size ibmCount = std::min(skinUsed->inverse_bind_matrices->count, skinUsed->joints_count);
        for (cgltf_size i = 0; i < ibmCount; ++i)
        {
            cgltf_accessor_read_float(skinUsed->inverse_bind_matrices, i, ibm, 16);
            outMesh->bones[i].offset = makeMat4(ibm);
        }
    }

    std::unordered_map<const cgltf_material *, int> materialMap;

    for (cgltf_size ni = 0; ni < data->nodes_count; ++ni)
    {
        const cgltf_node &node = data->nodes[ni];
        if (!node.mesh || node.skin != skinUsed)
            continue;

        for (cgltf_size pi = 0; pi < node.mesh->primitives_count; ++pi)
        {
            const cgltf_primitive &prim = node.mesh->primitives[pi];
            if (prim.type != cgltf_primitive_type_triangles)
                continue;

            cgltf_accessor *posAcc = nullptr;
            cgltf_accessor *norAcc = nullptr;
            cgltf_accessor *uvAcc = nullptr;
            cgltf_accessor *jointsAcc = nullptr;
            cgltf_accessor *weightsAcc = nullptr;

            for (cgltf_size ai = 0; ai < prim.attributes_count; ++ai)
            {
                const cgltf_attribute &attr = prim.attributes[ai];
                if (attr.type == cgltf_attribute_type_position)
                    posAcc = attr.data;
                else if (attr.type == cgltf_attribute_type_normal)
                    norAcc = attr.data;
                else if (attr.type == cgltf_attribute_type_texcoord && attr.index == 0)
                    uvAcc = attr.data;
                else if (attr.type == cgltf_attribute_type_joints && attr.index == 0)
                    jointsAcc = attr.data;
                else if (attr.type == cgltf_attribute_type_weights && attr.index == 0)
                    weightsAcc = attr.data;
            }

            if (!posAcc)
                continue;

            int matIndex = -1;
            auto matIt = materialMap.find(prim.material);
            if (matIt != materialMap.end())
            {
                matIndex = matIt->second;
            }
            else
            {
                Material *mat = makeGltfMaterial(name, path, textureDir, prim.material, (int)outMesh->materials.size());
                matIndex = outMesh->add_material(mat);
                materialMap[prim.material] = matIndex;
            }

            const uint32_t vertOffset = (uint32_t)outMesh->buffer.vertices.size();
            const uint32_t indexStart = (uint32_t)outMesh->buffer.indices.size();
            const cgltf_size vertexCount = posAcc->count;
            outMesh->buffer.vertices.resize(vertOffset + vertexCount);

            for (cgltf_size vi = 0; vi < vertexCount; ++vi)
            {
                AnimatedVertex &v = outMesh->buffer.vertices[vertOffset + vi];
                v.tangent = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);

                float f4[4] = {0, 0, 0, 0};
                cgltf_accessor_read_float(posAcc, vi, f4, 3);
                v.position = glm::vec3(f4[0], f4[1], f4[2]);

                if (norAcc)
                {
                    cgltf_accessor_read_float(norAcc, vi, f4, 3);
                    v.normal = glm::vec3(f4[0], f4[1], f4[2]);
                }
                else
                {
                    v.normal = glm::vec3(0.0f, 1.0f, 0.0f);
                }

                if (uvAcc)
                {
                    cgltf_accessor_read_float(uvAcc, vi, f4, 2);
                    v.uv = glm::vec2(f4[0], f4[1]);
                }
                else
                {
                    v.uv = glm::vec2(0.0f);
                }

                if (jointsAcc && weightsAcc)
                {
                    cgltf_uint joints[4] = {0, 0, 0, 0};
                    float weights[4] = {0, 0, 0, 0};
                    cgltf_accessor_read_uint(jointsAcc, vi, joints, 4);
                    cgltf_accessor_read_float(weightsAcc, vi, weights, 4);

                    v.boneIds = glm::ivec4(
                        (int)std::min<cgltf_uint>(joints[0], skinUsed->joints_count > 0 ? (cgltf_uint)skinUsed->joints_count - 1u : 0u),
                        (int)std::min<cgltf_uint>(joints[1], skinUsed->joints_count > 0 ? (cgltf_uint)skinUsed->joints_count - 1u : 0u),
                        (int)std::min<cgltf_uint>(joints[2], skinUsed->joints_count > 0 ? (cgltf_uint)skinUsed->joints_count - 1u : 0u),
                        (int)std::min<cgltf_uint>(joints[3], skinUsed->joints_count > 0 ? (cgltf_uint)skinUsed->joints_count - 1u : 0u));
                    v.boneWeights = glm::vec4(weights[0], weights[1], weights[2], weights[3]);

                    const float sum = v.boneWeights.x + v.boneWeights.y + v.boneWeights.z + v.boneWeights.w;
                    if (sum > 1e-6f)
                        v.boneWeights /= sum;
                    else
                        v.boneWeights = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
                }
                else
                {
                    v.boneIds = glm::ivec4(0, 0, 0, 0);
                    v.boneWeights = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
                }
            }

            if (prim.indices)
            {
                const cgltf_size indexCount = prim.indices->count;
                for (cgltf_size ii = 0; ii < indexCount; ++ii)
                    outMesh->buffer.indices.push_back(
                        vertOffset + (uint32_t)cgltf_accessor_read_index(prim.indices, ii));
                outMesh->add_surface(indexStart, (uint32_t)indexCount, matIndex);
            }
            else
            {
                for (cgltf_size vi = 0; vi < vertexCount; ++vi)
                    outMesh->buffer.indices.push_back(vertOffset + (uint32_t)vi);
                outMesh->add_surface(indexStart, (uint32_t)vertexCount, matIndex);
            }
        }
    }

    for (cgltf_size ai = 0; ai < data->animations_count; ++ai)
    {
        const cgltf_animation &srcAnim = data->animations[ai];
        Animation *anim = new Animation();
        anim->name = (srcAnim.name && srcAnim.name[0] != '\0') ? srcAnim.name : ("anim_" + std::to_string(ai));
        anim->ticksPerSecond = 1.0f;
        anim->duration = 0.0f;
        anim->channels.resize(outMesh->bones.size());

        for (size_t bi = 0; bi < outMesh->bones.size(); ++bi)
            anim->channels[bi].boneName = outMesh->bones[bi].name;

        for (cgltf_size ci = 0; ci < srcAnim.channels_count; ++ci)
        {
            const cgltf_animation_channel &srcChannel = srcAnim.channels[ci];
            if (!srcChannel.sampler || !srcChannel.target_node)
                continue;

            auto boneIt = boneIndexByNode.find(srcChannel.target_node);
            if (boneIt == boneIndexByNode.end())
                continue;

            const int boneIndex = boneIt->second;
            AnimationChannel &dstChannel = anim->channels[boneIndex];
            dstChannel.boneIndex = boneIndex;

            cgltf_accessor *timeAcc = srcChannel.sampler->input;
            cgltf_accessor *valueAcc = srcChannel.sampler->output;
            if (!timeAcc || !valueAcc)
                continue;

            const cgltf_size keyCount = std::min(timeAcc->count, valueAcc->count);
            for (cgltf_size ki = 0; ki < keyCount; ++ki)
            {
                float t = 0.0f;
                cgltf_accessor_read_float(timeAcc, ki, &t, 1);
                anim->duration = std::max(anim->duration, t);

                if (srcChannel.target_path == cgltf_animation_path_type_translation)
                {
                    float v[3] = {0, 0, 0};
                    cgltf_accessor_read_float(valueAcc, ki, v, 3);
                    dstChannel.posKeys.push_back({t, glm::vec3(v[0], v[1], v[2])});
                }
                else if (srcChannel.target_path == cgltf_animation_path_type_rotation)
                {
                    float v[4] = {0, 0, 0, 1};
                    cgltf_accessor_read_float(valueAcc, ki, v, 4);
                    dstChannel.rotKeys.push_back({t, glm::normalize(glm::quat(v[3], v[0], v[1], v[2]))});
                }
                else if (srcChannel.target_path == cgltf_animation_path_type_scale)
                {
                    float v[3] = {1, 1, 1};
                    cgltf_accessor_read_float(valueAcc, ki, v, 3);
                    dstChannel.scaleKeys.push_back({t, glm::vec3(v[0], v[1], v[2])});
                }
            }
        }

        if (anim->duration <= 0.0f)
            anim->duration = 1.0f;
        outMesh->animations.push_back(anim);
    }

    if (outMesh->buffer.vertices.empty() || outMesh->buffer.indices.empty())
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "[GLTF] No skinned geometry in %s", path.c_str());
        cgltf_free(data);
        return false;
    }

    if (!outMesh->buffer.vertices.empty() && !outMesh->buffer.indices.empty())
        outMesh->compute_tangents();
    outMesh->finalMatrices.resize(outMesh->bones.size(), glm::mat4(1.0f));
    outMesh->upload();

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "[GLTF] Animated loaded '%s': verts=%zu tris=%zu bones=%zu anims=%zu surfs=%zu",
                path.c_str(),
                outMesh->buffer.vertices.size(),
                outMesh->buffer.indices.size() / 3u,
                outMesh->bones.size(),
                outMesh->animations.size(),
                outMesh->surfaces.size());

    cgltf_free(data);
    return true;
}
}

AnimatedMesh *AnimatedMeshManager::load_gltf(const std::string &name,
                                             const std::string &path,
                                             const std::string &texture_dir)
{
    if (auto *existing = get(name))
        return existing;

    AnimatedMesh *mesh = new AnimatedMesh();
    if (!loadGLTFAnimated(name, path, texture_dir, mesh))
    {
        delete mesh;
        return nullptr;
    }

    cache[name] = mesh;
    return mesh;
}
