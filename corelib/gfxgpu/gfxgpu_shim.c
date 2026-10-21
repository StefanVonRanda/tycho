#include <SDL3/SDL.h>
#include <SDL3_shadercross/SDL_shadercross.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gfxgpu_shaders.h"

/* Defined in gfx_shim.c. It is not exposed in gfx.ty: Tycho never sees the
   SDL_Window pointer. */
extern void *gfx_native_window(void *handle);

typedef struct {
    SDL_GPUDevice *device;
    SDL_GPUGraphicsPipeline *pipeline;
    SDL_GPUBuffer *vertices;
    SDL_GPUTexture *depth;
    SDL_GPUTexture *color;
    SDL_GPUBuffer *mesh;
    uint32_t mesh_cap;
    SDL_GPUGraphicsPipeline *hud_pipeline;
    SDL_GPUBuffer *hud_vb;
    SDL_GPUTexture *hud_tex;
    SDL_GPUSampler *hud_sampler;
    SDL_Window *window;
    uint32_t w, h;
    int offscreen;
} Gpu;

static int gpu_upload_mesh(Gpu *g, const double *verts, int64_t nfloats);

static int gpu_upload_vertices(Gpu *g) {
    static const float verts[] = {
        -0.6f, -0.45f,  0.25f,   1.0f, 0.25f, 0.25f,
         0.6f, -0.45f, -0.25f,   0.25f, 1.0f, 0.35f,
         0.0f,  0.60f,  0.0f,    0.3f, 0.45f, 1.0f,
    };
    SDL_GPUBufferCreateInfo bi = {0};
    bi.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
    bi.size = sizeof verts;
    g->vertices = SDL_CreateGPUBuffer(g->device, &bi);
    if (!g->vertices) return -1;

    SDL_GPUTransferBufferCreateInfo ti = {0};
    ti.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    ti.size = sizeof verts;
    SDL_GPUTransferBuffer *tb = SDL_CreateGPUTransferBuffer(g->device, &ti);
    if (!tb) return -2;
    void *map = SDL_MapGPUTransferBuffer(g->device, tb, false);
    if (!map) {
        SDL_ReleaseGPUTransferBuffer(g->device, tb);
        return -3;
    }
    memcpy(map, verts, sizeof verts);
    SDL_UnmapGPUTransferBuffer(g->device, tb);

    SDL_GPUCommandBuffer *cb = SDL_AcquireGPUCommandBuffer(g->device);
    if (!cb) {
        SDL_ReleaseGPUTransferBuffer(g->device, tb);
        return -4;
    }
    SDL_GPUTransferBufferLocation src = { tb, 0 };
    SDL_GPUBufferRegion dst = { g->vertices, 0, sizeof verts };
    SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass(cb);
    SDL_UploadToGPUBuffer(cp, &src, &dst, true);
    SDL_EndGPUCopyPass(cp);
    if (!SDL_SubmitGPUCommandBuffer(cb)) {
        SDL_ReleaseGPUTransferBuffer(g->device, tb);
        return -5;
    }
    SDL_ReleaseGPUTransferBuffer(g->device, tb);
    SDL_WaitForGPUIdle(g->device);
    return 0;
}

