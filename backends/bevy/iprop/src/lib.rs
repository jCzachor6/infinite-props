//! Infinite Props for Rust (preview): load a compiled program (`.iprop`) or
//! model (`.iprop3d`) and generate it from a seed and inputs.
//!
//! A [`Program`] draws a 2D image with its auxiliary images (normal and
//! material maps) and outputs. A [`Model`] builds triangles, the textures
//! they wear and 3D outputs. Every result is an owned copy: nothing borrowed
//! from the native library outlives the call that made it.
//!
//! Models follow the model C API 0.3 (`iprop3d.h`), a preview that may change
//! until the model contract is frozen; programs follow the C API 0.7 and
//! ABI 0.5 (`iprop.h`).

mod ffi;

use std::ffi::c_char;
use std::fmt;
use std::ptr::{self, NonNull};
use std::sync::{Mutex, OnceLock};
use std::time::Duration;

/// A failed call: the status and the structured reason of the C API, the
/// stage it failed in and its message.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Error {
    /// `IPROP_EX_E_*`: 1 out of memory, 2 format, 3 trap, 4 argument,
    /// 5 internal, 6 unsupported.
    pub status: u32,
    /// `IPROP_REASON_*`, such as 3015 for exhausted work.
    pub reason: u32,
    /// `IPROP_STAGE_*`: 1 load, 2 validate, 3 arguments, 4 execute.
    pub stage: u32,
    /// The line of the source when the file carries debug lines, else 0.
    pub debug_line: u32,
    pub message: String,
}

impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "{} (status {}, reason {})",
            self.message, self.status, self.reason
        )
    }
}

impl std::error::Error for Error {}

pub type Result<T> = std::result::Result<T, Error>;

const STATUS_ARGUMENT: u32 = 4;
const REASON_INVALID_ARGUMENT: u32 = 4000;

fn argument(message: impl Into<String>) -> Error {
    Error {
        status: STATUS_ARGUMENT,
        reason: REASON_INVALID_ARGUMENT,
        stage: 3,
        debug_line: 0,
        message: message.into(),
    }
}

// One call with a structured error descriptor and a message buffer.
fn call(f: impl FnOnce(*mut ffi::ErrorEx) -> ffi::Status) -> Result<()> {
    let mut buffer = vec![0u8; 4096];
    let mut error = ffi::ErrorEx {
        struct_size: size_of::<ffi::ErrorEx>() as u32,
        status: 0,
        reason: 0,
        stage: 0,
        byte_offset: 0,
        location_flags: 0,
        offset_space: 0,
        function_index: 0,
        pc: 0,
        debug_line: 0,
        message_buffer: buffer.as_mut_ptr() as *mut c_char,
        message_capacity: buffer.len() as u32,
        message_required_bytes: 0,
        message_truncated: 0,
    };
    let status = f(&mut error);
    if status == ffi::OK {
        return Ok(());
    }
    let end = buffer.iter().position(|&b| b == 0).unwrap_or(buffer.len());
    Err(Error {
        status,
        reason: error.reason,
        stage: error.stage,
        debug_line: if error.location_flags & (1 << 3) != 0 {
            error.debug_line
        } else {
            0
        },
        message: String::from_utf8_lossy(&buffer[..end]).into_owned(),
    })
}

macro_rules! sized {
    ($kind:ty) => {{
        let mut value: $kind = unsafe { std::mem::zeroed() };
        value.struct_size = size_of::<$kind>() as u32;
        value
    }};
}

fn text(view: ffi::Utf8View) -> String {
    if view.data.is_null() || view.byte_count == 0 {
        return String::new();
    }
    let bytes =
        unsafe { std::slice::from_raw_parts(view.data as *const u8, view.byte_count as usize) };
    String::from_utf8_lossy(bytes).into_owned()
}

unsafe fn copy<T: Copy>(data: *const T, count: usize) -> Vec<T> {
    if data.is_null() || count == 0 {
        return Vec::new();
    }
    unsafe { std::slice::from_raw_parts(data, count) }.to_vec()
}

