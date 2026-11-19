//! The C declarations of iprop.h (C API 0.7, ABI 0.5) and iprop3d.h (model
//! API 0.3) this crate uses. Records mirror the headers field for field; the
//! sizes below are the pins of backends/capi3d/test/models.c on 64-bit
//! targets.

#![allow(non_camel_case_types, dead_code)]

use std::ffi::{c_char, c_void};

pub type Status = u32;
pub const OK: Status = 0;

#[repr(C)]
#[derive(Clone, Copy)]
pub struct Utf8View {
    pub data: *const c_char,
    pub byte_count: u32,
}

#[repr(C)]
#[derive(Clone, Copy, Default, Debug, PartialEq, Eq)]
pub struct Color {
    pub r: u8,
    pub g: u8,
    pub b: u8,
    pub a: u8,
}

#[repr(C)]
pub struct ErrorEx {
    pub struct_size: u32,
    pub status: u32,
    pub reason: u32,
    pub stage: u32,
    pub byte_offset: u64,
    pub location_flags: u32,
    pub offset_space: u32,
    pub function_index: u32,
    pub pc: u32,
    pub debug_line: u32,
    pub message_buffer: *mut c_char,
    pub message_capacity: u32,
    pub message_required_bytes: u64,
    pub message_truncated: u32,
}

#[repr(C)]
#[derive(Default)]
pub struct LibraryInfoEx {
    pub struct_size: u32,
    pub product_major: u32,
    pub product_minor: u32,
    pub product_patch: u32,
    pub api_major: u32,
    pub api_minor: u32,
    pub abi_major: u32,
    pub abi_minor: u32,
    pub format_major: u32,
    pub format_minor_min: u32,
    pub format_minor_max: u32,
    pub capabilities: u64,
}

#[repr(C)]
#[derive(Default)]
pub struct ProgramInfoEx {
    pub struct_size: u32,
    pub size_x: u32,
    pub size_y: u32,
    pub origin_x: f32,
    pub origin_y: f32,
    pub fuel: u64,
    pub has_emission: u32,
    pub emission_color: Color,
    pub emission_offset_x: f32,
    pub emission_offset_y: f32,
    pub emission_radius: f32,
    pub format_major: u32,
    pub format_minor: u32,
    pub container_flags: u32,
}

pub const INPUT_COLOR: u32 = 0;
pub const INPUT_F32: u32 = 1;
pub const INPUT_U32: u32 = 2;

#[repr(C)]
pub struct InputInfoEx {
    pub struct_size: u32,
    pub flags: u32,
    pub input_index: u32,
    pub kind: u32,
    pub name: Utf8View,
    pub color_default: Color,
    pub f32_default: f32,
    pub u32_default: u32,
}

#[repr(C)]
#[derive(Default)]
pub struct ParameterValueEx {
    pub struct_size: u32,
    pub flags: u32,
    pub kind: u32,
    pub color: Color,
    pub f32: f32,
    pub u32: u32,
}

#[repr(C)]
#[derive(Default)]
pub struct GenerateOptionsEx {
    pub struct_size: u32,
    pub flags: u32,
    pub seed: u32,
    pub size_x: u32,
    pub size_y: u32,
}

#[repr(C)]
pub struct ExecutionLimitsEx {
    pub struct_size: u32,
    pub flags: u32,
    pub work_limit: u64,
    pub timeout_ns: u64,
    pub cancellation: *const c_void,
    pub poll: Option<unsafe extern "C" fn(*mut c_void) -> u32>,
    pub poll_context: *mut c_void,
}

#[repr(C)]
#[derive(Default)]
pub struct WorkStatsEx {
    pub struct_size: u32,
    pub flags: u32,
    pub work_budget: u64,
    pub work_remaining: u64,
    pub work_used: u64,
}

