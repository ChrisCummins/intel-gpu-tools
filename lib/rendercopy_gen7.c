#include "rendercopy.h"
#include "gen7_render.h"

#include <assert.h>
#include <stddef.h>

#define ALIGN(x, y) (((x) + (y)-1) & ~((y)-1))

/* Assembled from ../shaders/ps/blit.g7a */
static const uint32_t ps_kernel[][4] = {
	{ 0x0080005a, 0x214077bd, 0x000000c0, 0x008d0040 },
	{ 0x0080005a, 0x218077bd, 0x000000d0, 0x008d0040 },
	{ 0x02800031, 0x2e001e3d, 0x00000140, 0x08840001 },
	{ 0x05800031, 0x20001e3c, 0x00000e00, 0x90031000 },
};

static uint32_t
batch_used(struct intel_batchbuffer *batch)
{
	return batch->state - batch->buffer;
}

static uint32_t
batch_align(struct intel_batchbuffer *batch, uint32_t align)
{
	uint32_t offset = batch_used(batch);
	offset = ALIGN(offset, align);
	batch->state = batch->buffer + offset;
	return offset;
}

static void *
batch_alloc(struct intel_batchbuffer *batch, uint32_t size, uint32_t align)
{
	uint32_t offset = batch_align(batch, align);
	batch->state += size;
	return memset(batch->buffer + offset, 0, size);
}

static uint32_t
batch_offset(struct intel_batchbuffer *batch, void *ptr)
{
	return (uint8_t *)ptr - batch->buffer;
}

static uint32_t
batch_copy(struct intel_batchbuffer *batch, const void *ptr, uint32_t size, uint32_t align)
{
	return batch_offset(batch, memcpy(batch_alloc(batch, size, align), ptr, size));
}

static void
gen7_render_flush(struct intel_batchbuffer *batch, uint32_t batch_end)
{
	int ret;

	ret = drm_intel_bo_subdata(batch->bo, 0, 4096, batch->buffer);
	if (ret == 0)
		ret = drm_intel_bo_mrb_exec(batch->bo, batch_end,
					    NULL, 0, 0, 0);
	assert(ret == 0);
}

static uint32_t
gen7_tiling_bits(uint32_t tiling)
{
	switch (tiling) {
	default: assert(0);
	case I915_TILING_NONE: return 0;
	case I915_TILING_X: return GEN7_SURFACE_TILED;
	case I915_TILING_Y: return GEN7_SURFACE_TILED | GEN7_SURFACE_TILED_Y;
	}
}

static uint32_t
gen7_bind_buf(struct intel_batchbuffer *batch,
	      struct scratch_buf *buf,
	      uint32_t format,
	      int is_dst)
{
	uint32_t *ss;
	uint32_t write_domain, read_domain;
	int ret;

	if (is_dst) {
		write_domain = read_domain = I915_GEM_DOMAIN_RENDER;
	} else {
		write_domain = 0;
		read_domain = I915_GEM_DOMAIN_SAMPLER;
	}

	ss = batch_alloc(batch, sizeof(*ss), 32);

	ss[0] = (GEN7_SURFACE_2D << GEN7_SURFACE_TYPE_SHIFT |
		 gen7_tiling_bits(buf->tiling) |
		format << GEN7_SURFACE_FORMAT_SHIFT);
	ss[1] = buf->bo->offset;
	ss[2] = ((buf_width(buf) - 1)  << GEN7_SURFACE_WIDTH_SHIFT |
		 (buf_height(buf) - 1) << GEN7_SURFACE_HEIGHT_SHIFT);
	ss[3] = (buf->stride - 1) << GEN7_SURFACE_PITCH_SHIFT;
	ss[4] = 0;
	ss[5] = 0;
	ss[6] = 0;
	ss[7] = 0;
	if (IS_HASWELL(batch->devid))
		ss[7] |= HSW_SURFACE_SWIZZLE(RED, GREEN, BLUE, ALPHA);

	ret = drm_intel_bo_emit_reloc(batch->bo,
				      batch_offset(batch, ss) + 4,
				      buf->bo, 0,
				      read_domain, write_domain);
	assert(ret == 0);

	return batch_offset(batch, ss);
}