// The versions this crate is written against, checked once.
fn require_library() -> Result<()> {
    static CHECK: OnceLock<Result<()>> = OnceLock::new();
    CHECK
        .get_or_init(|| {
            let mut info = sized!(ffi::LibraryInfoEx);
            call(|e| unsafe { ffi::iprop_library_query_ex(&mut info, e) })?;
            if (info.api_major, info.abi_major, info.abi_minor) != (0, 0, 5) || info.api_minor < 7 {
                return Err(argument(
                    "Infinite Props requires C API 0.7 or later with ABI 0.5",
                ));
            }
            let mut models = sized!(ffi::LibraryInfo3d);
            call(|e| unsafe { ffi::iprop3d_library_query(&mut models, e) })?;
            if (models.api_major, models.api_minor) != (0, 3) {
                return Err(argument("Infinite Props requires the model API 0.3"));
            }
            Ok(())
        })
        .clone()
}

// ---- inputs and limits ----------------------------------------------------------

/// A value for a declared input. A source `i32` input is stored as `U32`:
/// pass its bits, or use [`InputValue::i32`].
#[derive(Clone, Copy, Debug, PartialEq)]
pub enum InputValue {
    /// RGBA, 0-255 each.
    Color([u8; 4]),
    F32(f32),
    U32(u32),
}

impl InputValue {
    /// A signed value for an `i32` input, by its bits.
    pub fn i32(value: i32) -> Self {
        InputValue::U32(value as u32)
    }
}

/// One input a program or model declares, with its default.
#[derive(Clone, Debug, PartialEq)]
pub struct Input {
    pub name: String,
    pub default: InputValue,
}

/// Work, time and nothing else: a zero work limit selects the default of
/// what runs (100,000,000 steps for a program, 600,000,000 for a model).
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct Limits {
    pub work_limit: u64,
    pub timeout: Option<Duration>,
}

impl Limits {
    fn raw(&self) -> ffi::ExecutionLimitsEx {
        ffi::ExecutionLimitsEx {
            struct_size: size_of::<ffi::ExecutionLimitsEx>() as u32,
            flags: 0,
            work_limit: self.work_limit,
            timeout_ns: self.timeout.map_or(0, |t| {
                u64::try_from(t.as_nanos()).unwrap_or(u64::MAX).max(1)
            }),
            cancellation: ptr::null(),
            poll: None,
            poll_context: ptr::null_mut(),
        }
    }
}

fn read_input(info: &ffi::InputInfoEx) -> Input {
    let default = match info.kind {
        ffi::INPUT_COLOR => {
            let c = info.color_default;
            InputValue::Color([c.r, c.g, c.b, c.a])
        }
        ffi::INPUT_F32 => InputValue::F32(info.f32_default),
        _ => InputValue::U32(info.u32_default),
    };
    Input {
        name: text(info.name),
        default,
    }
}

struct Parameters(NonNull<ffi::Parameters>);

impl Drop for Parameters {
    fn drop(&mut self) {
        unsafe { ffi::iprop_parameters_free(self.0.as_ptr()) }
    }
}

impl Parameters {
    fn set(&mut self, name: &str, value: InputValue) -> Result<()> {
        let mut raw = sized!(ffi::ParameterValueEx);
        match value {
            InputValue::Color([r, g, b, a]) => {
                raw.kind = ffi::INPUT_COLOR;
                raw.color = ffi::Color { r, g, b, a };
            }
            InputValue::F32(v) => {
                raw.kind = ffi::INPUT_F32;
                raw.f32 = v;
            }
            InputValue::U32(v) => {
                raw.kind = ffi::INPUT_U32;
                raw.u32 = v;
            }
        }
        let view = ffi::Utf8View {
            data: name.as_ptr() as *const c_char,
            byte_count: u32::try_from(name.len())
                .map_err(|_| argument("input name is too long"))?,
        };
        call(|e| unsafe { ffi::iprop_parameters_set_named(self.0.as_ptr(), view, &raw, e) })
    }
}

/// An auxiliary image: what [`AuxImage::kind`] says, as RGBA8 rows from the
/// top. A normal map stores tangent-space directions; roughness, metallic
/// and occlusion maps hold their value in red, green and blue alike; an
/// emission map is a colour.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct AuxImage {
    pub kind: AuxKind,
    pub name: String,
    pub width: u32,
    pub height: u32,
    pub rgba: Vec<u8>,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum AuxKind {
    Normal,
    Roughness,
    Metallic,
    Ao,
    Emission,
    Other(u32),
}

