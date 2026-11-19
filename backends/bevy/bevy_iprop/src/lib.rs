//! Infinite Props for Bevy (preview). [`IpropPlugin`] loads compiled
//! programs (`.iprop`) and models (`.iprop3d`) as assets and generates them
//! where an entity asks for it:
//!
//! - [`IpropSprite`] draws a program from a seed into an [`Image`] and shows
//!   it as a [`Sprite`] standing on the program's origin;
//! - [`IpropModelInstance`] builds a model from a seed into one child per
//!   material, each a [`Mesh3d`] with a [`StandardMaterial`] that wears the
//!   model's textures and their normal and material maps.
//!
//! Generation runs on the main thread when an entity's request is new or
//! changed. Textures are generated once per key and shared through
//! [`IpropTextureCache`]. The conversions are public, so a game can build
//! meshes and images from an [`iprop`] result itself.
//!
//! Models use the right-handed, +Y up axes Bevy uses; triangles are
//! counterclockwise seen from outside and UV (0, 0) is the upper left of the
//! image, as in glTF. A model's units become metres by the scale of its child.

use bevy::asset::io::Reader;
use bevy::asset::{AssetLoader, LoadContext, RenderAssetUsages};
use bevy::image::{ImageAddressMode, ImageSampler, ImageSamplerDescriptor};
use bevy::mesh::{Indices, PrimitiveTopology};
use bevy::prelude::*;
use bevy::render::render_resource::{Extent3d, Face, TextureDimension, TextureFormat};
use bevy::sprite::Anchor;
use std::collections::HashMap;
use std::fmt;
use std::sync::Arc;

pub use iprop;
use iprop::{AuxKind, GeneratedImage, GeneratedModel, KnownTexture, Surface, Texture};

/// Registers the program and model assets, their loaders, the texture cache
/// and the systems that generate sprites and models.
pub struct IpropPlugin;

impl Plugin for IpropPlugin {
    fn build(&self, app: &mut App) {
        app.init_asset::<IpropProgram>()
            .init_asset::<IpropModel>()
            .register_asset_loader(IpropProgramLoader)
            .register_asset_loader(IpropModelLoader)
            .init_resource::<IpropTextureCache>()
            .add_systems(
                Update,
                (
                    (refresh_sprites, generate_sprites).chain(),
                    (refresh_models, generate_models).chain(),
                ),
            );
    }
}

/// A loaded `.iprop` program.
#[derive(Asset, TypePath, Clone)]
pub struct IpropProgram(pub Arc<iprop::Program>);

/// A loaded `.iprop3d` model.
#[derive(Asset, TypePath, Clone)]
pub struct IpropModel(pub Arc<iprop::Model>);

/// Why a file did not load.
#[derive(Debug)]
pub enum IpropLoadError {
    Io(std::io::Error),
    Iprop(iprop::Error),
}

impl fmt::Display for IpropLoadError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            IpropLoadError::Io(error) => write!(f, "could not read the file: {error}"),
            IpropLoadError::Iprop(error) => write!(f, "{error}"),
        }
    }
}

impl std::error::Error for IpropLoadError {}

/// Loads `.iprop` files.
#[derive(Default, TypePath)]
pub struct IpropProgramLoader;

impl AssetLoader for IpropProgramLoader {
    type Asset = IpropProgram;
    type Settings = ();
    type Error = IpropLoadError;

    async fn load(
        &self,
        reader: &mut dyn Reader,
        _settings: &(),
        _context: &mut LoadContext<'_>,
    ) -> Result<IpropProgram, IpropLoadError> {
        let mut bytes = Vec::new();
        reader
            .read_to_end(&mut bytes)
            .await
            .map_err(IpropLoadError::Io)?;
        iprop::Program::load(&bytes)
            .map(|program| IpropProgram(Arc::new(program)))
            .map_err(IpropLoadError::Iprop)
    }

    fn extensions(&self) -> &[&str] {
        &["iprop"]
    }
}