static int gpu_init_hud(Gpu *g) {
    SDL_ShaderCross_SPIRV_Info vi = {0};
    vi.bytecode = hud_vert_spv;
    vi.bytecode_size = hud_vert_spv_len;
    vi.entrypoint = "main";
    vi.shader_stage = SDL_SHADERCROSS_SHADERSTAGE_VERTEX;
    SDL_ShaderCross_GraphicsShaderResourceInfo vri = {0};
    SDL_GPUShader *vs = SDL_ShaderCross_CompileGraphicsShaderFromSPIRV(g->device, &vi, &vri, 0);
    SDL_ShaderCross_SPIRV_Info fi = {0};
    fi.bytecode = hud_frag_spv;
    fi.bytecode_size = hud_frag_spv_len;
    fi.entrypoint = "main";
    fi.shader_stage = SDL_SHADERCROSS_SHADERSTAGE_FRAGMENT;
    SDL_ShaderCross_GraphicsShaderResourceInfo fri = {0};
    fri.num_samplers = 1;
    SDL_GPUShader *fs = SDL_ShaderCross_CompileGraphicsShaderFromSPIRV(g->device, &fi, &fri, 0);
    if (!vs || !fs) {
        if (vs) SDL_ReleaseGPUShader(g->device, vs);
        if (fs) SDL_ReleaseGPUShader(g->device, fs);
        return -1;
    }
    SDL_GPUVertexBufferDescription vbd = {0};
    vbd.slot = 0;
    vbd.pitch = 4 * sizeof(float);
    vbd.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    SDL_GPUVertexAttribute attrs[2] = {
        {0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, 0},
        {1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, 2 * sizeof(float)},
    };
    SDL_GPUVertexInputState vis = {0};
    vis.vertex_buffer_descriptions = &vbd;
    vis.num_vertex_buffers = 1;
    vis.vertex_attributes = attrs;
    vis.num_vertex_attributes = 2;
    SDL_GPURasterizerState rs = {0};
    rs.fill_mode = SDL_GPU_FILLMODE_FILL;
    rs.cull_mode = SDL_GPU_CULLMODE_NONE;
    rs.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    SDL_GPUDepthStencilState ds = {0};
    SDL_GPUColorTargetDescription ctd = {0};
    ctd.format = SDL_GetGPUSwapchainTextureFormat(g->device, g->window);
    ctd.blend_state.enable_blend = true;
    ctd.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
    ctd.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    ctd.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
    ctd.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
    ctd.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    ctd.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
    SDL_GPUGraphicsPipelineTargetInfo ti = {0};
    ti.color_target_descriptions = &ctd;
    ti.num_color_targets = 1;
    SDL_GPUGraphicsPipelineCreateInfo pci = {0};
    pci.vertex_shader = vs;
    pci.fragment_shader = fs;
    pci.vertex_input_state = vis;
    pci.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pci.rasterizer_state = rs;
    pci.depth_stencil_state = ds;
    pci.target_info = ti;
    g->hud_pipeline = SDL_CreateGPUGraphicsPipeline(g->device, &pci);
    SDL_ReleaseGPUShader(g->device, vs);
    SDL_ReleaseGPUShader(g->device, fs);
    if (!g->hud_pipeline) return -2;

    static const float quad[] = {
        -1.0f, -1.0f, 0.0f, 1.0f,
         1.0f, -1.0f, 1.0f, 1.0f,
         1.0f,  1.0f, 1.0f, 0.0f,
        -1.0f, -1.0f, 0.0f, 1.0f,
         1.0f,  1.0f, 1.0f, 0.0f,
        -1.0f,  1.0f, 0.0f, 0.0f,
    };
    SDL_GPUBufferCreateInfo bi = {0};
    bi.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
    bi.size = sizeof quad;
    g->hud_vb = SDL_CreateGPUBuffer(g->device, &bi);
    if (!g->hud_vb) return -3;
    SDL_GPUTransferBufferCreateInfo ti2 = {0};
    ti2.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    ti2.size = sizeof quad;
    SDL_GPUTransferBuffer *tb = SDL_CreateGPUTransferBuffer(g->device, &ti2);
    if (!tb) return -4;
    void *map = SDL_MapGPUTransferBuffer(g->device, tb, false);
    memcpy(map, quad, sizeof quad);
    SDL_UnmapGPUTransferBuffer(g->device, tb);
    SDL_GPUCommandBuffer *cb = SDL_AcquireGPUCommandBuffer(g->device);
    SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass(cb);
    SDL_GPUTransferBufferLocation src = { tb, 0 };
    SDL_GPUBufferRegion dst = { g->hud_vb, 0, sizeof quad };
    SDL_UploadToGPUBuffer(cp, &src, &dst, true);
    SDL_EndGPUCopyPass(cp);
    SDL_SubmitGPUCommandBuffer(cb);
    SDL_ReleaseGPUTransferBuffer(g->device, tb);
    SDL_WaitForGPUIdle(g->device);

    SDL_GPUSamplerCreateInfo si = {0};
    si.min_filter = SDL_GPU_FILTER_LINEAR;
    si.mag_filter = SDL_GPU_FILTER_LINEAR;
    si.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    si.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    si.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    si.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    g->hud_sampler = SDL_CreateGPUSampler(g->device, &si);
    return g->hud_sampler ? 0 : -5;
}