impl AuxKind {
    fn from_raw(kind: u32) -> Self {
        match kind {
            1 => AuxKind::Normal,
            2 => AuxKind::Roughness,
            3 => AuxKind::Metallic,
            4 => AuxKind::Ao,
            5 => AuxKind::Emission,
            other => AuxKind::Other(other),
        }
    }

    /// The `iprop_aux_image_kind` number.
    pub fn raw(self) -> u32 {
        match self {
            AuxKind::Normal => 1,
            AuxKind::Roughness => 2,
            AuxKind::Metallic => 3,
            AuxKind::Ao => 4,
            AuxKind::Emission => 5,
            AuxKind::Other(other) => other,
        }
    }
}

fn read_aux(view: &ffi::AuxImageViewEx) -> Result<AuxImage> {
    if view.pixel_format != ffi::PIXEL_FORMAT_RGBA8 {
        return Err(argument("an auxiliary image is not RGBA8"));
    }
    Ok(AuxImage {
        kind: AuxKind::from_raw(view.kind),
        name: text(view.name),
        width: view.width,
        height: view.height,
        rgba: unsafe { copy(view.pixels, view.byte_count as usize) },
    })
}

// ---- programs -------------------------------------------------------------------

/// The size and anchor a program declares, and its fuel budget.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct ProgramInfo {
    pub width: u32,
    pub height: u32,
    /// Where the prop stands, in pixels from the upper left corner.
    pub origin: [f32; 2],
    pub fuel: u64,
}

/// A loaded, validated `.iprop` file. Calls on one program are serialized;
/// load it once per thread to generate in parallel.
pub struct Program {
    raw: NonNull<ffi::Program>,
    info: ProgramInfo,
    inputs: Vec<Input>,
    lock: Mutex<()>,
}

// The native program is used under `lock` only.
unsafe impl Send for Program {}
unsafe impl Sync for Program {}

impl Drop for Program {
    fn drop(&mut self) {
        unsafe { ffi::iprop_program_free(self.raw.as_ptr()) }
    }
}

/// What to generate: a seed, the canvas size (the program's own when
/// `None`), inputs by name and execution limits.
#[derive(Clone, Debug, Default, PartialEq)]
pub struct Request {
    pub seed: u32,
    pub size: Option<(u32, u32)>,
    pub inputs: Vec<(String, InputValue)>,
    pub limits: Limits,
}

impl Request {
    pub fn seed(seed: u32) -> Self {
        Request {
            seed,
            ..Default::default()
        }
    }