/// Loads `.iprop3d` files.
#[derive(Default, TypePath)]
pub struct IpropModelLoader;

impl AssetLoader for IpropModelLoader {
    type Asset = IpropModel;
    type Settings = ();
    type Error = IpropLoadError;

    async fn load(
        &self,
        reader: &mut dyn Reader,
        _settings: &(),
        _context: &mut LoadContext<'_>,
    ) -> Result<IpropModel, IpropLoadError> {
        let mut bytes = Vec::new();
        reader
            .read_to_end(&mut bytes)
            .await
            .map_err(IpropLoadError::Io)?;
        iprop::Model::load(&bytes)
            .map(|model| IpropModel(Arc::new(model)))
            .map_err(IpropLoadError::Iprop)
    }

    fn extensions(&self) -> &[&str] {
        &["iprop3d"]
    }
}

/// A generation that failed, on the entity that asked for it. Changing the
/// request tries again.
#[derive(Component, Clone, Debug)]
pub struct IpropFailed(pub iprop::Error);

// ---- images -----------------------------------------------------------------------

/// How an image is sampled: unfiltered for pixel art and crisp skins, and
/// wrapped where a model repeats a texture.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub struct Sampling {
    pub crisp: bool,
    pub repeats: bool,
}

/// An RGBA8 image, rows from the top: sRGB for colour, linear for data such
/// as normal and material maps.
pub fn rgba_image(width: u32, height: u32, rgba: Vec<u8>, srgb: bool, sampling: Sampling) -> Image {
    let format = if srgb {
        TextureFormat::Rgba8UnormSrgb
    } else {
        TextureFormat::Rgba8Unorm
    };
    let mut image = Image::new(
        Extent3d {
            width,
            height,
            depth_or_array_layers: 1,
        },
        TextureDimension::D2,
        rgba,
        format,
        RenderAssetUsages::default(),
    );
    let mut sampler = if sampling.crisp {
        ImageSamplerDescriptor::nearest()
    } else {
        ImageSamplerDescriptor::linear()
    };
    let address = if sampling.repeats {
        ImageAddressMode::Repeat
    } else {
        ImageAddressMode::ClampToEdge
    };
    sampler.address_mode_u = address;
    sampler.address_mode_v = address;
    image.sampler = ImageSampler::Descriptor(sampler);
    image
}

/// The image of a generated program, unfiltered as pixel art wants it.
pub fn program_image(generated: &GeneratedImage) -> Image {
    rgba_image(
        generated.width,
        generated.height,
        generated.rgba.clone(),
        true,
        Sampling {
            crisp: true,
            repeats: false,
        },
    )
}

/// Occlusion in red, roughness in green and metallic in blue, as Bevy and
/// glTF read them; a map the texture lacks is 255, so its number stands.
pub fn packed_material_map(texture: &Texture) -> Option<Vec<u8>> {
    let map = |kind| {
        texture
            .image(kind)
            .filter(|m| m.width == texture.width && m.height == texture.height)
    };
    let (ao, roughness, metallic) = (
        map(AuxKind::Ao),
        map(AuxKind::Roughness),
        map(AuxKind::Metallic),
    );
    if ao.is_none() && roughness.is_none() && metallic.is_none() {
        return None;
    }
    let pixels = (texture.width * texture.height) as usize;
    let mut packed = vec![255u8; pixels * 4];
    for (channel, image) in [(0, ao), (1, roughness), (2, metallic)] {
        if let Some(image) = image {
            for i in 0..pixels {
                packed[i * 4 + channel] = image.rgba[i * 4];
            }
        }
    }
    Some(packed)
}

/// The images of one texture under one sampling.
#[derive(Clone, Debug)]
pub struct TextureImages {
    pub color: Handle<Image>,
    pub normal: Option<Handle<Image>>,
    /// Occlusion, roughness and metallic packed; see [`packed_material_map`].
    pub material: Option<Handle<Image>>,
    pub emission: Option<Handle<Image>>,
}