void *gfxgpu_open(void *handle) {
    SDL_Window *window = (SDL_Window *)gfx_native_window(handle);
    if (!window) return NULL;
    if (!SDL_ShaderCross_Init()) {
        fprintf(stderr, "gfxgpu: shadercross init: %s\n", SDL_GetError());
        return NULL;
    }
    SDL_GPUShaderFormat formats = SDL_ShaderCross_GetSPIRVShaderFormats();
    if (formats == 0) {
        fprintf(stderr, "gfxgpu: no SPIR-V-capable GPU backend\n");
        return NULL;
    }
    SDL_GPUDevice *device = SDL_CreateGPUDevice(formats, true, NULL);
    if (!device) {
        fprintf(stderr, "gfxgpu: SDL_CreateGPUDevice: %s\n", SDL_GetError());
        return NULL;
    }
    if (!SDL_ClaimWindowForGPUDevice(device, window)) {
        fprintf(stderr, "gfxgpu: claim window: %s\n", SDL_GetError());
        SDL_DestroyGPUDevice(device);
        return NULL;
    }
    Gpu *g = (Gpu *)calloc(1, sizeof(*g));
    if (!g) {
        SDL_ReleaseWindowFromGPUDevice(device, window);
        SDL_DestroyGPUDevice(device);
        return NULL;
    }
    g->device = device;
    g->window = window;
    int ww = 0, hh = 0;
    SDL_GetWindowSizeInPixels(window, &ww, &hh);
    g->w = (uint32_t)ww;
    g->h = (uint32_t)hh;

    SDL_ShaderCross_SPIRV_Info vi = {0};
    vi.bytecode = tri_vert_spv;
    vi.bytecode_size = tri_vert_spv_len;
    vi.entrypoint = "main";
    vi.shader_stage = SDL_SHADERCROSS_SHADERSTAGE_VERTEX;
    SDL_ShaderCross_GraphicsShaderResourceInfo vri = {0};
    vri.num_uniform_buffers = 1;
    SDL_GPUShader *vs = SDL_ShaderCross_CompileGraphicsShaderFromSPIRV(device, &vi, &vri, 0);
    if (!vs) {
        fprintf(stderr, "gfxgpu: vertex shader: %s\n", SDL_GetError());
        goto fail;
    }
    SDL_ShaderCross_SPIRV_Info fi = {0};
    fi.bytecode = tri_frag_spv;
    fi.bytecode_size = tri_frag_spv_len;
    fi.entrypoint = "main";
    fi.shader_stage = SDL_SHADERCROSS_SHADERSTAGE_FRAGMENT;
    SDL_ShaderCross_GraphicsShaderResourceInfo fri = {0};
    SDL_GPUShader *fs = SDL_ShaderCross_CompileGraphicsShaderFromSPIRV(device, &fi, &fri, 0);
    if (!fs) {
        fprintf(stderr, "gfxgpu: fragment shader: %s\n", SDL_GetError());
        SDL_ReleaseGPUShader(device, vs);
        goto fail;
    }

    SDL_GPUVertexBufferDescription vbd = {0};
    vbd.slot = 0;
    vbd.pitch = 6 * sizeof(float);
    vbd.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    SDL_GPUVertexAttribute attrs[2] = {
        {0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, 0},
        {1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, 3 * sizeof(float)},
    };
    SDL_GPUVertexInputState vis = {0};
    vis.vertex_buffer_descriptions = &vbd;
    vis.num_vertex_buffers = 1;
    vis.vertex_attributes = attrs;
    vis.num_vertex_attributes = 2;

    SDL_GPURasterizerState rs = {0};
    rs.fill_mode = SDL_GPU_FILLMODE_FILL;
    rs.cull_mode = SDL_GPU_CULLMODE_NONE;
    rs.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    rs.enable_depth_clip = true;

    SDL_GPUDepthStencilState ds = {0};
    ds.compare_op = SDL_GPU_COMPAREOP_LESS;
    ds.enable_depth_test = true;
    ds.enable_depth_write = true;

    SDL_GPUColorTargetDescription ctd = {0};
    ctd.format = SDL_GetGPUSwapchainTextureFormat(device, window);
    SDL_GPUGraphicsPipelineTargetInfo ti = {0};
    ti.color_target_descriptions = &ctd;
    ti.num_color_targets = 1;
    ti.depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
    ti.has_depth_stencil_target = true;

    SDL_GPUGraphicsPipelineCreateInfo pci = {0};
    pci.vertex_shader = vs;
    pci.fragment_shader = fs;
    pci.vertex_input_state = vis;
    pci.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pci.rasterizer_state = rs;
    pci.depth_stencil_state = ds;
    pci.target_info = ti;
    g->pipeline = SDL_CreateGPUGraphicsPipeline(device, &pci);
    SDL_ReleaseGPUShader(device, vs);
    SDL_ReleaseGPUShader(device, fs);
    if (!g->pipeline) {
        fprintf(stderr, "gfxgpu: pipeline: %s\n", SDL_GetError());
        goto fail;
    }

    SDL_GPUTextureCreateInfo dci = {0};
    dci.type = SDL_GPU_TEXTURETYPE_2D;
    dci.format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
    dci.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
    dci.width = g->w;
    dci.height = g->h;
    dci.layer_count_or_depth = 1;
    dci.num_levels = 1;
    dci.sample_count = SDL_GPU_SAMPLECOUNT_1;
    g->depth = SDL_CreateGPUTexture(device, &dci);
    if (!g->depth) {
        fprintf(stderr, "gfxgpu: depth texture: %s\n", SDL_GetError());
        goto fail;
    }
    if (gpu_upload_vertices(g) != 0) goto fail;
    if (gpu_init_hud(g) != 0) goto fail;
    fprintf(stderr, "gfxgpu: driver=%s\n", SDL_GetGPUDeviceDriver(device));
    return g;

fail:
    if (g->pipeline) SDL_ReleaseGPUGraphicsPipeline(device, g->pipeline);
    if (g->vertices) SDL_ReleaseGPUBuffer(device, g->vertices);
    if (g->depth) SDL_ReleaseGPUTexture(device, g->depth);
    SDL_ReleaseWindowFromGPUDevice(device, window);
    SDL_DestroyGPUDevice(device);
    free(g);
    return NULL;
}