    pub fn input(mut self, name: impl Into<String>, value: InputValue) -> Self {
        self.inputs.push((name.into(), value));
        self
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum OutputKind {
    Point,
    Rect,
    Circle,
    Part,
    Other(u32),
}

/// One output slot, in the program's order: root outputs (owner 0), then
/// those of its parts. `values` holds `stride` floats per element: x, y for
/// a point, x, y, w, h for a rectangle and x, y, r for a circle. Part
/// outputs carry no values here.
#[derive(Clone, Debug, PartialEq)]
pub struct Output {
    pub name: String,
    pub kind: OutputKind,
    pub list: bool,
    pub owner: u32,
    pub stride: usize,
    pub values: Vec<f32>,
}

/// One generated image: RGBA8 rows from the top, with its auxiliary images,
/// outputs and counters.
#[derive(Clone, Debug, PartialEq)]
pub struct GeneratedImage {
    pub width: u32,
    pub height: u32,
    pub rgba: Vec<u8>,
    pub images: Vec<AuxImage>,
    pub outputs: Vec<Output>,
    pub fuel_budget: u64,
    pub fuel_used: u64,
    pub work_budget: u64,
    pub work_used: u64,
}

impl GeneratedImage {
    /// The first auxiliary image of a kind.
    pub fn image(&self, kind: AuxKind) -> Option<&AuxImage> {
        self.images.iter().find(|image| image.kind == kind)
    }
}

struct ResultHandle(NonNull<ffi::ResultHandle>);

impl Drop for ResultHandle {
    fn drop(&mut self) {
        unsafe { ffi::iprop_result_free(self.0.as_ptr()) }
    }
}

impl Program {
    /// Loads and validates a compiled program.
    pub fn load(bytes: &[u8]) -> Result<Program> {
        require_library()?;
        let mut raw = ptr::null_mut();
        call(|e| unsafe {
            ffi::iprop_program_load_ex(bytes.as_ptr(), bytes.len() as u64, &mut raw, e)
        })?;
        let raw = NonNull::new(raw).ok_or_else(|| argument("the library returned no program"))?;
        let mut program = Program {
            raw,
            info: ProgramInfo {
                width: 0,
                height: 0,
                origin: [0.0; 2],
                fuel: 0,
            },
            inputs: Vec::new(),
            lock: Mutex::new(()),
        };
        let mut info = sized!(ffi::ProgramInfoEx);
        call(|e| unsafe { ffi::iprop_program_query(raw.as_ptr(), &mut info, e) })?;
        program.info = ProgramInfo {
            width: info.size_x,
            height: info.size_y,
            origin: [info.origin_x, info.origin_y],
            fuel: info.fuel,
        };
        let mut count = 0;
        call(|e| unsafe { ffi::iprop_program_input_count(raw.as_ptr(), &mut count, e) })?;
        for index in 0..count {
            let mut input = sized!(ffi::InputInfoEx);
            call(|e| unsafe { ffi::iprop_program_input_get(raw.as_ptr(), index, &mut input, e) })?;
            program.inputs.push(read_input(&input));
        }
        Ok(program)
    }

    pub fn info(&self) -> ProgramInfo {
        self.info
    }

    pub fn inputs(&self) -> &[Input] {
        &self.inputs
    }

    /// Generates one image. Inputs the request leaves out keep their
    /// defaults.
    pub fn generate(&self, request: &Request) -> Result<GeneratedImage> {
        let _guard = self
            .lock
            .lock()
            .unwrap_or_else(|poisoned| poisoned.into_inner());
        let mut parameters = ptr::null_mut();
        call(|e| unsafe { ffi::iprop_parameters_create(self.raw.as_ptr(), &mut parameters, e) })?;
        let mut parameters =
            Parameters(NonNull::new(parameters).ok_or_else(|| argument("no parameter set"))?);
        for (name, value) in &request.inputs {
            parameters.set(name, *value)?;
        }
        let mut options = sized!(ffi::GenerateOptionsEx);
        options.seed = request.seed;
        if let Some((width, height)) = request.size {
            options.size_x = width;
            options.size_y = height;
        }
        let limits = request.limits.raw();
        let mut raw = ptr::null_mut();
        call(|e| unsafe {
            ffi::iprop_generate_controlled(
                self.raw.as_ptr(),
                &options,
                parameters.0.as_ptr(),
                &limits,
                &mut raw,
                e,
            )
        })?;
        let result = ResultHandle(
            NonNull::new(raw).ok_or_else(|| argument("the library returned no result"))?,
        );
        let r = result.0.as_ptr();

        let mut image = sized!(ffi::ImageViewEx);
        call(|e| unsafe { ffi::iprop_result_query_image(r, &mut image, e) })?;
        let mut stats = sized!(ffi::ResultStatsEx);
        call(|e| unsafe { ffi::iprop_result_query_stats(r, &mut stats, e) })?;
        let mut work = sized!(ffi::WorkStatsEx);
        call(|e| unsafe { ffi::iprop_result_query_work(r, &mut work, e) })?;

        let mut count = 0;
        call(|e| unsafe { ffi::iprop_result_aux_image_count(r, &mut count, e) })?;
        let mut images = Vec::with_capacity(count as usize);
        for index in 0..count {
            let mut view = sized!(ffi::AuxImageViewEx);
            call(|e| unsafe { ffi::iprop_result_aux_image_get(r, index, &mut view, e) })?;
            images.push(read_aux(&view)?);
        }

        call(|e| unsafe { ffi::iprop_result_output_count(r, &mut count, e) })?;
        let mut outputs = Vec::with_capacity(count as usize);
        for index in 0..count {
            let mut view = sized!(ffi::OutputViewEx);
            call(|e| unsafe { ffi::iprop_result_output_get(r, index, &mut view, e) })?;
            let kind = match view.kind {
                0 => OutputKind::Point,
                1 => OutputKind::Rect,
                2 => OutputKind::Circle,
                3 => OutputKind::Part,
                other => OutputKind::Other(other),
            };
            let stride = if kind == OutputKind::Part {
                0
            } else {
                view.stride_floats as usize
            };
            let values = unsafe { copy(view.values, view.element_count as usize * stride) };
            outputs.push(Output {
                name: text(view.name),
                kind,
                list: view.is_list != 0,
                owner: view.owner,
                stride,
                values,
            });
        }

        Ok(GeneratedImage {
            width: image.width,
            height: image.height,
            rgba: unsafe { copy(image.pixels, image.byte_count as usize) },
            images,
            outputs,
            fuel_budget: stats.fuel_budget,
            fuel_used: stats.fuel_used,
            work_budget: work.work_budget,
            work_used: work.work_used,
        })
    }
}

// ---- models ---------------------------------------------------------------------

/// The model API's version and the limits of one generation.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct ModelLibrary {
    pub api: (u32, u32),
    pub max_vertices: u32,
    pub max_triangles: u32,
    pub max_textures: u32,
    pub max_texture_side: u32,
    pub max_texels: u64,
    /// The coarsest level of detail.
    pub max_lod: u32,
    /// Changes whenever a texture's pixels, fuel or work may; a host that
    /// keeps textures beyond the process stores it beside them.
    pub texture_stamp: u64,
}

/// Describes the loaded model library.
pub fn model_library() -> Result<ModelLibrary> {
    require_library()?;
    let mut info = sized!(ffi::LibraryInfo3d);
    call(|e| unsafe { ffi::iprop3d_library_query(&mut info, e) })?;
    Ok(ModelLibrary {
        api: (info.api_major, info.api_minor),
        max_vertices: info.max_vertices,
        max_triangles: info.max_triangles,
        max_textures: info.max_textures,
        max_texture_side: info.max_texture_side,
        max_texels: info.max_texels,
        max_lod: 7,
        texture_stamp: unsafe { ffi::iprop3d_texture_stamp() },
    })
}

/// The nominal size a model declares, in model units, its units per metre
/// and fuel budget.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct ModelInfo {
    pub size: [f32; 3],
    pub units_per_meter: f32,
    pub fuel: u64,
}