#[repr(C)]
#[derive(Default)]
pub struct ResultStatsEx {
    pub struct_size: u32,
    pub flags: u32,
    pub fuel_budget: u64,
    pub fuel_remaining: u64,
    pub fuel_used: u64,
    pub image_byte_count: u64,
    pub output_element_count: u64,
}

pub const PIXEL_FORMAT_RGBA8: u32 = 1;

#[repr(C)]
pub struct ImageViewEx {
    pub struct_size: u32,
    pub flags: u32,
    pub width: u32,
    pub height: u32,
    pub byte_count: u64,
    pub row_stride: u64,
    pub pixel_format: u32,
    pub alpha_mode: u32,
    pub origin: u32,
    pub pixels: *const u8,
}

#[repr(C)]
pub struct AuxImageViewEx {
    pub struct_size: u32,
    pub flags: u32,
    pub image_index: u32,
    pub kind: u32,
    pub source: u32,
    pub width: u32,
    pub height: u32,
    pub byte_count: u64,
    pub row_stride: u64,
    pub pixel_format: u32,
    pub alpha_mode: u32,
    pub origin: u32,
    pub name: Utf8View,
    pub pixels: *const u8,
}

#[repr(C)]
pub struct OutputViewEx {
    pub struct_size: u32,
    pub flags: u32,
    pub output_index: u32,
    pub owner: u32,
    pub kind: u32,
    pub is_list: u32,
    pub element_count: u32,
    pub stride_floats: u32,
    pub name: Utf8View,
    pub values: *const f32,
}

// ---- models ----------------------------------------------------------------

#[repr(C)]
#[derive(Default)]
pub struct LibraryInfo3d {
    pub struct_size: u32,
    pub api_major: u32,
    pub api_minor: u32,
    pub max_vertices: u32,
    pub max_triangles: u32,
    pub max_textures: u32,
    pub max_texture_side: u32,
    pub max_texels: u64,
}

#[repr(C)]
#[derive(Default)]
pub struct ModelInfo {
    pub struct_size: u32,
    pub flags: u32,
    pub size_x: f32,
    pub size_y: f32,
    pub size_z: f32,
    pub units_per_meter: f32,
    pub fuel: u64,
    pub input_count: u32,
    pub skin_program_count: u32,
    pub container_flags: u32,
}

#[repr(C)]
pub struct SkinProgramInfo {
    pub struct_size: u32,
    pub flags: u32,
    pub program_index: u32,
    pub width: u32,
    pub height: u32,
    pub name: Utf8View,
}

#[repr(C)]
#[derive(Clone, Copy, Default, Debug, PartialEq, Eq, Hash)]
pub struct KnownTexture {
    pub key: u64,
    pub fuel: u64,
    pub work: u64,
    pub image_kinds: u32,
    pub reserved: u32,
}

#[repr(C)]
pub struct GenerateOptions3d {
    pub struct_size: u32,
    pub flags: u32,
    pub seed: u32,
    pub lod: u32,
    pub known_textures: *const KnownTexture,
    pub known_texture_count: u32,
}

#[repr(C)]
#[derive(Default)]
pub struct ResultInfo3d {
    pub struct_size: u32,
    pub flags: u32,
    pub bounds_min: [f32; 3],
    pub bounds_max: [f32; 3],
    pub size: [f32; 3],
    pub units_per_meter: f32,
    pub vertex_count: u32,
    pub triangle_count: u32,
    pub surface_count: u32,
    pub texture_count: u32,
    pub fuel_budget: u64,
    pub fuel_used: u64,
    pub work_budget: u64,
    pub work_used: u64,
    pub output_count: u32,
}

pub const ALPHA_OPAQUE: u32 = 0;
pub const ALPHA_CUTOUT: u32 = 1;
pub const ALPHA_BLEND: u32 = 2;