int64_t gfxgpu_frame(void *handle, double angle) {
    Gpu *g = (Gpu *)handle;
    if (!g) return -1;
    SDL_GPUCommandBuffer *cb = SDL_AcquireGPUCommandBuffer(g->device);
    if (!cb) return -2;
    SDL_GPUTexture *swap = NULL;
    uint32_t w = 0, h = 0;
    if (!SDL_AcquireGPUSwapchainTexture(cb, g->window, &swap, &w, &h)) {
        SDL_SubmitGPUCommandBuffer(cb);
        return -3;
    }
    if (!swap) {
        SDL_SubmitGPUCommandBuffer(cb);
        return 0;
    }
    if (w != g->w || h != g->h) {
        SDL_WaitForGPUIdle(g->device);
        SDL_GPUTextureCreateInfo dci = {0};
        dci.type = SDL_GPU_TEXTURETYPE_2D;
        dci.format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
        dci.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
        dci.width = w;
        dci.height = h;
        dci.layer_count_or_depth = 1;
        dci.num_levels = 1;
        dci.sample_count = SDL_GPU_SAMPLECOUNT_1;
        SDL_GPUTexture *nd = SDL_CreateGPUTexture(g->device, &dci);
        if (!nd) {
            SDL_SubmitGPUCommandBuffer(cb);
            return -4;
        }
        SDL_ReleaseGPUTexture(g->device, g->depth);
        g->depth = nd;
        g->w = w;
        g->h = h;
    }

    float c = cosf((float)angle);
    float s = sinf((float)angle);
    float mvp[16] = {
        0.5f * c, 0.0f, -0.5f * s, 0.0f,
        0.0f, 0.5f, 0.0f, 0.0f,
        0.5f * s, 0.0f, 0.5f * c, 0.0f,
        0.0f, 0.0f, 0.5f, 1.0f,
    };
    SDL_PushGPUVertexUniformData(cb, 0, mvp, (Uint32)sizeof mvp);

    SDL_GPUColorTargetInfo color = {0};
    color.texture = swap;
    color.clear_color = (SDL_FColor){0.03f, 0.05f, 0.09f, 1.0f};
    color.load_op = SDL_GPU_LOADOP_CLEAR;
    color.store_op = SDL_GPU_STOREOP_STORE;
    SDL_GPUDepthStencilTargetInfo depth = {0};
    depth.texture = g->depth;
    depth.clear_depth = 1.0f;
    depth.load_op = SDL_GPU_LOADOP_CLEAR;
    depth.store_op = SDL_GPU_STOREOP_DONT_CARE;
    depth.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
    depth.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
    SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(cb, &color, 1, &depth);
    SDL_BindGPUGraphicsPipeline(pass, g->pipeline);
    SDL_GPUBufferBinding vb = { g->vertices, 0 };
    SDL_BindGPUVertexBuffers(pass, 0, &vb, 1);
    SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
    SDL_EndGPURenderPass(pass);
    if (!SDL_SubmitGPUCommandBuffer(cb)) return -5;
    return 0;
}

static int gpu_resize_depth(Gpu *g, uint32_t w, uint32_t h) {
    if (w == g->w && h == g->h && g->depth) return 0;
    SDL_GPUTextureCreateInfo dci = {0};
    dci.type = SDL_GPU_TEXTURETYPE_2D;
    dci.format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
    dci.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
    dci.width = w; dci.height = h;
    dci.layer_count_or_depth = 1;
    dci.num_levels = 1;
    dci.sample_count = SDL_GPU_SAMPLECOUNT_1;
    SDL_GPUTexture *nd = SDL_CreateGPUTexture(g->device, &dci);
    if (!nd) return -1;
    if (g->depth) SDL_ReleaseGPUTexture(g->device, g->depth);
    g->depth = nd;
    g->w = w; g->h = h;
    return 0;
}