/// A 2D program a model generates as a texture.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct SkinProgram {
    pub name: String,
    pub width: u32,
    pub height: u32,
}

/// A loaded, validated `.iprop3d` file. One model may be generated from
/// several threads at once.
pub struct Model {
    raw: NonNull<ffi::Model>,
    info: ModelInfo,
    inputs: Vec<Input>,
    skin_programs: Vec<SkinProgram>,
    // Parameter sets are made one at a time; generations run in parallel.
    lock: Mutex<()>,
}

unsafe impl Send for Model {}
unsafe impl Sync for Model {}

impl Drop for Model {
    fn drop(&mut self) {
        unsafe { ffi::iprop3d_model_free(self.raw.as_ptr()) }
    }
}

/// A texture the host already holds, as a [`Texture`] reported it.
pub type KnownTexture = ffi::KnownTexture;

/// What to generate: a seed, a level of detail (0 is the whole model),
/// inputs by name, limits, and textures the host already holds, which the
/// generation charges and does not make again.
#[derive(Clone, Debug, Default, PartialEq)]
pub struct ModelRequest {
    pub seed: u32,
    pub lod: u32,
    pub inputs: Vec<(String, InputValue)>,
    pub limits: Limits,
    pub known: Vec<KnownTexture>,
}

impl ModelRequest {
    pub fn seed(seed: u32) -> Self {
        ModelRequest {
            seed,
            ..Default::default()
        }
    }

    pub fn lod(mut self, lod: u32) -> Self {
        self.lod = lod;
        self
    }