struct vertex {
	uint16_t x;
	uint16_t y;
	uint16_t s;
	uint16_t t;
};

static void
gen7_emit_vertex_elements(struct intel_batchbuffer *batch)
{
	OUT_BATCH(GEN7_3DSTATE_VERTEX_ELEMENTS |
		  ((2 * (1 + 2)) + 1 - 2));

	OUT_BATCH(0 << GEN7_VE0_VERTEX_BUFFER_INDEX_SHIFT | GEN7_VE0_VALID |
		  GEN7_SURFACEFORMAT_R32G32B32A32_FLOAT << GEN7_VE0_FORMAT_SHIFT |
		  0 << GEN7_VE0_OFFSET_SHIFT);

	OUT_BATCH(GEN7_VFCOMPONENT_STORE_0 << GEN7_VE1_VFCOMPONENT_0_SHIFT |
		  GEN7_VFCOMPONENT_STORE_0 << GEN7_VE1_VFCOMPONENT_1_SHIFT |
		  GEN7_VFCOMPONENT_STORE_0 << GEN7_VE1_VFCOMPONENT_2_SHIFT |
		  GEN7_VFCOMPONENT_STORE_0 << GEN7_VE1_VFCOMPONENT_3_SHIFT);

	/* x,y */
	OUT_BATCH(0 << GEN7_VE0_VERTEX_BUFFER_INDEX_SHIFT | GEN7_VE0_VALID |
		  GEN7_SURFACEFORMAT_R16G16_SSCALED << GEN7_VE0_FORMAT_SHIFT |
		  offsetof(struct vertex, x) << GEN7_VE0_OFFSET_SHIFT);
	OUT_BATCH(GEN7_VFCOMPONENT_STORE_SRC << GEN7_VE1_VFCOMPONENT_0_SHIFT |
		  GEN7_VFCOMPONENT_STORE_SRC << GEN7_VE1_VFCOMPONENT_1_SHIFT |
		  GEN7_VFCOMPONENT_STORE_0 << GEN7_VE1_VFCOMPONENT_2_SHIFT |
		  GEN7_VFCOMPONENT_STORE_1_FLT << GEN7_VE1_VFCOMPONENT_3_SHIFT);

	/* s,t */
	OUT_BATCH(0 << GEN7_VE0_VERTEX_BUFFER_INDEX_SHIFT | GEN7_VE0_VALID |
		  GEN7_SURFACEFORMAT_R16G16_SSCALED << GEN7_VE0_FORMAT_SHIFT |
		  offsetof(struct vertex, s) << GEN7_VE0_OFFSET_SHIFT);
	OUT_BATCH(GEN7_VFCOMPONENT_STORE_SRC << GEN7_VE1_VFCOMPONENT_0_SHIFT |
		  GEN7_VFCOMPONENT_STORE_SRC << GEN7_VE1_VFCOMPONENT_1_SHIFT |
		  GEN7_VFCOMPONENT_STORE_0 << GEN7_VE1_VFCOMPONENT_2_SHIFT |
		  GEN7_VFCOMPONENT_STORE_1_FLT << GEN7_VE1_VFCOMPONENT_3_SHIFT);
}

static uint32_t
gen7_create_vertex_buffer(struct intel_batchbuffer *batch,
			  uint32_t src_x, uint32_t src_y,
			  uint32_t dst_x, uint32_t dst_y,
			  uint32_t width, uint32_t height)
{
	struct vertex *v;

	v = batch_alloc(batch, 3 * sizeof(struct vertex), 8);

	v[0] = (struct vertex) {
		.x = dst_x + width,
		.y = dst_y + height,
		.s = src_x + width,
		.t = src_y + height
	};

	v[1] = (struct vertex) {
		.x = dst_x,
		.y = dst_y + height,
		.s = src_x,
		.t = src_y + height
	};

	v[2] = (struct vertex) {
		.x = dst_x,
		.y = dst_y,
		.s = src_x,
		.t = src_y
	};

	return batch_offset(batch, v);
}