struct CachedTexture {
    texture: Arc<Texture>,
    images: HashMap<Sampling, TextureImages>,
}

/// The textures generated so far, by key: a model asks for each only once,
/// and every sampling of it is made once. Entries keep their images alive
/// until [`IpropTextureCache::clear`].
#[derive(Resource, Default)]
pub struct IpropTextureCache {
    textures: HashMap<u64, CachedTexture>,
}

impl IpropTextureCache {
    /// The records that name every held texture known to a generation.
    pub fn known(&self) -> Vec<KnownTexture> {
        let mut known: Vec<KnownTexture> = self
            .textures
            .values()
            .map(|cached| cached.texture.known())
            .collect();
        known.sort_by_key(|record| record.key);
        known
    }

    pub fn len(&self) -> usize {
        self.textures.len()
    }

    pub fn is_empty(&self) -> bool {
        self.textures.is_empty()
    }

    pub fn clear(&mut self) {
        self.textures.clear();
    }

    /// Keeps a texture that carries its pixels; a reused one is already held.
    pub fn insert(&mut self, texture: &Texture) {
        if !texture.reused {
            self.textures
                .entry(texture.key)
                .or_insert_with(|| CachedTexture {
                    texture: Arc::new(texture.clone()),
                    images: HashMap::new(),
                });
        }
    }

    /// The images of a held texture under a sampling, made on first use.
    pub fn images(
        &mut self,
        key: u64,
        sampling: Sampling,
        images: &mut Assets<Image>,
    ) -> Option<TextureImages> {
        let cached = self.textures.get_mut(&key)?;
        let texture = &cached.texture;
        let made = cached.images.entry(sampling).or_insert_with(|| {
            let (w, h) = (texture.width, texture.height);
            let mut map = |kind, srgb| {
                texture.image(kind).map(|m| {
                    images.add(rgba_image(
                        m.width,
                        m.height,
                        m.rgba.clone(),
                        srgb,
                        sampling,
                    ))
                })
            };
            let normal = map(AuxKind::Normal, false);
            let emission = map(AuxKind::Emission, true);
            TextureImages {
                color: images.add(rgba_image(w, h, texture.rgba.clone(), true, sampling)),
                normal,
                material: packed_material_map(texture)
                    .map(|packed| images.add(rgba_image(w, h, packed, false, sampling))),
                emission,
            }
        });
        Some(made.clone())
    }
}

// ---- meshes and materials -----------------------------------------------------------

/// One surface as a triangle list with positions, normals, UVs and
/// tangents, in model units.
pub fn surface_mesh(surface: &Surface) -> Mesh {
    let mut mesh = Mesh::new(
        PrimitiveTopology::TriangleList,
        RenderAssetUsages::default(),
    );
    mesh.insert_attribute(Mesh::ATTRIBUTE_POSITION, surface.positions.clone());
    mesh.insert_attribute(Mesh::ATTRIBUTE_NORMAL, surface.normals.clone());
    mesh.insert_attribute(Mesh::ATTRIBUTE_UV_0, surface.uvs.clone());
    mesh.insert_attribute(Mesh::ATTRIBUTE_TANGENT, surface.tangents.clone());
    mesh.insert_indices(Indices::U32(surface.indices.clone()));
    mesh
}