    pub fn input(mut self, name: impl Into<String>, value: InputValue) -> Self {
        self.inputs.push((name.into(), value));
        self
    }
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub enum AlphaMode {
    Opaque,
    /// Texels below the cutoff are not drawn.
    Cutout(f32),
    /// Mixed over what is behind by alpha: glass.
    Blend,
}

/// The triangles of one material, counterclockwise from outside, in model
/// units with +Y up and +Z toward the viewer.
#[derive(Clone, Debug, PartialEq)]
pub struct Surface {
    /// Into [`GeneratedModel::textures`].
    pub texture: usize,
    pub roughness: f32,
    pub metallic: f32,
    pub emission: f32,
    pub alpha: AlphaMode,
    /// The cutoff the skin holds, whatever the mode.
    pub alpha_cutoff: f32,
    pub two_sided: bool,
    /// Sample without filtering or mipmaps.
    pub crisp: bool,
    pub positions: Vec<[f32; 3]>,
    pub normals: Vec<[f32; 3]>,
    /// The direction of growing u and a sign w: w * cross(normal, tangent)
    /// points up the image.
    pub tangents: Vec<[f32; 4]>,
    /// (0, 0) is the upper left corner of the texture.
    pub uvs: Vec<[f32; 2]>,
    pub indices: Vec<u32>,
}

/// One generated texture. Equal keys mean equal images.
#[derive(Clone, Debug, PartialEq)]
pub struct Texture {
    pub key: u64,
    pub name: String,
    pub asset_width: u32,
    pub asset_height: u32,
    pub width: u32,
    pub height: u32,
    /// Sampled beyond 0..1: wrap it rather than clamp.
    pub repeats: bool,
    /// RGBA8 rows from the top; empty when `reused`.
    pub rgba: Vec<u8>,
    /// Normal and material maps; empty when `reused`.
    pub images: Vec<AuxImage>,
    pub fuel: u64,
    pub work: u64,
    /// Bit k for an auxiliary image of kind k.
    pub image_kinds: u32,
    /// Named known by the request: the host holds the pixels.
    pub reused: bool,
}

impl Texture {
    /// The record that names this texture known in a later request.
    pub fn known(&self) -> KnownTexture {
        KnownTexture {
            key: self.key,
            fuel: self.fuel,
            work: self.work,
            image_kinds: self.image_kinds,
            reserved: 0,
        }
    }

    /// The first auxiliary image of a kind.
    pub fn image(&self, kind: AuxKind) -> Option<&AuxImage> {
        self.images.iter().find(|image| image.kind == kind)
    }

    /// Whether the texture has an auxiliary image of a kind, held or not.
    pub fn has(&self, kind: AuxKind) -> bool {
        kind.raw() < 32 && self.image_kinds & (1 << kind.raw()) != 0
    }
}

/// An oriented box: centre, size along its own axes and those axes.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Box3 {
    pub center: [f32; 3],
    pub size: [f32; 3],
    pub axes: [[f32; 3]; 3],
}

/// A point or box output with what the generation wrote: nothing for a
/// singular output never written, one value, or any number for a list.
#[derive(Clone, Debug, PartialEq)]
pub enum ModelOutput {
    Points {
        name: String,
        list: bool,
        points: Vec<[f32; 3]>,
    },
    Boxes {
        name: String,
        list: bool,
        boxes: Vec<Box3>,
    },
}

impl ModelOutput {
    pub fn name(&self) -> &str {
        match self {
            ModelOutput::Points { name, .. } | ModelOutput::Boxes { name, .. } => name,
        }
    }
}

/// One generated model.
#[derive(Clone, Debug, PartialEq)]
pub struct GeneratedModel {
    pub bounds_min: [f32; 3],
    pub bounds_max: [f32; 3],
    pub size: [f32; 3],
    pub units_per_meter: f32,
    pub vertex_count: u32,
    pub triangle_count: u32,
    pub surfaces: Vec<Surface>,
    pub textures: Vec<Texture>,
    pub outputs: Vec<ModelOutput>,
    pub fuel_budget: u64,
    pub fuel_used: u64,
    pub work_budget: u64,
    pub work_used: u64,
}