int64_t gfxgpu_frame_scene(void *handle, const double *verts, int64_t nfloats,
                           const double *mvp, int64_t nmvp,
                           const unsigned char *overlay, int64_t overlay_len,
                           int64_t w, int64_t h) {
    Gpu *g = (Gpu *)handle;
    if (!g || g->offscreen || !g->pipeline || !g->hud_pipeline) return -1;
    if (nfloats < 0 || nfloats % 6 != 0) return -1;
    if (gpu_upload_mesh(g, verts, nfloats) != 0) return -2;
    SDL_GPUCommandBuffer *cb = SDL_AcquireGPUCommandBuffer(g->device);
    if (!cb) return -3;
    SDL_GPUTexture *swap = NULL;
    uint32_t sw = 0, sh = 0;
    if (!SDL_AcquireGPUSwapchainTexture(cb, g->window, &swap, &sw, &sh) || !swap) {
        SDL_SubmitGPUCommandBuffer(cb);
        return -4;
    }
    if (gpu_resize_depth(g, sw, sh) != 0) {
        SDL_SubmitGPUCommandBuffer(cb);
        return -5;
    }

    /* Upload the transparent HUD overlay at its native 480x270-ish size. */
    int have_hud = 0;
    SDL_GPUTransferBuffer *hud_tb = NULL;
    if (overlay && overlay_len >= w * h * 4) {
        if (!g->hud_tex || g->hud_tex == NULL) {
            SDL_GPUTextureCreateInfo hci = {0};
            hci.type = SDL_GPU_TEXTURETYPE_2D;
            hci.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
            hci.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
            hci.width = (uint32_t)w; hci.height = (uint32_t)h;
            hci.layer_count_or_depth = 1;
            hci.num_levels = 1;
            hci.sample_count = SDL_GPU_SAMPLECOUNT_1;
            g->hud_tex = SDL_CreateGPUTexture(g->device, &hci);
        }
        if (g->hud_tex) {
            SDL_GPUTransferBufferCreateInfo tci = {0};
            tci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
            tci.size = (uint32_t)(w * h * 4);
            hud_tb = SDL_CreateGPUTransferBuffer(g->device, &tci);
            if (hud_tb) {
                void *map = SDL_MapGPUTransferBuffer(g->device, hud_tb, false);
                if (map) {
                    memcpy(map, overlay, (size_t)(w * h * 4));
                    SDL_UnmapGPUTransferBuffer(g->device, hud_tb);
                    SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass(cb);
                    SDL_GPUTextureTransferInfo src = { hud_tb, 0, (Uint32)w, (Uint32)h };
                    SDL_GPUTextureRegion dst = { g->hud_tex, 0, 0, 0, 0, 0, (Uint32)w, (Uint32)h, 1 };
                    SDL_UploadToGPUTexture(cp, &src, &dst, true);
                    SDL_EndGPUCopyPass(cp);
                    have_hud = 1;
                }
            }
        }
    }

    float m[16] = {0};
    if (mvp && nmvp >= 16) {
        for (int i = 0; i < 16; i++) m[i] = (float)mvp[i];
    } else { m[0] = m[5] = m[10] = m[15] = 1.0f; }
    SDL_PushGPUVertexUniformData(cb, 0, m, sizeof m);

    SDL_GPUColorTargetInfo color = {0};
    color.texture = swap;
    color.clear_color = (SDL_FColor){0.02f, 0.04f, 0.07f, 1.0f};
    color.load_op = SDL_GPU_LOADOP_CLEAR;
    color.store_op = SDL_GPU_STOREOP_STORE;
    SDL_GPUDepthStencilTargetInfo depth = {0};
    depth.texture = g->depth;
    depth.clear_depth = 1.0f;
    depth.load_op = SDL_GPU_LOADOP_CLEAR;
    depth.store_op = SDL_GPU_STOREOP_DONT_CARE;
    depth.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
    depth.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
    SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(cb, &color, 1, &depth);
    if (nfloats > 0) {
        SDL_BindGPUGraphicsPipeline(pass, g->pipeline);
        SDL_GPUBufferBinding vb = { g->mesh, 0 };
        SDL_BindGPUVertexBuffers(pass, 0, &vb, 1);
        SDL_DrawGPUPrimitives(pass, (Uint32)(nfloats / 6), 1, 0, 0);
    }
    SDL_EndGPURenderPass(pass);

    if (have_hud && g->hud_tex) {
        SDL_GPUColorTargetInfo hc = {0};
        hc.texture = swap;
        hc.load_op = SDL_GPU_LOADOP_LOAD;
        hc.store_op = SDL_GPU_STOREOP_STORE;
        SDL_GPURenderPass *hp = SDL_BeginGPURenderPass(cb, &hc, 1, NULL);
        SDL_BindGPUGraphicsPipeline(hp, g->hud_pipeline);
        SDL_GPUBufferBinding hb = { g->hud_vb, 0 };
        SDL_BindGPUVertexBuffers(hp, 0, &hb, 1);
        SDL_GPUTextureSamplerBinding sb = { g->hud_tex, g->hud_sampler };
        SDL_BindGPUFragmentSamplers(hp, 0, &sb, 1);
        SDL_DrawGPUPrimitives(hp, 6, 1, 0, 0);
        SDL_EndGPURenderPass(hp);
    }
    if (!SDL_SubmitGPUCommandBuffer(cb)) {
        if (hud_tb) SDL_ReleaseGPUTransferBuffer(g->device, hud_tb);
        return -6;
    }
    if (hud_tb) SDL_ReleaseGPUTransferBuffer(g->device, hud_tb);
    return 0;
}