/// The material of a surface wearing a texture's images. The surface's
/// roughness and metallic multiply their maps, emission multiplies the
/// emission map or, without one, the colour.
pub fn surface_material(
    surface: &Surface,
    texture: &Texture,
    images: &TextureImages,
) -> StandardMaterial {
    let mut material = StandardMaterial {
        base_color: Color::WHITE,
        base_color_texture: Some(images.color.clone()),
        perceptual_roughness: surface.roughness,
        metallic: surface.metallic,
        normal_map_texture: images.normal.clone(),
        double_sided: surface.two_sided,
        cull_mode: if surface.two_sided {
            None
        } else {
            Some(Face::Back)
        },
        alpha_mode: match surface.alpha {
            iprop::AlphaMode::Opaque => AlphaMode::Opaque,
            iprop::AlphaMode::Cutout(cutoff) => AlphaMode::Mask(cutoff),
            iprop::AlphaMode::Blend => AlphaMode::Blend,
        },
        ..default()
    };
    if texture.has(AuxKind::Roughness) || texture.has(AuxKind::Metallic) {
        material.metallic_roughness_texture = images.material.clone();
    }
    if texture.has(AuxKind::Ao) {
        material.occlusion_texture = images.material.clone();
    }
    if surface.emission > 0.0 {
        let e = surface.emission;
        material.emissive = LinearRgba::rgb(e, e, e);
        material.emissive_texture = Some(
            images
                .emission
                .clone()
                .unwrap_or_else(|| images.color.clone()),
        );
    }
    material
}

/// The meshes and materials of a generated model, one pair per surface.
/// Its textures join the cache first; a reused one must be held there.
pub fn model_parts(
    generated: &GeneratedModel,
    cache: &mut IpropTextureCache,
    images: &mut Assets<Image>,
    meshes: &mut Assets<Mesh>,
    materials: &mut Assets<StandardMaterial>,
) -> Vec<(Handle<Mesh>, Handle<StandardMaterial>)> {
    for texture in &generated.textures {
        cache.insert(texture);
    }
    generated
        .surfaces
        .iter()
        .filter_map(|surface| {
            let texture = generated.textures.get(surface.texture)?;
            let sampling = Sampling {
                crisp: surface.crisp,
                repeats: texture.repeats,
            };
            let set = cache.images(texture.key, sampling, images)?;
            let held = cache.textures.get(&texture.key)?.texture.clone();
            Some((
                meshes.add(surface_mesh(surface)),
                materials.add(surface_material(surface, &held, &set)),
            ))
        })
        .collect()
}

// ---- sprites ----------------------------------------------------------------------

/// Draws a program on this entity: a [`Sprite`] of the generated image,
/// anchored at the program's origin. Change the request to draw again.
#[derive(Component, Clone, Debug)]
#[require(Transform, Visibility)]
pub struct IpropSprite {
    pub program: Handle<IpropProgram>,
    pub request: iprop::Request,
}

/// What [`IpropSprite`] made: the image, its normal map when the program
/// publishes one, and the program's outputs in pixels from the upper left.
#[derive(Component, Clone, Debug)]
pub struct IpropSpriteImage {
    pub image: Handle<Image>,
    pub normal: Option<Handle<Image>>,
    pub outputs: Vec<iprop::Output>,
}

fn refresh_sprites(mut commands: Commands, changed: Query<Entity, Changed<IpropSprite>>) {
    for entity in &changed {
        commands
            .entity(entity)
            .remove::<(IpropSpriteImage, IpropFailed)>();
    }
}

#[allow(clippy::type_complexity)]
fn generate_sprites(
    mut commands: Commands,
    pending: Query<(Entity, &IpropSprite), (Without<IpropSpriteImage>, Without<IpropFailed>)>,
    programs: Res<Assets<IpropProgram>>,
    mut images: ResMut<Assets<Image>>,
) {
    for (entity, sprite) in &pending {
        let Some(program) = programs.get(&sprite.program) else {
            continue;
        };
        match program.0.generate(&sprite.request) {
            Ok(generated) => {
                let info = program.0.info();
                let normal = generated.image(AuxKind::Normal).map(|m| {
                    images.add(rgba_image(
                        m.width,
                        m.height,
                        m.rgba.clone(),
                        false,
                        Sampling {
                            crisp: true,
                            repeats: false,
                        },
                    ))
                });
                let image = images.add(program_image(&generated));
                // The anchor is relative to the centre, +Y up.
                let anchor = Vec2::new(
                    info.origin[0] / generated.width as f32 - 0.5,
                    0.5 - info.origin[1] / generated.height as f32,
                );
                commands.entity(entity).insert((
                    Sprite::from_image(image.clone()),
                    Anchor(anchor),
                    IpropSpriteImage {
                        image,
                        normal,
                        outputs: generated.outputs,
                    },
                ));
            }
            Err(error) => {
                warn!("Infinite Props sprite failed: {error}");
                commands.entity(entity).insert(IpropFailed(error));
            }
        }
    }
}