impl GeneratedModel {
    pub fn output(&self, name: &str) -> Option<&ModelOutput> {
        self.outputs.iter().find(|output| output.name() == name)
    }
}

struct ModelResultHandle(NonNull<ffi::ModelResult>);

impl Drop for ModelResultHandle {
    fn drop(&mut self) {
        unsafe { ffi::iprop3d_result_free(self.0.as_ptr()) }
    }
}

fn chunks<const N: usize>(data: *const f32, count: usize) -> Vec<[f32; N]> {
    let flat = unsafe { copy(data, count * N) };
    flat.as_chunks::<N>().0.to_vec()
}

impl Model {
    /// Loads and validates a compiled model and every skin it embeds.
    pub fn load(bytes: &[u8]) -> Result<Model> {
        require_library()?;
        let mut raw = ptr::null_mut();
        call(|e| unsafe {
            ffi::iprop3d_model_load(bytes.as_ptr(), bytes.len() as u64, &mut raw, e)
        })?;
        let raw = NonNull::new(raw).ok_or_else(|| argument("the library returned no model"))?;
        let mut model = Model {
            raw,
            info: ModelInfo {
                size: [0.0; 3],
                units_per_meter: 0.0,
                fuel: 0,
            },
            inputs: Vec::new(),
            skin_programs: Vec::new(),
            lock: Mutex::new(()),
        };
        let mut info = sized!(ffi::ModelInfo);
        call(|e| unsafe { ffi::iprop3d_model_query(raw.as_ptr(), &mut info, e) })?;
        model.info = ModelInfo {
            size: [info.size_x, info.size_y, info.size_z],
            units_per_meter: info.units_per_meter,
            fuel: info.fuel,
        };
        for index in 0..info.input_count {
            let mut input = sized!(ffi::InputInfoEx);
            call(|e| unsafe { ffi::iprop3d_model_input_get(raw.as_ptr(), index, &mut input, e) })?;
            model.inputs.push(read_input(&input));
        }
        for index in 0..info.skin_program_count {
            let mut skin = sized!(ffi::SkinProgramInfo);
            call(|e| unsafe {
                ffi::iprop3d_model_skin_program(raw.as_ptr(), index, &mut skin, e)
            })?;
            model.skin_programs.push(SkinProgram {
                name: text(skin.name),
                width: skin.width,
                height: skin.height,
            });
        }
        Ok(model)
    }

    pub fn info(&self) -> ModelInfo {
        self.info
    }

    pub fn inputs(&self) -> &[Input] {
        &self.inputs
    }

    pub fn skin_programs(&self) -> &[SkinProgram] {
        &self.skin_programs
    }

