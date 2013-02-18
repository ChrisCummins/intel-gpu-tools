#include "rendercopy.h"
#include "gen7_render.h"

#include <assert.h>

#define ALIGN(x, y) (((x) + (y)-1) & ~((y)-1))

/* This is our pixel shader. Here be dragons.
 *
 * ../shaders/ps/cec.g7a
 */
static const uint32_t ps_kernel[][4] = {
	/* Plane instruction - get X values.
	 * pln (16) g10	g6<0,1,0>F g2<8,8,1>F { align1 };       */
	{ 0x0080005a, 0x214077bd, 0x000000c0, 0x008d0040 },

	/* Plane instruction - get Y values.
	 * pln (16) g12	g6.16<0,1,0>F g2<8,8,1>F { align1 };    */
	{ 0x0080005a, 0x218077bd, 0x000000d0, 0x008d0040 },

	/* Sample instruction - get pixel data.
	 * send (16) g112 g10 0x2 0x8840001 { align1 };         */
	{ 0x02800031, 0x2e001e3d, 0x00000140, 0x08840001 },

	/* Move instructions - overwrite RGBA values with our
	 * own.
	 *      MOV         DEST                   VALUE        */
	/* { 0x00800001, 0x2e0000fd, 0x00000000, 0x00000000 },  */ // R, g112
	/* { 0x00800001, 0x2e4003fd, 0x00000000, 0x00000000 },  */ // G, g114
	/* { 0x00800001, 0x2e8003fd, 0x00000000, 0x00000000 },  */ // B, g116
	/* { 0x00800001, 0x2ec003fd, 0x00000000, 0x00000000 },  */ // A, g118

	/* Write instruction - return pixels.
	 * send (16) null g112 0x25 0x10031000 { align1, EOT }; */
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

/******************************************************************************/

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

static void
gen7_emit_multisample(struct intel_batchbuffer *batch)
{
	/* The 3DSTATE_MULTISAMPLE command is used to specify multisample state
	 * associated with the current render target/depth buffer. This is
	 * non-pipelined state.
	 */
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
	/* This command sets up the URB configuration for PS Push Constant
	 * Buffer. */
	OUT_BATCH(GEN7_3DSTATE_PUSH_CONSTANT_ALLOC_PS | (2 - 2));
	OUT_BATCH(0);

	/* VS URB Entry Allocation Size equal to 4(5 512-bit URB rows) may cause
	 * performance to decrease due to banking in the URB. Element sizes of
	 * 16 to 20 should be programmed with six 512-bit URB rows. Number of VS
	 * entries must be divisible by 8 if size < 9.
	 */
	OUT_BATCH(GEN7_3DSTATE_URB_VS | (2 - 2));
	OUT_BATCH((64 << GEN7_URB_ENTRY_NUMBER_SHIFT) |
		  (2 - 1) << GEN7_URB_ENTRY_SIZE_SHIFT |
		  (1 << GEN7_URB_STARTING_ADDRESS_SHIFT));

	/* This command may not overlap with the push constants in the URB
	 * defined by the 3DSTATE_PUSH_CONSTANT_ALLOC_VS,
	 * 3DSTATE_PUSH_CONSTANT_ALLOC_DS, 3DSTATE_PUSH_CONSTANT_ALLOC_HS, and
	 * 3DSTATE_PUSH_CONSTANT_ALLOC_GS commands.
	*/
	OUT_BATCH(GEN7_3DSTATE_URB_HS | (2 - 2));
	OUT_BATCH((0 << GEN7_URB_ENTRY_SIZE_SHIFT) |
		  (2 << GEN7_URB_STARTING_ADDRESS_SHIFT));

	/* This command may not overlap with the push constants in the URB
	 * defined by the 3DSTATE_PUSH_CONSTANT_ALLOC_VS,
	 * 3DSTATE_PUSH_CONSTANT_ALLOC_DS, 3DSTATE_PUSH_CONSTANT_ALLOC_HS, and
	 * 3DSTATE_PUSH_CONSTANT_ALLOC_GS commands.
	 */
	OUT_BATCH(GEN7_3DSTATE_URB_DS | (2 - 2));
	OUT_BATCH((0 << GEN7_URB_ENTRY_SIZE_SHIFT) |
		  (2 << GEN7_URB_STARTING_ADDRESS_SHIFT));

	/* This command may not overlap with the push constants in the URB
	 * defined by the 3DSTATE_PUSH_CONSTANT_ALLOC_VS,
	 * 3DSTATE_PUSH_CONSTANT_ALLOC_DS, 3DSTATE_PUSH_CONSTANT_ALLOC_HS, and
	 * 3DSTATE_PUSH_CONSTANT_ALLOC_GS commands.
	*/
	OUT_BATCH(GEN7_3DSTATE_URB_GS | (2 - 2));
	OUT_BATCH((0 << GEN7_URB_ENTRY_SIZE_SHIFT) |
		  (1 << GEN7_URB_STARTING_ADDRESS_SHIFT));
}

/* Vertex Shader stage */
static void
gen7_emit_vs(struct intel_batchbuffer *batch)
{
	OUT_BATCH(GEN7_3DSTATE_VS | (6 - 2));
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
}

/* Hull Shader stage */
static void
gen7_emit_hs(struct intel_batchbuffer *batch)
{
        OUT_BATCH(GEN7_3DSTATE_HS | (7 - 2));
        OUT_BATCH(0);
        OUT_BATCH(0);
        OUT_BATCH(0);
        OUT_BATCH(0);
        OUT_BATCH(0);
        OUT_BATCH(0);
}

/* Tessellation Engine */
static void
gen7_emit_te(struct intel_batchbuffer *batch)
{
        OUT_BATCH(GEN7_3DSTATE_TE | (4 - 2));
        OUT_BATCH(0);
        OUT_BATCH(0);
        OUT_BATCH(0);
}

/* Domain Shader stage */
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

/* Geometry Shader stage */
static void
gen7_emit_gs(struct intel_batchbuffer *batch)
{
        OUT_BATCH(GEN7_3DSTATE_GS | (7 - 2));
        OUT_BATCH(0);
        OUT_BATCH(0);
        OUT_BATCH(0);
        OUT_BATCH(0);
        OUT_BATCH(0);
        OUT_BATCH(0);
}

/* As a stage of the 3D pipeline, the CLIP stage receives inputs from the
 * previous (GS) stage. Refer to 3D Overview for an overview of the various
 * types of input to a 3D Pipeline stage. The remainder of this subsection
 * describes the inputs specific to the CLIP stage.
*/
static void
gen7_emit_clip(struct intel_batchbuffer *batch)
{
	/* The state used by the Clip Stage is defined by this inline state
	 * packet. */
        OUT_BATCH(GEN7_3DSTATE_CLIP | (4 - 2));
        OUT_BATCH(0);
        OUT_BATCH(0);
        OUT_BATCH(0);

	/* The 3DSTATE_VIEWPORT_STATE_POINTERS_CLIP command is used to define
	 * the location of fixed functions’ viewport state table.
	 */
        OUT_BATCH(GEN7_3DSTATE_VIEWPORT_STATE_POINTERS_SF_CL | (2 - 2));
        OUT_BATCH(0);
}

/* Strips and Fans stage
 *
 * The Strips and Fan (SF) stage of the 3D pipeline is responsible for
 * performing “setup” operations required to rasterize 3D objects.
 */
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

/* Windower/Masker stage
 *
 *
 */
static void
gen7_emit_wm(struct intel_batchbuffer *batch)
{
        OUT_BATCH(GEN7_3DSTATE_WM | (3 - 2));
        OUT_BATCH(GEN7_WM_DISPATCH_ENABLE |
                  GEN7_WM_PERSPECTIVE_PIXEL_BARYCENTRIC);
        OUT_BATCH(0);
}

/* Stream Output Logic stage */
static void
gen7_emit_sol(struct intel_batchbuffer *batch)
{
        OUT_BATCH(GEN7_3DSTATE_STREAMOUT | (3 - 2));
        OUT_BATCH(0);
        OUT_BATCH(0);
}

static void
gen7_emit_null_depth_buffer(struct intel_batchbuffer *batch)
{
	/* The depth buffer surface state is delivered as a pipelined state
	 * packet. However, the state change pipelining isn’t completely
	 * transparent. */
        OUT_BATCH(GEN7_3DSTATE_DEPTH_BUFFER | (7 - 2));
        OUT_BATCH(GEN7_SURFACE_NULL << GEN7_3DSTATE_DEPTH_BUFFER_TYPE_SHIFT |
                  GEN7_DEPTHFORMAT_D32_FLOAT << GEN7_3DSTATE_DEPTH_BUFFER_FORMAT_SHIFT);
	OUT_BATCH(0);
        OUT_BATCH(0);
        OUT_BATCH(0);
        OUT_BATCH(0);
        OUT_BATCH(0);

	/* This command defines the depth clear value delivered as a pipelined
	 * state command. However, the state change pipelining isn’t completely
	 * transparent */
	OUT_BATCH(GEN7_3DSTATE_CLEAR_PARAMS | (3 - 2));
        OUT_BATCH(0);
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

static uint32_t
gen7_create_cc_viewport(struct intel_batchbuffer *batch)
{
	struct gen7_cc_viewport *vp;

	vp = batch_alloc(batch, sizeof(*vp), 32);
	vp->min_depth = -1.e35;
	vp->max_depth = 1.e35;

	return batch_offset(batch, vp);
}

/* Colour calculator stage */
static void
gen7_emit_cc(struct intel_batchbuffer *batch)
{
	/* The 3DSTATE_BLEND_STATE_POINTERS command is used to set up the
	 * pointers to the color calculator state. */
        OUT_BATCH(GEN7_3DSTATE_BLEND_STATE_POINTERS | (2 - 2));
        OUT_BATCH(gen7_create_blend_state(batch));

	/* The 3DSTATE_VIEWPORT_STATE_POINTERS_CC command is used to define the
	 * location of fixed functions’ viewport state table. */
        OUT_BATCH(GEN7_3DSTATE_VIEWPORT_STATE_POINTERS_CC | (2 - 2));
	OUT_BATCH(gen7_create_cc_viewport(batch));
}

static struct gen7_sampler_state *
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

	return ss;
}

static void
gen7_emit_sampler(struct intel_batchbuffer *batch)
{
	struct gen7_sampler_state *ss = gen7_create_sampler(batch);
	uint32_t ss_pointer = batch_offset(batch, ss);

        OUT_BATCH(GEN7_3DSTATE_SAMPLER_STATE_POINTERS_PS | (2 - 2));
        OUT_BATCH(ss_pointer);
}

/* The state used by “setup backend” is defined by this inline state packet.
 */
static void
gen7_emit_sbe(struct intel_batchbuffer *batch)
{
	OUT_BATCH(GEN7_3DSTATE_SBE | (14 - 2));
	OUT_BATCH(1 << GEN7_SBE_NUM_OUTPUTS_SHIFT |
		  1 << GEN7_SBE_URB_ENTRY_READ_LENGTH_SHIFT |
		  1 << GEN7_SBE_URB_ENTRY_READ_OFFSET_SHIFT);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
}

/* 3DSTATE_PS
 *
 * Documentation: ivb_ihd_os_vol2_part1.pdf, p283
 *
 * This command is used to set state used by the pixel shader dispatch
 * stage.
 */
static void
gen7_emit_ps(struct intel_batchbuffer *batch)
{
	OUT_BATCH(GEN7_3DSTATE_PS | (8 - 2));

	/* Kernel Start Pointer
	 *
	 * Specifies the 64-byte aligned address offset of the first instruction
	 * in the kernel[0]. This pointer is relative to the Instruction Base
	 * Address.
	 */
	OUT_BATCH(batch_copy(batch, ps_kernel, sizeof(ps_kernel), 64));

	/* Single Program Flow
	 *
	 * Specifies the initial condition of the kernel program as either a
	 * single program flow (SIMDnxm with m = 1) or as multiple program flows
	 * (SIMDnxm with m > 1). See CR0 description in ISA Execution
	 * Environment.
	 */
	OUT_BATCH(1 << GEN7_PS_SAMPLER_COUNT_SHIFT |
		  2 << GEN7_PS_BINDING_TABLE_ENTRY_COUNT_SHIFT);

	/* Scratch Space Base Pointer
	 *
	 * Specifies the 1k-byte aligned address offset to scratch space for use
	 * by the kernel. This pointer is relative to the General State Base
	 * Address.
	 */
	OUT_BATCH(0);

	/* Maximum Number of Threads
	 *
	 * Specifies the maximum number of simultaneous threads allowed to be
	 * active. Used to avoid using up the scratch space, or to avoid
	 * potential deadlock.
	 */
	OUT_BATCH(40 << IVB_PS_MAX_THREADS_SHIFT |
		  GEN7_PS_16_DISPATCH_ENABLE |
		  GEN7_PS_ATTRIBUTE_ENABLE);

	/* Reserved */
	OUT_BATCH(6 << GEN7_PS_DISPATCH_START_GRF_SHIFT_0);

	/* Kernel Start Pointer
	 *
	 * Specifies the 64-byte aligned address offset of the first instruction
	 * in kernel[1]. This pointer is relative to the Instruction Base
	 * Address.
	 */
	OUT_BATCH(0);

	/* Kernel Start Pointer
	 *
	 * Specifies the 64-byte aligned address offset of the first instruction
	 * in kernel[2]. This pointer is relative to the Instruction Base
	 * Address.
	 */
	OUT_BATCH(0);
}

/* Documentation: ivb_ihd_os_vol2_part1.pdf
 *
 * This is a variable-length command used to specify the active vertex elements
 * (up to 34) Each VERTEX_ELEMENT_STATE structure contains a Valid bit which
 * determines which elements are used.
 */
static void
gen7_emit_vertex_elements(struct intel_batchbuffer *batch)
{
	/* 7:0 Vertex Element Count: (DWord Count + 1) / 2 */
	OUT_BATCH(GEN7_3DSTATE_VERTEX_ELEMENTS | 6);

	/* VERTEX_ELEMENT_STATE Structure
	 *
	 *   31:26 Vertex buffer index: specifies which vertex buffer the
	 *                              element is sourced from.
	 *   25    Valid: Whether the vertex element is used in vertex assembly.
	 *   24:16 Source element format.
	 *   15    Edge Flag Enable.
	 */
	OUT_BATCH(0 << GEN7_VE0_VERTEX_BUFFER_INDEX_SHIFT | GEN7_VE0_VALID |
		  GEN7_SURFACEFORMAT_R32G32B32A32_FLOAT << GEN7_VE0_FORMAT_SHIFT |
		  0 << GEN7_VE0_OFFSET_SHIFT);
	OUT_BATCH(GEN7_VFCOMPONENT_STORE_0 << GEN7_VE1_VFCOMPONENT_0_SHIFT |
		  GEN7_VFCOMPONENT_STORE_0 << GEN7_VE1_VFCOMPONENT_1_SHIFT |
		  GEN7_VFCOMPONENT_STORE_0 << GEN7_VE1_VFCOMPONENT_2_SHIFT |
		  GEN7_VFCOMPONENT_STORE_0 << GEN7_VE1_VFCOMPONENT_3_SHIFT);

	/* Spacial coordinates (X, Y). */
	OUT_BATCH(0 << GEN7_VE0_VERTEX_BUFFER_INDEX_SHIFT | GEN7_VE0_VALID |
		  GEN7_SURFACEFORMAT_R16G16_SSCALED << GEN7_VE0_FORMAT_SHIFT |
		  (0*2) << GEN7_VE0_OFFSET_SHIFT);
	OUT_BATCH(GEN7_VFCOMPONENT_STORE_SRC << GEN7_VE1_VFCOMPONENT_0_SHIFT |
		  GEN7_VFCOMPONENT_STORE_SRC << GEN7_VE1_VFCOMPONENT_1_SHIFT |
		  GEN7_VFCOMPONENT_STORE_0 << GEN7_VE1_VFCOMPONENT_2_SHIFT |
		  GEN7_VFCOMPONENT_STORE_1_FLT << GEN7_VE1_VFCOMPONENT_3_SHIFT);

	/* Texture coordinates (S, T). */
	OUT_BATCH(0 << GEN7_VE0_VERTEX_BUFFER_INDEX_SHIFT | GEN7_VE0_VALID |
		  GEN7_SURFACEFORMAT_R16G16_SSCALED << GEN7_VE0_FORMAT_SHIFT |
		  (2*2) << GEN7_VE0_OFFSET_SHIFT);
	OUT_BATCH(GEN7_VFCOMPONENT_STORE_SRC << GEN7_VE1_VFCOMPONENT_0_SHIFT |
		  GEN7_VFCOMPONENT_STORE_SRC << GEN7_VE1_VFCOMPONENT_1_SHIFT |
		  GEN7_VFCOMPONENT_STORE_0 << GEN7_VE1_VFCOMPONENT_2_SHIFT |
		  GEN7_VFCOMPONENT_STORE_1_FLT << GEN7_VE1_VFCOMPONENT_3_SHIFT);

	/* Pixel colour. */
	OUT_BATCH(0 << GEN7_VE0_VERTEX_BUFFER_INDEX_SHIFT | GEN7_VE0_VALID |
		  GEN7_SURFACEFORMAT_R16G16_SSCALED << GEN7_VE0_FORMAT_SHIFT |
		  (2*2) << GEN7_VE0_OFFSET_SHIFT);
	/* OUT_BATCH(GEN7_VFCOMPONENT_STORE_SRC << GEN7_VE1_VFCOMPONENT_0_SHIFT | */
	/* 	  GEN7_VFCOMPONENT_STORE_SRC << GEN7_VE1_VFCOMPONENT_1_SHIFT | */
	/* 	  GEN7_VFCOMPONENT_STORE_0 << GEN7_VE1_VFCOMPONENT_2_SHIFT | */
	/* 	  GEN7_VFCOMPONENT_STORE_1_FLT << GEN7_VE1_VFCOMPONENT_3_SHIFT); */
/* 	  GEN7_SURFACEFORMAT_R8G8B8A8_UINT << GEN7_VE0_FORMAT_SHIFT | */
	/* 	  (4*2) << GEN7_VE0_OFFSET_SHIFT); */
	/* OUT_BATCH(GEN7_VFCOMPONENT_NOSTORE << GEN7_VE1_VFCOMPONENT_0_SHIFT | */
	/* 	  GEN7_VFCOMPONENT_NOSTORE << GEN7_VE1_VFCOMPONENT_1_SHIFT); */
}

#define BORDER 30
static uint32_t
gen7_create_vertex_buffer(struct intel_batchbuffer *batch,
			  uint32_t src_x, uint32_t src_y,
			  uint32_t dst_x, uint32_t dst_y,
			  uint32_t width, uint32_t height)
{
	uint16_t *v;

	v = batch_alloc(batch, 18*sizeof(*v), 8);

	/*
	 *       COORDINATE SYSTEM
	 *
	 *          dst_x       dst_x+width
	 *    dst_y +--------------------->
	 *          |      v[4,7]
	 *          |         /\
	 *          |        /  \
	 *          |       /    \
	 *          |      /      \
	 *          |     /________\
	 *  dst_y + |  v[0,3]   v[8,11]
	 *  height  V
	 *
	 * v[(n*i)]    destination x
	 * v[(n*i)+1]  destination y
	 * v[(n*i)+2]  texture s
	 * v[(n*i)+3]  texture t
	 * v[(n*i)+4]  pixel colour (RG)
	 * v[(n*i)+4]  pixel colour (BA)
	 */
	v[0]  = dst_x + BORDER;
	v[1]  = dst_y + height - BORDER;
	v[2]  = src_x;
	v[3]  = src_y + height;
	v[4]  = 0xFFFF;
	v[5]  = 0xFFFF;

	v[6]  = dst_x + (width / 2);
	v[7]  = dst_y + BORDER;
	v[8]  = src_x + (width / 2);
	v[9]  = src_y;
	v[10] = 0xFFFF;
	v[11] = 0xFFFF;

	v[12]  = dst_x + width - BORDER;
	v[13]  = dst_y + height - BORDER;
	v[14]  = src_x + width;
	v[15]  = src_y + height;
	v[16]  = 0xFFFF;
	v[17]  = 0xFFFF;

	return batch_offset(batch, v);
}

/* Documentation: ivb_ihd_os_vol2_part1.pdf, p77
 *
 * This command is used to specify VB state used by the VF function.
 * * This command can specify from 1 to 33 VBs.
 *
 * The VertexBufferID field within a VERTEX_BUFFER_STATE structure indicates the
 * specific VB. If a VB definition is not included in this command, its
 * associated state is left unchanged and is available for use if previously
 * defined.
 */
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

	/* 3DSTATE_VERTEX_BUFFERS Structure
	 *
	 *   31:29 Instruction type
	 *   28:27 Instruction sub-type
	 *   26:24 Instruction Opcode
	 *   23:17 Instruction Sub-Opcode
	 *   15:8  Reserved
	 *    7:0  DWord Count
	 */
	OUT_BATCH(GEN7_3DSTATE_VERTEX_BUFFERS | (5 - 2));

	/* VERTEX_BUFFER_STATE Structure
	 *
	 * This structure is used in 3DSTATE_VERTEX_BUFFERS to set the state
	 * associated with a VB. The VF function will use this state to
	 * determine how/where to extract vertex element data for all vertex
	 * elements associated with the VB.  The VERTEX_BUFFER_STATE structure
	 * is 4 DWords for both INSTANCEDATA and VERTEXDATA buffers.A VB is
	 * defined as a 1D array of vertex data structures, accessed via a
	 * computed index value. The VF function therefore needs to know the
	 * starting address of the first structure (index 0) and size of the
	 * vertex data structure.
	 */
	OUT_BATCH(0 << GEN7_VB0_BUFFER_INDEX_SHIFT |
		  GEN7_VB0_VERTEXDATA |
		  GEN7_VB0_ADDRESS_MODIFY_ENABLE |
		  6*2 << GEN7_VB0_BUFFER_PITCH_SHIFT);

	/* Buffer Starting Address
	 *
	 * This field contains the byte-aligned Graphics Address of the first
	 * element of interest within the VB. Software must program this value
	 * with the combination (sum) of the base address of the memory resource
	 * and the byte offset from the base address to the starting structure
	 * within the buffer.
	 */
	OUT_RELOC(batch->bo, I915_GEM_DOMAIN_VERTEX, 0, offset);

	/* End Address
	 *
	 * This field defines the address of the last valid byte in this
	 * particular VB. Access of a vertex element which either straddles or
	 * is beyond this address will return 0’s for any data read.
	 */
	OUT_BATCH(~0);

	/* Instance Data Step Rate
	 *
	 * This field only applies to INSTANCEDATA buffers – it is ignored (but
	 * still present) for VERTEXDATA buffers).
	 *
	 * This field determines the rate at which instance data for this
	 * particular INSTANCEDATA vertex buffer is changed in sequential
	 * instances. Only after the number of instances specified by this field
	 * is generated is new (sequential) instance data provided. This process
	 * continues for each group of instances defined in the draw
	 * command. For example, a value of 1 in this field causes new instance
	 * data to be supplied with each sequential (instance) group of
	 * vertices. A value of 2 causes every other instance group of vertices
	 * to be provided with new instance data. The special value of 0 causes
	 * all vertices of all instances generated by the draw command to be
	 * provided with the same instance data. (The same effect can be
	 * achieved by setting this field to its maximum value.)
	 */
	OUT_BATCH(0);
}