static void gen7_emit_vertex_buffer(struct intel_batchbuffer *batch,
				    int src_x, int src_y,
				    int dst_x, int dst_y,
				    int width, int height)
{
	uint32_t offset;

	offset = gen7_create_vertex_buffer(batch,
					   src_x, src_y,
					   dst_x, dst_y,
					   width, height);

	OUT_BATCH(GEN7_3DSTATE_VERTEX_BUFFERS | (5 - 2));
	OUT_BATCH(0 << GEN7_VB0_BUFFER_INDEX_SHIFT |
		  GEN7_VB0_VERTEXDATA |
		  GEN7_VB0_ADDRESS_MODIFY_ENABLE |
		  sizeof(struct vertex) << GEN7_VB0_BUFFER_PITCH_SHIFT);

	OUT_RELOC(batch->bo, I915_GEM_DOMAIN_VERTEX, 0, offset);
	OUT_BATCH(~0);
	OUT_BATCH(0);
}

static uint32_t
gen7_bind_surfaces(struct intel_batchbuffer *batch,
		   struct scratch_buf *src,
		   struct scratch_buf *dst)
{
	uint32_t *binding_table;

	binding_table = batch_alloc(batch, 8, 32);

	binding_table[0] =
		gen7_bind_buf(batch, dst, GEN7_SURFACEFORMAT_B8G8R8A8_UNORM, 1);
	binding_table[1] =
		gen7_bind_buf(batch, src, GEN7_SURFACEFORMAT_B8G8R8A8_UNORM, 0);

	return batch_offset(batch, binding_table);
}

static void
gen7_emit_binding_table(struct intel_batchbuffer *batch,
			struct scratch_buf *src,
			struct scratch_buf *dst)
{
	OUT_BATCH(GEN7_3DSTATE_BINDING_TABLE_POINTERS_PS | (2 - 2));
	OUT_BATCH(gen7_bind_surfaces(batch, src, dst));
}

static void
gen7_emit_drawing_rectangle(struct intel_batchbuffer *batch, struct scratch_buf *dst)
{
	OUT_BATCH(GEN7_3DSTATE_DRAWING_RECTANGLE | (4 - 2));
	OUT_BATCH(0);
	OUT_BATCH((buf_height(dst) - 1) << 16 | (buf_width(dst) - 1));
	OUT_BATCH(0);
}

static uint32_t
gen7_create_blend_state(struct intel_batchbuffer *batch)
{
	struct gen7_blend_state *blend;

	blend = batch_alloc(batch, sizeof(*blend), 64);

	blend->blend0.dest_blend_factor = GEN7_BLENDFACTOR_ZERO;
	blend->blend0.source_blend_factor = GEN7_BLENDFACTOR_ONE;
	blend->blend0.blend_func = GEN7_BLENDFUNCTION_ADD;
	blend->blend1.post_blend_clamp_enable = 1;
	blend->blend1.pre_blend_clamp_enable = 1;

	return batch_offset(batch, blend);
}

static void
gen7_emit_state_base_address(struct intel_batchbuffer *batch)
{
	OUT_BATCH(GEN7_STATE_BASE_ADDRESS | (10 - 2));
	OUT_BATCH(0);
	OUT_RELOC(batch->bo, I915_GEM_DOMAIN_INSTRUCTION, 0, BASE_ADDRESS_MODIFY);
	OUT_RELOC(batch->bo, I915_GEM_DOMAIN_INSTRUCTION, 0, BASE_ADDRESS_MODIFY);
	OUT_BATCH(0);
	OUT_RELOC(batch->bo, I915_GEM_DOMAIN_INSTRUCTION, 0, BASE_ADDRESS_MODIFY);

	OUT_BATCH(0);
	OUT_BATCH(0 | BASE_ADDRESS_MODIFY);
	OUT_BATCH(0);
	OUT_BATCH(0 | BASE_ADDRESS_MODIFY);
}