#[repr(C)]
pub struct SurfaceView {
    pub struct_size: u32,
    pub flags: u32,
    pub surface_index: u32,
    pub texture_index: u32,
    pub roughness: f32,
    pub metallic: f32,
    pub emission: f32,
    pub alpha_mode: u32,
    pub alpha_cutoff: f32,
    pub two_sided: u32,
    pub crisp: u32,
    pub vertex_count: u32,
    pub index_count: u32,
    pub positions: *const f32,
    pub normals: *const f32,
    pub tangents: *const f32,
    pub uvs: *const f32,
    pub indices: *const u32,
}

#[repr(C)]
pub struct TextureView {
    pub struct_size: u32,
    pub flags: u32,
    pub texture_index: u32,
    pub repeats: u32,
    pub key: u64,
    pub name: Utf8View,
    pub asset_width: u32,
    pub asset_height: u32,
    pub width: u32,
    pub height: u32,
    pub byte_count: u64,
    pub row_stride: u64,
    pub pixel_format: u32,
    pub alpha_mode: u32,
    pub origin: u32,
    pub image_count: u32,
    pub pixels: *const u8,
    pub fuel: u64,
    pub work: u64,
    pub image_kinds: u32,
    pub reused: u32,
}

pub const OUTPUT_POINT3D: u32 = 0;
pub const OUTPUT_BOX3D: u32 = 1;

#[repr(C)]
pub struct OutputView3d {
    pub struct_size: u32,
    pub flags: u32,
    pub output_index: u32,
    pub kind: u32,
    pub list: u32,
    pub value_count: u32,
    pub floats_per_value: u32,
    pub name: Utf8View,
    pub values: *const f32,
}

#[cfg(target_pointer_width = "64")]
const _: () = {
    use std::mem::size_of;
    assert!(size_of::<LibraryInfo3d>() == 40);
    assert!(size_of::<ModelInfo>() == 48);
    assert!(size_of::<SkinProgramInfo>() == 40);
    assert!(size_of::<KnownTexture>() == 32);
    assert!(size_of::<GenerateOptions3d>() == 32);
    assert!(size_of::<ResultInfo3d>() == 104);
    assert!(size_of::<SurfaceView>() == 96);
    assert!(size_of::<TextureView>() == 120);
    assert!(size_of::<OutputView3d>() == 56);
};

pub enum Program {}
pub enum Parameters {}
pub enum ResultHandle {}
pub enum Model {}
pub enum ModelResult {}