// ---- models -----------------------------------------------------------------------

/// Builds a model on this entity: a child scaled from model units to metres
/// holds one [`Mesh3d`] per material. Change the request to build again.
/// Textures the [`IpropTextureCache`] holds are not generated again.
#[derive(Component, Clone, Debug)]
#[require(Transform, Visibility)]
pub struct IpropModelInstance {
    pub model: Handle<IpropModel>,
    pub request: iprop::ModelRequest,
}

/// What [`IpropModelInstance`] built: the child holding the meshes, the
/// model's outputs and bounds in model units, and its counters.
#[derive(Component, Clone, Debug)]
pub struct IpropModelGenerated {
    pub root: Entity,
    pub outputs: Vec<iprop::ModelOutput>,
    pub units_per_meter: f32,
    pub bounds_min: Vec3,
    pub bounds_max: Vec3,
    pub vertex_count: u32,
    pub triangle_count: u32,
}

fn refresh_models(
    mut commands: Commands,
    changed: Query<(Entity, Option<&IpropModelGenerated>), Changed<IpropModelInstance>>,
) {
    for (entity, generated) in &changed {
        if let Some(generated) = generated {
            commands.entity(generated.root).despawn();
        }
        commands
            .entity(entity)
            .remove::<(IpropModelGenerated, IpropFailed)>();
    }
}

#[allow(clippy::too_many_arguments, clippy::type_complexity)]
fn generate_models(
    mut commands: Commands,
    pending: Query<
        (Entity, &IpropModelInstance),
        (Without<IpropModelGenerated>, Without<IpropFailed>),
    >,
    models: Res<Assets<IpropModel>>,
    mut cache: ResMut<IpropTextureCache>,
    mut images: ResMut<Assets<Image>>,
    mut meshes: ResMut<Assets<Mesh>>,
    mut materials: ResMut<Assets<StandardMaterial>>,
) {
    for (entity, instance) in &pending {
        let Some(model) = models.get(&instance.model) else {
            continue;
        };
        let mut request = instance.request.clone();
        let held = cache.known();
        request.known.extend(
            held.into_iter()
                .filter(|record| !instance.request.known.iter().any(|k| k.key == record.key)),
        );
        match model.0.generate(&request) {
            Ok(generated) => {
                let parts = model_parts(
                    &generated,
                    &mut cache,
                    &mut images,
                    &mut meshes,
                    &mut materials,
                );
                let scale = if generated.units_per_meter > 0.0 {
                    1.0 / generated.units_per_meter
                } else {
                    1.0
                };
                let root = commands
                    .spawn((
                        Transform::from_scale(Vec3::splat(scale)),
                        Visibility::default(),
                        ChildOf(entity),
                    ))
                    .id();
                for (mesh, material) in parts {
                    commands.spawn((Mesh3d(mesh), MeshMaterial3d(material), ChildOf(root)));
                }
                commands.entity(entity).insert(IpropModelGenerated {
                    root,
                    outputs: generated.outputs,
                    units_per_meter: generated.units_per_meter,
                    bounds_min: Vec3::from_array(generated.bounds_min),
                    bounds_max: Vec3::from_array(generated.bounds_max),
                    vertex_count: generated.vertex_count,
                    triangle_count: generated.triangle_count,
                });
            }
            Err(error) => {
                warn!("Infinite Props model failed: {error}");
                commands.entity(entity).insert(IpropFailed(error));
            }
        }
    }
}