static uint32_t
gen7_create_cc_viewport(struct intel_batchbuffer *batch)
{
	struct gen7_cc_viewport *vp;

	vp = batch_alloc(batch, sizeof(*vp), 32);
	vp->min_depth = -1.e35;
	vp->max_depth = 1.e35;

	return batch_offset(batch, vp);
}

static void
gen7_emit_cc(struct intel_batchbuffer *batch)
{
        OUT_BATCH(GEN7_3DSTATE_BLEND_STATE_POINTERS | (2 - 2));
        OUT_BATCH(gen7_create_blend_state(batch));

        OUT_BATCH(GEN7_3DSTATE_VIEWPORT_STATE_POINTERS_CC | (2 - 2));
	OUT_BATCH(gen7_create_cc_viewport(batch));
}

static uint32_t
gen7_create_sampler(struct intel_batchbuffer *batch)
{
	struct gen7_sampler_state *ss;

	ss = batch_alloc(batch, sizeof(*ss), 32);

	ss->ss0.min_filter = GEN7_MAPFILTER_NEAREST;
	ss->ss0.mag_filter = GEN7_MAPFILTER_NEAREST;

	ss->ss3.r_wrap_mode = GEN7_TEXCOORDMODE_CLAMP;
	ss->ss3.s_wrap_mode = GEN7_TEXCOORDMODE_CLAMP;
	ss->ss3.t_wrap_mode = GEN7_TEXCOORDMODE_CLAMP;

	ss->ss3.non_normalized_coord = 1;

	return batch_offset(batch, ss);
}

static void
gen7_emit_sampler(struct intel_batchbuffer *batch)
{
        OUT_BATCH(GEN7_3DSTATE_SAMPLER_STATE_POINTERS_PS | (2 - 2));
        OUT_BATCH(gen7_create_sampler(batch));
}

static void
gen7_emit_multisample(struct intel_batchbuffer *batch)
{
	OUT_BATCH(GEN7_3DSTATE_MULTISAMPLE | (4 - 2));
	OUT_BATCH(GEN7_3DSTATE_MULTISAMPLE_PIXEL_LOCATION_CENTER |
		  GEN7_3DSTATE_MULTISAMPLE_NUMSAMPLES_1); /* 1 sample/pixel */
	OUT_BATCH(0);
	OUT_BATCH(0);

	OUT_BATCH(GEN7_3DSTATE_SAMPLE_MASK | (2 - 2));
	OUT_BATCH(1);
}

static void
gen7_emit_urb(struct intel_batchbuffer *batch)
{
	OUT_BATCH(GEN7_3DSTATE_PUSH_CONSTANT_ALLOC_PS | (2 - 2));
	OUT_BATCH(8); /* in 1KBs */

	/* num of VS entries must be divisible by 8 if size < 9 */
	OUT_BATCH(GEN7_3DSTATE_URB_VS | (2 - 2));
	OUT_BATCH((64 << GEN7_URB_ENTRY_NUMBER_SHIFT) |
		  (2 - 1) << GEN7_URB_ENTRY_SIZE_SHIFT |
		  (1 << GEN7_URB_STARTING_ADDRESS_SHIFT));

	OUT_BATCH(GEN7_3DSTATE_URB_HS | (2 - 2));
	OUT_BATCH((0 << GEN7_URB_ENTRY_SIZE_SHIFT) |
		  (2 << GEN7_URB_STARTING_ADDRESS_SHIFT));

	OUT_BATCH(GEN7_3DSTATE_URB_DS | (2 - 2));
	OUT_BATCH((0 << GEN7_URB_ENTRY_SIZE_SHIFT) |
		  (2 << GEN7_URB_STARTING_ADDRESS_SHIFT));

	OUT_BATCH(GEN7_3DSTATE_URB_GS | (2 - 2));
	OUT_BATCH((0 << GEN7_URB_ENTRY_SIZE_SHIFT) |
		  (1 << GEN7_URB_STARTING_ADDRESS_SHIFT));
}

static void
gen7_emit_vs(struct intel_batchbuffer *batch)
{
	OUT_BATCH(GEN7_3DSTATE_VS | (6 - 2));
	OUT_BATCH(0); /* no VS kernel */
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0); /* pass-through */
}