int64_t gfxgpu_close(void *handle) {
    Gpu *g = (Gpu *)handle;
    if (!g) return 0;
    SDL_WaitForGPUIdle(g->device);
    if (g->pipeline) SDL_ReleaseGPUGraphicsPipeline(g->device, g->pipeline);
    if (g->vertices) SDL_ReleaseGPUBuffer(g->device, g->vertices);
    if (g->mesh) SDL_ReleaseGPUBuffer(g->device, g->mesh);
    if (g->hud_pipeline) SDL_ReleaseGPUGraphicsPipeline(g->device, g->hud_pipeline);
    if (g->hud_vb) SDL_ReleaseGPUBuffer(g->device, g->hud_vb);
    if (g->hud_tex) SDL_ReleaseGPUTexture(g->device, g->hud_tex);
    if (g->hud_sampler) SDL_ReleaseGPUSampler(g->device, g->hud_sampler);
    if (g->depth) SDL_ReleaseGPUTexture(g->device, g->depth);
    if (g->color) SDL_ReleaseGPUTexture(g->device, g->color);
    SDL_ReleaseWindowFromGPUDevice(g->device, g->window);
    SDL_DestroyGPUDevice(g->device);
    SDL_ShaderCross_Quit();
    free(g);
    return 0;
}


/* ---------------------------------------------------------------- offscreen
   The game backend renders into a GPU color/depth target and downloads the
   pixels into the existing core:gfx framebuffer. HUD, input and audio remain
   in core:gfx; this is the first mesh backend, not yet a swapchain HUD path. */