unsafe extern "C" {
    pub fn iprop_library_query_ex(out: *mut LibraryInfoEx, error: *mut ErrorEx) -> Status;
    pub fn iprop_program_load_ex(
        bytes: *const u8,
        byte_count: u64,
        out: *mut *mut Program,
        error: *mut ErrorEx,
    ) -> Status;
    pub fn iprop_program_free(program: *mut Program);
    pub fn iprop_program_query(
        program: *const Program,
        out: *mut ProgramInfoEx,
        error: *mut ErrorEx,
    ) -> Status;
    pub fn iprop_program_input_count(
        program: *const Program,
        out: *mut u32,
        error: *mut ErrorEx,
    ) -> Status;
    pub fn iprop_program_input_get(
        program: *const Program,
        index: u32,
        out: *mut InputInfoEx,
        error: *mut ErrorEx,
    ) -> Status;
    pub fn iprop_parameters_create(
        program: *const Program,
        out: *mut *mut Parameters,
        error: *mut ErrorEx,
    ) -> Status;
    pub fn iprop_parameters_free(parameters: *mut Parameters);
    pub fn iprop_parameters_set_named(
        parameters: *mut Parameters,
        name: Utf8View,
        value: *const ParameterValueEx,
        error: *mut ErrorEx,
    ) -> Status;
    pub fn iprop_generate_controlled(
        program: *const Program,
        options: *const GenerateOptionsEx,
        parameters: *const Parameters,
        limits: *const ExecutionLimitsEx,
        out: *mut *mut ResultHandle,
        error: *mut ErrorEx,
    ) -> Status;
    pub fn iprop_result_free(result: *mut ResultHandle);
    pub fn iprop_result_query_image(
        result: *const ResultHandle,
        out: *mut ImageViewEx,
        error: *mut ErrorEx,
    ) -> Status;
    pub fn iprop_result_query_stats(
        result: *const ResultHandle,
        out: *mut ResultStatsEx,
        error: *mut ErrorEx,
    ) -> Status;
    pub fn iprop_result_query_work(
        result: *const ResultHandle,
        out: *mut WorkStatsEx,
        error: *mut ErrorEx,
    ) -> Status;
    pub fn iprop_result_aux_image_count(
        result: *const ResultHandle,
        out: *mut u32,
        error: *mut ErrorEx,
    ) -> Status;
    pub fn iprop_result_aux_image_get(
        result: *const ResultHandle,
        index: u32,
        out: *mut AuxImageViewEx,
        error: *mut ErrorEx,
    ) -> Status;
    pub fn iprop_result_output_count(
        result: *const ResultHandle,
        out: *mut u32,
        error: *mut ErrorEx,
    ) -> Status;
    pub fn iprop_result_output_get(
        result: *const ResultHandle,
        index: u32,
        out: *mut OutputViewEx,
        error: *mut ErrorEx,
    ) -> Status;

    pub fn iprop3d_library_query(out: *mut LibraryInfo3d, error: *mut ErrorEx) -> Status;
    pub fn iprop3d_model_load(
        bytes: *const u8,
        byte_count: u64,
        out: *mut *mut Model,
        error: *mut ErrorEx,
    ) -> Status;
    pub fn iprop3d_model_free(model: *mut Model);
    pub fn iprop3d_model_query(
        model: *const Model,
        out: *mut ModelInfo,
        error: *mut ErrorEx,
    ) -> Status;
    pub fn iprop3d_model_skin_program(
        model: *const Model,
        index: u32,
        out: *mut SkinProgramInfo,
        error: *mut ErrorEx,
    ) -> Status;
    pub fn iprop3d_model_input_count(
        model: *const Model,
        out: *mut u32,
        error: *mut ErrorEx,
    ) -> Status;
    pub fn iprop3d_model_input_get(
        model: *const Model,
        index: u32,
        out: *mut InputInfoEx,
        error: *mut ErrorEx,
    ) -> Status;
    pub fn iprop3d_model_parameters_create(
        model: *const Model,
        out: *mut *mut Parameters,
        error: *mut ErrorEx,
    ) -> Status;
    pub fn iprop3d_generate(
        model: *const Model,
        options: *const GenerateOptions3d,
        parameters: *const Parameters,
        limits: *const ExecutionLimitsEx,
        out: *mut *mut ModelResult,
        error: *mut ErrorEx,
    ) -> Status;
    pub fn iprop3d_result_free(result: *mut ModelResult);
    pub fn iprop3d_result_query(
        result: *const ModelResult,
        out: *mut ResultInfo3d,
        error: *mut ErrorEx,
    ) -> Status;
    pub fn iprop3d_result_surface(
        result: *const ModelResult,
        index: u32,
        out: *mut SurfaceView,
        error: *mut ErrorEx,
    ) -> Status;
    pub fn iprop3d_result_texture(
        result: *const ModelResult,
        index: u32,
        out: *mut TextureView,
        error: *mut ErrorEx,
    ) -> Status;
    pub fn iprop3d_result_texture_image(
        result: *const ModelResult,
        texture: u32,
        image: u32,
        out: *mut AuxImageViewEx,
        error: *mut ErrorEx,
    ) -> Status;
    pub fn iprop3d_result_output(
        result: *const ModelResult,
        index: u32,
        out: *mut OutputView3d,
        error: *mut ErrorEx,
    ) -> Status;
    pub fn iprop3d_texture_stamp() -> u64;
}