static void
gen7_emit_hs(struct intel_batchbuffer *batch)
{
        OUT_BATCH(GEN7_3DSTATE_HS | (7 - 2));
        OUT_BATCH(0); /* no HS kernel */
        OUT_BATCH(0);
        OUT_BATCH(0);
        OUT_BATCH(0);
        OUT_BATCH(0);
        OUT_BATCH(0); /* pass-through */
}

static void
gen7_emit_te(struct intel_batchbuffer *batch)
{
        OUT_BATCH(GEN7_3DSTATE_TE | (4 - 2));
        OUT_BATCH(0);
        OUT_BATCH(0);
        OUT_BATCH(0);
}

static void
gen7_emit_ds(struct intel_batchbuffer *batch)
{
        OUT_BATCH(GEN7_3DSTATE_DS | (6 - 2));
        OUT_BATCH(0);
        OUT_BATCH(0);
        OUT_BATCH(0);
        OUT_BATCH(0);
        OUT_BATCH(0);
}

static void
gen7_emit_gs(struct intel_batchbuffer *batch)
{
        OUT_BATCH(GEN7_3DSTATE_GS | (7 - 2));
        OUT_BATCH(0); /* no GS kernel */
        OUT_BATCH(0);
        OUT_BATCH(0);
        OUT_BATCH(0);
        OUT_BATCH(0);
        OUT_BATCH(0); /* pass-through  */
}

static void
gen7_emit_streamout(struct intel_batchbuffer *batch)
{
        OUT_BATCH(GEN7_3DSTATE_STREAMOUT | (3 - 2));
        OUT_BATCH(0);
        OUT_BATCH(0);
}

static void
gen7_emit_sf(struct intel_batchbuffer *batch)
{
        OUT_BATCH(GEN7_3DSTATE_SF | (7 - 2));
        OUT_BATCH(0);
        OUT_BATCH(GEN7_3DSTATE_SF_CULL_NONE);
        OUT_BATCH(2 << GEN7_3DSTATE_SF_TRIFAN_PROVOKE_SHIFT);
        OUT_BATCH(0);
        OUT_BATCH(0);
        OUT_BATCH(0);
}

static void
gen7_emit_sbe(struct intel_batchbuffer *batch)
{
	OUT_BATCH(GEN7_3DSTATE_SBE | (14 - 2));
	OUT_BATCH(1 << GEN7_SBE_NUM_OUTPUTS_SHIFT |
		  1 << GEN7_SBE_URB_ENTRY_READ_LENGTH_SHIFT |
		  1 << GEN7_SBE_URB_ENTRY_READ_OFFSET_SHIFT);
	OUT_BATCH(0);
	OUT_BATCH(0); /* dw4 */
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0); /* dw8 */
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0); /* dw12 */
	OUT_BATCH(0);
	OUT_BATCH(0);
}

static void
gen7_emit_ps(struct intel_batchbuffer *batch)
{
	int threads;

	if (IS_HASWELL(batch->devid))
		threads = 40 << HSW_PS_MAX_THREADS_SHIFT | 1 << HSW_PS_SAMPLE_MASK_SHIFT;
	else
		threads = 40 << IVB_PS_MAX_THREADS_SHIFT;

	OUT_BATCH(GEN7_3DSTATE_PS | (8 - 2));
	OUT_BATCH(batch_copy(batch, ps_kernel, sizeof(ps_kernel), 64));
	OUT_BATCH(1 << GEN7_PS_SAMPLER_COUNT_SHIFT |
		  2 << GEN7_PS_BINDING_TABLE_ENTRY_COUNT_SHIFT);
	OUT_BATCH(0); /* scratch address */
	OUT_BATCH(threads |
		  GEN7_PS_16_DISPATCH_ENABLE |
		  GEN7_PS_ATTRIBUTE_ENABLE);
	OUT_BATCH(6 << GEN7_PS_DISPATCH_START_GRF_SHIFT_0);
	OUT_BATCH(0);
	OUT_BATCH(0);
}

