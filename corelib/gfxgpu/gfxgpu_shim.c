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
    SDL_Window *window;
    uint32_t w, h;
} Gpu;

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

int64_t gfxgpu_close(void *handle) {
    Gpu *g = (Gpu *)handle;
    if (!g) return 0;
    SDL_WaitForGPUIdle(g->device);
    if (g->pipeline) SDL_ReleaseGPUGraphicsPipeline(g->device, g->pipeline);
    if (g->vertices) SDL_ReleaseGPUBuffer(g->device, g->vertices);
    if (g->depth) SDL_ReleaseGPUTexture(g->device, g->depth);
    SDL_ReleaseWindowFromGPUDevice(g->device, g->window);
    SDL_DestroyGPUDevice(g->device);
    SDL_ShaderCross_Quit();
    free(g);
    return 0;
}