void *gfxgpu_open_offscreen(int64_t w, int64_t h) {
    if (w <= 0 || h <= 0 || w > 16384 || h > 16384) return NULL;
    if (!SDL_ShaderCross_Init()) {
        fprintf(stderr, "gfxgpu: shadercross init: %s\n", SDL_GetError());
        return NULL;
    }
    SDL_GPUShaderFormat formats = SDL_ShaderCross_GetSPIRVShaderFormats();
    SDL_GPUDevice *device = SDL_CreateGPUDevice(formats, true, NULL);
    if (!device) {
        fprintf(stderr, "gfxgpu: offscreen device: %s\n", SDL_GetError());
        return NULL;
    }
    Gpu *g = (Gpu *)calloc(1, sizeof(*g));
    if (!g) { SDL_DestroyGPUDevice(device); return NULL; }
    g->device = device;
    g->offscreen = 1;
    g->w = (uint32_t)w;
    g->h = (uint32_t)h;

    SDL_ShaderCross_SPIRV_Info vi = {0};
    vi.bytecode = tri_vert_spv;
    vi.bytecode_size = tri_vert_spv_len;
    vi.entrypoint = "main";
    vi.shader_stage = SDL_SHADERCROSS_SHADERSTAGE_VERTEX;
    SDL_ShaderCross_GraphicsShaderResourceInfo vri = {0};
    vri.num_uniform_buffers = 1;
    SDL_GPUShader *vs = SDL_ShaderCross_CompileGraphicsShaderFromSPIRV(device, &vi, &vri, 0);
    SDL_ShaderCross_SPIRV_Info fi = {0};
    fi.bytecode = tri_frag_spv;
    fi.bytecode_size = tri_frag_spv_len;
    fi.entrypoint = "main";
    fi.shader_stage = SDL_SHADERCROSS_SHADERSTAGE_FRAGMENT;
    SDL_ShaderCross_GraphicsShaderResourceInfo fri = {0};
    SDL_GPUShader *fs = SDL_ShaderCross_CompileGraphicsShaderFromSPIRV(device, &fi, &fri, 0);
    if (!vs || !fs) {
        fprintf(stderr, "gfxgpu: offscreen shaders: %s\n", SDL_GetError());
        if (vs) SDL_ReleaseGPUShader(device, vs);
        if (fs) SDL_ReleaseGPUShader(device, fs);
        goto off_fail;
    }

    SDL_GPUVertexBufferDescription vbd = {0};
    vbd.slot = 0;
    vbd.pitch = 6 * sizeof(float);
    vbd.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    SDL_GPUVertexAttribute attrs[2] = {
        {0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, 0},
        {1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, 3 * sizeof(float)},
    };
    SDL_GPUVertexInputState vis = {0};
    vis.vertex_buffer_descriptions = &vbd;
    vis.num_vertex_buffers = 1;
    vis.vertex_attributes = attrs;
    vis.num_vertex_attributes = 2;
    SDL_GPURasterizerState rs = {0};
    rs.fill_mode = SDL_GPU_FILLMODE_FILL;
    rs.cull_mode = SDL_GPU_CULLMODE_NONE;
    rs.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    rs.enable_depth_clip = true;
    SDL_GPUDepthStencilState ds = {0};
    ds.compare_op = SDL_GPU_COMPAREOP_LESS;
    ds.enable_depth_test = true;
    ds.enable_depth_write = true;
    SDL_GPUColorTargetDescription ctd = {0};
    ctd.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    SDL_GPUGraphicsPipelineTargetInfo ti = {0};
    ti.color_target_descriptions = &ctd;
    ti.num_color_targets = 1;
    ti.depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
    ti.has_depth_stencil_target = true;
    SDL_GPUGraphicsPipelineCreateInfo pci = {0};
    pci.vertex_shader = vs;
    pci.fragment_shader = fs;
    pci.vertex_input_state = vis;
    pci.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pci.rasterizer_state = rs;
    pci.depth_stencil_state = ds;
    pci.target_info = ti;
    g->pipeline = SDL_CreateGPUGraphicsPipeline(device, &pci);
    SDL_ReleaseGPUShader(device, vs);
    SDL_ReleaseGPUShader(device, fs);
    if (!g->pipeline) {
        fprintf(stderr, "gfxgpu: offscreen pipeline: %s\n", SDL_GetError());
        goto off_fail;
    }

    SDL_GPUTextureCreateInfo tci = {0};
    tci.type = SDL_GPU_TEXTURETYPE_2D;
    tci.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    tci.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
    tci.width = g->w;
    tci.height = g->h;
    tci.layer_count_or_depth = 1;
    tci.num_levels = 1;
    tci.sample_count = SDL_GPU_SAMPLECOUNT_1;
    g->color = SDL_CreateGPUTexture(device, &tci);
    if (!g->color) goto off_fail;
    SDL_GPUTextureCreateInfo dci = tci;
    dci.format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
    dci.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
    g->depth = SDL_CreateGPUTexture(device, &dci);
    if (!g->depth) goto off_fail;
    fprintf(stderr, "gfxgpu: offscreen driver=%s\n", SDL_GetGPUDeviceDriver(device));
    return g;

off_fail:
    if (g->pipeline) SDL_ReleaseGPUGraphicsPipeline(device, g->pipeline);
    if (g->color) SDL_ReleaseGPUTexture(device, g->color);
    if (g->depth) SDL_ReleaseGPUTexture(device, g->depth);
    SDL_DestroyGPUDevice(device);
    free(g);
    return NULL;
}

static int gpu_upload_mesh(Gpu *g, const double *verts, int64_t nfloats) {
    uint32_t bytes = (uint32_t)(nfloats * sizeof(float));
    if (bytes > g->mesh_cap) {
        if (g->mesh) SDL_ReleaseGPUBuffer(g->device, g->mesh);
        SDL_GPUBufferCreateInfo bi = {0};
        bi.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
        bi.size = bytes;
        g->mesh = SDL_CreateGPUBuffer(g->device, &bi);
        if (!g->mesh) { g->mesh_cap = 0; return -1; }
        g->mesh_cap = bytes;
    }
    if (bytes == 0) return 0;
    float *fp = (float *)malloc((size_t)nfloats * sizeof(float));
    if (!fp) return -2;
    for (int64_t i = 0; i < nfloats; i++) fp[i] = (float)verts[i];
    SDL_GPUTransferBufferCreateInfo ti = {0};
    ti.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    ti.size = bytes;
    SDL_GPUTransferBuffer *tb = SDL_CreateGPUTransferBuffer(g->device, &ti);
    if (!tb) return -2;
    void *map = SDL_MapGPUTransferBuffer(g->device, tb, false);
    if (!map) { SDL_ReleaseGPUTransferBuffer(g->device, tb); return -3; }
    memcpy(map, fp, bytes);
    free(fp);
    SDL_UnmapGPUTransferBuffer(g->device, tb);
    SDL_GPUCommandBuffer *cb = SDL_AcquireGPUCommandBuffer(g->device);
    if (!cb) { SDL_ReleaseGPUTransferBuffer(g->device, tb); return -4; }
    SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass(cb);
    SDL_GPUTransferBufferLocation src = { tb, 0 };
    SDL_GPUBufferRegion dst = { g->mesh, 0, bytes };
    SDL_UploadToGPUBuffer(cp, &src, &dst, true);
    SDL_EndGPUCopyPass(cp);
    if (!SDL_SubmitGPUCommandBuffer(cb)) {
        SDL_ReleaseGPUTransferBuffer(g->device, tb);
        return -5;
    }
    SDL_ReleaseGPUTransferBuffer(g->device, tb);
    return 0;
}