static void
gen7_emit_clip(struct intel_batchbuffer *batch)
{
        OUT_BATCH(GEN7_3DSTATE_CLIP | (4 - 2));
        OUT_BATCH(0);
        OUT_BATCH(0); /* pass-through */
        OUT_BATCH(0);

        OUT_BATCH(GEN7_3DSTATE_VIEWPORT_STATE_POINTERS_SF_CL | (2 - 2));
        OUT_BATCH(0);
}

static void
gen7_emit_wm(struct intel_batchbuffer *batch)
{
        OUT_BATCH(GEN7_3DSTATE_WM | (3 - 2));
        OUT_BATCH(GEN7_WM_DISPATCH_ENABLE |
                  GEN7_WM_PERSPECTIVE_PIXEL_BARYCENTRIC);
        OUT_BATCH(0);
}

static void
gen7_emit_null_depth_buffer(struct intel_batchbuffer *batch)
{
        OUT_BATCH(GEN7_3DSTATE_DEPTH_BUFFER | (7 - 2));
        OUT_BATCH(GEN7_SURFACE_NULL << GEN7_3DSTATE_DEPTH_BUFFER_TYPE_SHIFT |
                  GEN7_DEPTHFORMAT_D32_FLOAT << GEN7_3DSTATE_DEPTH_BUFFER_FORMAT_SHIFT);
        OUT_BATCH(0); /* disable depth, stencil and hiz */
        OUT_BATCH(0);
        OUT_BATCH(0);
        OUT_BATCH(0);
        OUT_BATCH(0);

        OUT_BATCH(GEN7_3DSTATE_CLEAR_PARAMS | (3 - 2));
        OUT_BATCH(0);
        OUT_BATCH(0);
}

#define BATCH_STATE_SPLIT 2048
void gen7_render_copyfunc(struct intel_batchbuffer *batch,
			  struct scratch_buf *src, unsigned src_x, unsigned src_y,
			  unsigned width, unsigned height,
			  struct scratch_buf *dst, unsigned dst_x, unsigned dst_y)
{
	uint32_t batch_end;

	intel_batchbuffer_flush(batch);

	batch->state = &batch->buffer[BATCH_STATE_SPLIT];

	OUT_BATCH(GEN7_PIPELINE_SELECT | PIPELINE_SELECT_3D);

	gen7_emit_state_base_address(batch);
	gen7_emit_multisample(batch);
	gen7_emit_urb(batch);
	gen7_emit_vs(batch);
	gen7_emit_hs(batch);
	gen7_emit_te(batch);
	gen7_emit_ds(batch);
	gen7_emit_gs(batch);
	gen7_emit_clip(batch);
	gen7_emit_sf(batch);
	gen7_emit_wm(batch);
	gen7_emit_streamout(batch);
	gen7_emit_null_depth_buffer(batch);

	gen7_emit_cc(batch);
        gen7_emit_sampler(batch);
        gen7_emit_sbe(batch);
        gen7_emit_ps(batch);
        gen7_emit_vertex_elements(batch);
        gen7_emit_vertex_buffer(batch,
				src_x, src_y, dst_x, dst_y, width, height);
	gen7_emit_binding_table(batch, src, dst);
	gen7_emit_drawing_rectangle(batch, dst);

        OUT_BATCH(GEN7_3DPRIMITIVE | (7- 2));
        OUT_BATCH(GEN7_3DPRIMITIVE_VERTEX_SEQUENTIAL | _3DPRIM_RECTLIST);
        OUT_BATCH(3);
        OUT_BATCH(0);
        OUT_BATCH(1);   /* single instance */
        OUT_BATCH(0);   /* start instance location */
        OUT_BATCH(0);   /* index buffer offset, ignored */

	OUT_BATCH(MI_BATCH_BUFFER_END);

	batch_end = batch->ptr - batch->buffer;
	batch_end = ALIGN(batch_end, 8);
	assert(batch_end < BATCH_STATE_SPLIT);

	gen7_render_flush(batch, batch_end);
	intel_batchbuffer_reset(batch);
}