    /// Generates one model. Inputs the request leaves out keep their
    /// defaults.
    pub fn generate(&self, request: &ModelRequest) -> Result<GeneratedModel> {
        let parameters = {
            let _guard = self
                .lock
                .lock()
                .unwrap_or_else(|poisoned| poisoned.into_inner());
            let mut raw = ptr::null_mut();
            call(|e| unsafe {
                ffi::iprop3d_model_parameters_create(self.raw.as_ptr(), &mut raw, e)
            })?;
            let mut parameters =
                Parameters(NonNull::new(raw).ok_or_else(|| argument("no parameter set"))?);
            for (name, value) in &request.inputs {
                parameters.set(name, *value)?;
            }
            parameters
        };
        let options = ffi::GenerateOptions3d {
            struct_size: size_of::<ffi::GenerateOptions3d>() as u32,
            flags: 0,
            seed: request.seed,
            lod: request.lod,
            known_textures: if request.known.is_empty() {
                ptr::null()
            } else {
                request.known.as_ptr()
            },
            known_texture_count: u32::try_from(request.known.len())
                .map_err(|_| argument("too many known textures"))?,
        };
        let limits = request.limits.raw();
        let mut raw = ptr::null_mut();
        call(|e| unsafe {
            ffi::iprop3d_generate(
                self.raw.as_ptr(),
                &options,
                parameters.0.as_ptr(),
                &limits,
                &mut raw,
                e,
            )
        })?;
        drop(parameters);
        let result = ModelResultHandle(
            NonNull::new(raw).ok_or_else(|| argument("the library returned no result"))?,
        );
        let r = result.0.as_ptr();

        let mut info = sized!(ffi::ResultInfo3d);
        call(|e| unsafe { ffi::iprop3d_result_query(r, &mut info, e) })?;

        let mut textures = Vec::with_capacity(info.texture_count as usize);
        for index in 0..info.texture_count {
            let mut view = sized!(ffi::TextureView);
            call(|e| unsafe { ffi::iprop3d_result_texture(r, index, &mut view, e) })?;
            if view.reused == 0 && view.pixel_format != ffi::PIXEL_FORMAT_RGBA8 {
                return Err(argument("a texture is not RGBA8"));
            }
            let mut images = Vec::with_capacity(view.image_count as usize);
            for image in 0..view.image_count {
                let mut aux = sized!(ffi::AuxImageViewEx);
                call(|e| unsafe {
                    ffi::iprop3d_result_texture_image(r, index, image, &mut aux, e)
                })?;
                images.push(read_aux(&aux)?);
            }
            textures.push(Texture {
                key: view.key,
                name: text(view.name),
                asset_width: view.asset_width,
                asset_height: view.asset_height,
                width: view.width,
                height: view.height,
                repeats: view.repeats != 0,
                rgba: unsafe { copy(view.pixels, view.byte_count as usize) },
                images,
                fuel: view.fuel,
                work: view.work,
                image_kinds: view.image_kinds,
                reused: view.reused != 0,
            });
        }

        let mut surfaces = Vec::with_capacity(info.surface_count as usize);
        for index in 0..info.surface_count {
            let mut view = sized!(ffi::SurfaceView);
            call(|e| unsafe { ffi::iprop3d_result_surface(r, index, &mut view, e) })?;
            let vertices = view.vertex_count as usize;
            surfaces.push(Surface {
                texture: view.texture_index as usize,
                roughness: view.roughness,
                metallic: view.metallic,
                emission: view.emission,
                alpha: match view.alpha_mode {
                    ffi::ALPHA_CUTOUT => AlphaMode::Cutout(view.alpha_cutoff),
                    ffi::ALPHA_BLEND => AlphaMode::Blend,
                    _ => AlphaMode::Opaque,
                },
                alpha_cutoff: view.alpha_cutoff,
                two_sided: view.two_sided != 0,
                crisp: view.crisp != 0,
                positions: chunks::<3>(view.positions, vertices),
                normals: chunks::<3>(view.normals, vertices),
                tangents: chunks::<4>(view.tangents, vertices),
                uvs: chunks::<2>(view.uvs, vertices),
                indices: unsafe { copy(view.indices, view.index_count as usize) },
            });
        }

        let mut outputs = Vec::with_capacity(info.output_count as usize);
        for index in 0..info.output_count {
            let mut view = sized!(ffi::OutputView3d);
            call(|e| unsafe { ffi::iprop3d_result_output(r, index, &mut view, e) })?;
            let name = text(view.name);
            let list = view.list != 0;
            let count = view.value_count as usize;
            outputs.push(if view.kind == ffi::OUTPUT_BOX3D {
                let boxes = chunks::<15>(view.values, count)
                    .into_iter()
                    .map(|v| Box3 {
                        center: [v[0], v[1], v[2]],
                        size: [v[3], v[4], v[5]],
                        axes: [
                            [v[6], v[7], v[8]],
                            [v[9], v[10], v[11]],
                            [v[12], v[13], v[14]],
                        ],
                    })
                    .collect();
                ModelOutput::Boxes { name, list, boxes }
            } else {
                ModelOutput::Points {
                    name,
                    list,
                    points: chunks::<3>(view.values, count),
                }
            });
        }

        Ok(GeneratedModel {
            bounds_min: info.bounds_min,
            bounds_max: info.bounds_max,
            size: info.size,
            units_per_meter: info.units_per_meter,
            vertex_count: info.vertex_count,
            triangle_count: info.triangle_count,
            surfaces,
            textures,
            outputs,
            fuel_budget: info.fuel_budget,
            fuel_used: info.fuel_used,
            work_budget: info.work_budget,
            work_used: info.work_used,
        })
    }
}