static uint32_t
gen7_tiling_bits(uint32_t tiling)
{
	switch (tiling) {
	case I915_TILING_NONE:
		return 0;
	case I915_TILING_X:
		return GEN7_SURFACE_TILED;
	case I915_TILING_Y:
		return GEN7_SURFACE_TILED | GEN7_SURFACE_TILED_Y;
	default:
		assert(0);
	}
}

static uint32_t
gen7_bind_buf(struct intel_batchbuffer *batch,
	      struct scratch_buf *buf,
	      uint32_t format,
	      int is_dst)
{
	uint32_t *ss;
	uint32_t write_domain;
	uint32_t read_domain;
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

	ret = drm_intel_bo_emit_reloc(batch->bo,
				      batch_offset(batch, ss) + 4,
				      buf->bo, 0,
				      read_domain, write_domain);
	assert(ret == 0);

	return batch_offset(batch, ss);
}

static uint32_t
gen7_bind_surfaces(struct intel_batchbuffer *batch,
		   struct scratch_buf *src,
		   struct scratch_buf *dst)
{
	uint32_t *binding_table;

	binding_table = batch_alloc(batch, 8, 32);

	/* 32 bits per element, see PRM v4p1, p80 */
	binding_table[0] = gen7_bind_buf(batch, dst,
					 GEN7_SURFACEFORMAT_B8G8R8A8_UNORM, 1);
	binding_table[1] = gen7_bind_buf(batch, src,
					 GEN7_SURFACEFORMAT_B8G8R8A8_UNORM, 0);