void gfxgpu_render_mesh(void *handle, const double *verts, int64_t nfloats,
                         const double *mvp, int64_t nmvp,
                         unsigned char **out, int64_t *outlen) {
    *out = NULL;
    *outlen = 0;
    Gpu *g = (Gpu *)handle;
    if (!g || !g->offscreen) return;
    if (nfloats < 0 || nfloats % 6 != 0) return;
    if (gpu_upload_mesh(g, verts, nfloats) != 0) return;
    SDL_GPUCommandBuffer *cb = SDL_AcquireGPUCommandBuffer(g->device);
    if (!cb) return;
    SDL_GPUColorTargetInfo color = {0};
    color.texture = g->color;
    color.clear_color = (SDL_FColor){0.02f, 0.04f, 0.07f, 1.0f};
    color.load_op = SDL_GPU_LOADOP_CLEAR;
    color.store_op = SDL_GPU_STOREOP_STORE;
    SDL_GPUDepthStencilTargetInfo depth = {0};
    depth.texture = g->depth;
    depth.clear_depth = 1.0f;
    depth.load_op = SDL_GPU_LOADOP_CLEAR;
    depth.store_op = SDL_GPU_STOREOP_DONT_CARE;
    depth.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
    depth.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
    SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(cb, &color, 1, &depth);
    if (!pass) { SDL_SubmitGPUCommandBuffer(cb); return; }
    if (nfloats > 0) {
        float m[16] = {0};
        if (mvp && nmvp >= 16) {
            for (int i = 0; i < 16; i++) m[i] = (float)mvp[i];
        } else {
            m[0] = 1.0f; m[5] = 1.0f; m[10] = 1.0f; m[15] = 1.0f;
        }
        SDL_PushGPUVertexUniformData(cb, 0, m, sizeof m);
        SDL_BindGPUGraphicsPipeline(pass, g->pipeline);
        SDL_GPUBufferBinding vb = { g->mesh, 0 };
        SDL_BindGPUVertexBuffers(pass, 0, &vb, 1);
        SDL_DrawGPUPrimitives(pass, (Uint32)(nfloats / 6), 1, 0, 0);
    }
    SDL_EndGPURenderPass(pass);

    uint32_t bytes = g->w * g->h * 4;
    SDL_GPUTransferBufferCreateInfo ti = {0};
    ti.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
    ti.size = bytes;
    SDL_GPUTransferBuffer *tb = SDL_CreateGPUTransferBuffer(g->device, &ti);
    if (!tb) { SDL_SubmitGPUCommandBuffer(cb); return; }
    SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass(cb);
    SDL_GPUTextureRegion src = { g->color, 0, 0, 0, 0, 0, g->w, g->h, 1 };
    SDL_GPUTextureTransferInfo dst = { tb, 0, g->w, g->h };
    SDL_DownloadFromGPUTexture(cp, &src, &dst);
    SDL_EndGPUCopyPass(cp);
    if (!SDL_SubmitGPUCommandBuffer(cb)) {
        SDL_ReleaseGPUTransferBuffer(g->device, tb);
        return;
    }
    SDL_WaitForGPUIdle(g->device);
    const uint8_t *rgba = (const uint8_t *)SDL_MapGPUTransferBuffer(g->device, tb, false);
    if (!rgba) { SDL_ReleaseGPUTransferBuffer(g->device, tb); return; }
    uint8_t *copy = (uint8_t *)malloc(bytes);
    if (copy) {
        memcpy(copy, rgba, bytes);
        *out = copy;
        *outlen = bytes;
    }
    SDL_UnmapGPUTransferBuffer(g->device, tb);
    SDL_ReleaseGPUTransferBuffer(g->device, tb);
}

int64_t gfxgpu_close_offscreen(void *handle) { return gfxgpu_close(handle); }