	return batch_offset(batch, binding_table);
}

static void
gen7_emit_binding_table(struct intel_batchbuffer *batch,
			struct scratch_buf *src,
			struct scratch_buf *dst)
{
	/* The 3DSTATE_BINDING_TABLE_POINTERS_PS command is used to define the
	 * location of fixed functions’ BINDING_TABLE_STATE. Only some of the
	 * fixed functions utilize binding tables. */
	OUT_BATCH(GEN7_3DSTATE_BINDING_TABLE_POINTERS_PS | (2 - 2));
	OUT_BATCH(gen7_bind_surfaces(batch, src, dst));
}

/* Documentation: page 90, ivb_ihd_os_vol2_part1.pdf
 *
 * The 3DPRIMITIVE command is used to submit 3D primitives to be processed by
 * the 3D pipeline. Typically the processing results in rendering pixel data
 * into the render targets, but this is not required.  The parameters passed in
 * this command are forwarded to the Vertex Fetch function. The Vertex Fetch
 * function will use this information to generate vertex data structures and
 * store them in the URB. These vertices are then passed down the 3D pipeline.
 */
static void
gen7_emit_primitive(struct intel_batchbuffer *batch)
{
	OUT_BATCH(GEN7_3DPRIMITIVE | (7- 2));
        OUT_BATCH(GEN7_3DPRIMITIVE_VERTEX_SEQUENTIAL | _3DPRIM_TRILIST);

	/* Vertex count per instance: how many vertices are to be generated for
	 * each instance of the primitive topology.
	 */
	OUT_BATCH(3);

	/* Start vertex location: the "starting vertex" for each instance. This
	 * allowes skipping over part of the vertices in a buffer.
	 */
	OUT_BATCH(0);

	/* Instance count: the number of instances by which the primitive
	 * topology is to be regenerated.
	 */
        OUT_BATCH(1);

	/* Start instance location.
	 */
        OUT_BATCH(0);

	/* Base vertex location.
	 */
        OUT_BATCH(0);
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

#define BATCH_STATE_SPLIT 2048
void gen7_render_copyfunc(struct intel_batchbuffer *batch,
			  struct scratch_buf *src,
			  unsigned src_x, unsigned src_y,
			  unsigned width, unsigned height,
			  struct scratch_buf *dst,
			  unsigned dst_x, unsigned dst_y)
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
	gen7_emit_sol(batch);
	gen7_emit_null_depth_buffer(batch);

	gen7_emit_cc(batch);
        gen7_emit_sampler(batch);
        gen7_emit_sbe(batch);
        gen7_emit_ps(batch);

	gen7_emit_vertex_elements(batch);
        gen7_emit_vertex_buffer(batch,
				src_x, src_y,
				dst_x, dst_y,
				width, height);
	gen7_emit_binding_table(batch, src, dst);
	gen7_emit_primitive(batch);

	OUT_BATCH(MI_BATCH_BUFFER_END);

	batch_end = batch->ptr - batch->buffer;
	batch_end = ALIGN(batch_end, 8);
	assert(batch_end < BATCH_STATE_SPLIT);

	gen7_render_flush(batch, batch_end);
	intel_batchbuffer_reset(batch);
}
