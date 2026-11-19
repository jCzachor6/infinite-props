//! The plugin against the binding fixtures (IPROP_FIXTURES, see the iprop
//! crate's tests): conversions checked array for array, then a headless app
//! that loads files, builds a model and a sprite, shares textures and
//! reports failures.

use bevy::asset::AssetPlugin;
use bevy::mesh::{Indices, VertexAttributeValues};
use bevy::prelude::*;
use bevy::sprite::Anchor;
use bevy_iprop::iprop::{self, AuxKind, InputValue, ModelRequest, Request};
use bevy_iprop::*;
use std::path::{Path, PathBuf};

fn fixtures() -> Option<PathBuf> {
    match std::env::var_os("IPROP_FIXTURES") {
        Some(dir) => Some(PathBuf::from(dir)),
        None => {
            eprintln!("IPROP_FIXTURES is not set: skipped");
            None
        }
    }
}

fn model(dir: &Path, name: &str) -> iprop::Model {
    iprop::Model::load(&std::fs::read(dir.join(name)).unwrap()).unwrap()
}

fn floats<const N: usize>(values: &VertexAttributeValues) -> Vec<[f32; N]> {
    match (values, N) {
        (VertexAttributeValues::Float32x2(v), 2) => {
            v.iter().map(|a| a[..].try_into().unwrap()).collect()
        }
        (VertexAttributeValues::Float32x3(v), 3) => {
            v.iter().map(|a| a[..].try_into().unwrap()).collect()
        }
        (VertexAttributeValues::Float32x4(v), 4) => {
            v.iter().map(|a| a[..].try_into().unwrap()).collect()
        }
        _ => panic!("unexpected attribute format"),
    }
}

#[test]
fn a_surface_becomes_a_mesh_with_its_arrays_unchanged() {
    let Some(dir) = fixtures() else { return };
    for (name, seed) in [
        ("model-crate.iprop3d", 5),
        ("model-shapes.iprop3d", 3),
        ("model-sculpt.iprop3d", 1),
    ] {
        let generated = model(&dir, name)
            .generate(&ModelRequest::seed(seed))
            .unwrap();
        assert!(!generated.surfaces.is_empty(), "{name}");
        for surface in &generated.surfaces {
            let mesh = surface_mesh(surface);
            assert_eq!(
                floats::<3>(mesh.attribute(Mesh::ATTRIBUTE_POSITION).unwrap()),
                surface.positions,
                "{name}"
            );
            assert_eq!(
                floats::<3>(mesh.attribute(Mesh::ATTRIBUTE_NORMAL).unwrap()),
                surface.normals,
                "{name}"
            );
            assert_eq!(
                floats::<2>(mesh.attribute(Mesh::ATTRIBUTE_UV_0).unwrap()),
                surface.uvs,
                "{name}"
            );
            assert_eq!(
                floats::<4>(mesh.attribute(Mesh::ATTRIBUTE_TANGENT).unwrap()),
                surface.tangents,
                "{name}"
            );
            match mesh.indices().unwrap() {
                Indices::U32(indices) => assert_eq!(indices, &surface.indices, "{name}"),
                Indices::U16(_) => panic!("u32 indices expected"),
            }
        }
    }
}

#[test]
fn materials_follow_the_numbers_maps_and_modes_of_each_surface() {
    let Some(dir) = fixtures() else { return };
    let mut images = Assets::<Image>::default();
    let mut meshes = Assets::<Mesh>::default();
    let mut materials = Assets::<StandardMaterial>::default();
    let mut cache = IpropTextureCache::default();
    let mut modes = Vec::new();
    for (name, seed) in [
        ("model-materials.iprop3d", 1),
        ("model-materials.iprop3d", 3),
        ("model-crate.iprop3d", 5),
    ] {
        let generated = model(&dir, name)
            .generate(&ModelRequest::seed(seed))
            .unwrap();
        let parts = model_parts(
            &generated,
            &mut cache,
            &mut images,
            &mut meshes,
            &mut materials,
        );
        assert_eq!(parts.len(), generated.surfaces.len(), "{name}");
        for ((_, handle), surface) in parts.iter().zip(&generated.surfaces) {
            let material = materials.get(handle).unwrap();
            let texture = &generated.textures[surface.texture];
            assert_eq!(material.perceptual_roughness, surface.roughness, "{name}");
            assert_eq!(material.metallic, surface.metallic, "{name}");
            assert_eq!(material.double_sided, surface.two_sided, "{name}");
            assert_eq!(material.cull_mode.is_none(), surface.two_sided, "{name}");
            assert_eq!(
                material.normal_map_texture.is_some(),
                texture.has(AuxKind::Normal),
                "{name}"
            );
            assert_eq!(
                material.metallic_roughness_texture.is_some(),
                texture.has(AuxKind::Roughness) || texture.has(AuxKind::Metallic),
                "{name}"
            );
            assert_eq!(
                material.occlusion_texture.is_some(),
                texture.has(AuxKind::Ao),
                "{name}"
            );
            assert_eq!(
                material.emissive_texture.is_some(),
                surface.emission > 0.0,
                "{name}"
            );
            let mode = match (surface.alpha, material.alpha_mode) {
                (iprop::AlphaMode::Opaque, AlphaMode::Opaque) => "opaque",
                (iprop::AlphaMode::Cutout(a), AlphaMode::Mask(b)) if a == b => "cutout",
                (iprop::AlphaMode::Blend, AlphaMode::Blend) => "blend",
                (a, b) => panic!("{name}: {a:?} became {b:?}"),
            };
            modes.push(mode);
            // The colour is sRGB and sampled as the surface asks.
            let color = images
                .get(material.base_color_texture.as_ref().unwrap())
                .unwrap();
            assert_eq!(
                color.texture_descriptor.format,
                bevy::render::render_resource::TextureFormat::Rgba8UnormSrgb
            );
            assert_eq!(color.data.as_deref(), Some(&texture.rgba[..]), "{name}");
            let bevy::image::ImageSampler::Descriptor(sampler) = &color.sampler else {
                panic!("{name}: no sampler")
            };
            let wrap = if texture.repeats {
                bevy::image::ImageAddressMode::Repeat
            } else {
                bevy::image::ImageAddressMode::ClampToEdge
            };
            assert_eq!(
                (sampler.address_mode_u, sampler.address_mode_v),
                (wrap, wrap),
                "{name}"
            );
            let filter = if surface.crisp {
                bevy::image::ImageFilterMode::Nearest
            } else {
                bevy::image::ImageFilterMode::Linear
            };
            assert_eq!(sampler.mag_filter, filter, "{name}");
        }
    }
    // The materials fixture has glass (blend, two-sided) and a glowing lamp.
    assert!(
        modes.contains(&"opaque") && modes.contains(&"blend"),
        "{modes:?}"
    );
}

#[test]
fn the_packed_material_map_keeps_absent_maps_neutral() {
    let Some(dir) = fixtures() else { return };
    let maps = iprop::Program::load(&std::fs::read(dir.join("binding-maps.iprop")).unwrap())
        .unwrap()
        .generate(&Request::seed(5))
        .unwrap();
    let texture = iprop::Texture {
        key: 1,
        name: "maps".into(),
        asset_width: maps.width,
        asset_height: maps.height,
        width: maps.width,
        height: maps.height,
        repeats: false,
        rgba: maps.rgba.clone(),
        images: maps
            .images
            .iter()
            .filter(|m| m.kind != AuxKind::Metallic)
            .cloned()
            .collect(),
        fuel: 0,
        work: 0,
        image_kinds: 0,
        reused: false,
    };
    let packed = packed_material_map(&texture).unwrap();
    let ao = maps.image(AuxKind::Ao).unwrap();
    let roughness = maps.image(AuxKind::Roughness).unwrap();
    for i in 0..(maps.width * maps.height) as usize {
        assert_eq!(
            packed[i * 4..i * 4 + 4],
            [ao.rgba[i * 4], roughness.rgba[i * 4], 255, 255]
        );
    }
}

fn app(dir: &Path) -> App {
    let mut app = App::new();
    app.add_plugins((
        MinimalPlugins,
        AssetPlugin {
            file_path: dir.to_string_lossy().into_owned(),
            ..default()
        },
    ))
    .init_asset::<Image>()
    .init_asset::<Mesh>()
    .init_asset::<StandardMaterial>()
    .add_plugins(IpropPlugin);
    app
}

fn run_until(app: &mut App, done: impl Fn(&World) -> bool) {
    for _ in 0..500 {
        app.update();
        if done(app.world()) {
            return;
        }
        std::thread::sleep(std::time::Duration::from_millis(5));
    }
    panic!("the app did not finish in time");
}

#[test]
fn the_plugin_loads_files_and_builds_models_and_sprites() {
    let Some(dir) = fixtures() else { return };
    let mut app = app(&dir);
    let (model, program) = {
        let server = app.world().resource::<AssetServer>();
        (
            server.load::<IpropModel>("model-crate.iprop3d"),
            server.load::<IpropProgram>("bindings.iprop"),
        )
    };
    let crate_entity = app
        .world_mut()
        .spawn(IpropModelInstance {
            model: model.clone(),
            request: ModelRequest::seed(5),
        })
        .id();
    let request = Request::seed(4294967295)
        .input("tint", InputValue::Color([0x12, 0x34, 0x56, 0xff]))
        .input("amount", InputValue::F32(-0.0))
        .input("offset", InputValue::U32(4294967289));
    let sprite_entity = app.world_mut().spawn(IpropSprite { program, request }).id();
    run_until(&mut app, |world| {
        world.get::<IpropModelGenerated>(crate_entity).is_some()
            && world.get::<IpropSpriteImage>(sprite_entity).is_some()
    });

    // The model: one child per surface under a root scaled to metres.
    let expected = iprop::Model::load(&std::fs::read(dir.join("model-crate.iprop3d")).unwrap())
        .unwrap()
        .generate(&ModelRequest::seed(5))
        .unwrap();
    let generated = app
        .world()
        .get::<IpropModelGenerated>(crate_entity)
        .unwrap()
        .clone();
    assert_eq!(
        (generated.vertex_count, generated.triangle_count),
        (expected.vertex_count, expected.triangle_count)
    );
    let root = app.world().get::<Transform>(generated.root).unwrap();
    assert_eq!(root.scale, Vec3::splat(1.0 / 50.0));
    let children: Vec<Entity> = app
        .world()
        .get::<Children>(generated.root)
        .unwrap()
        .iter()
        .collect();
    assert_eq!(children.len(), expected.surfaces.len());
    let meshes = app.world().resource::<Assets<Mesh>>();
    for (child, surface) in children.iter().zip(&expected.surfaces) {
        let handle = &app.world().get::<Mesh3d>(*child).unwrap().0;
        assert_eq!(
            meshes.get(handle).unwrap().count_vertices(),
            surface.positions.len()
        );
        assert!(
            app.world()
                .get::<MeshMaterial3d<StandardMaterial>>(*child)
                .is_some()
        );
    }

    // The sprite: the fixture's image, anchored at the program's origin.
    let sprite = app
        .world()
        .get::<IpropSpriteImage>(sprite_entity)
        .unwrap()
        .clone();
    let image = app
        .world()
        .resource::<Assets<Image>>()
        .get(&sprite.image)
        .unwrap();
    assert_eq!(
        image.data.as_deref(),
        Some(&std::fs::read(dir.join("bindings.rgba")).unwrap()[..])
    );
    assert!(app.world().get::<Sprite>(sprite_entity).is_some());
    assert!(app.world().get::<Anchor>(sprite_entity).is_some());
    assert_eq!(
        sprite
            .outputs
            .iter()
            .find(|o| o.name == "marks")
            .unwrap()
            .values,
        [3.0, 4.0, 5.0, 6.0]
    );

    // A second crate generates no texture again: the cache names them known.
    let held = app.world().resource::<IpropTextureCache>().len();
    let images_before = app.world().resource::<Assets<Image>>().len();
    let second = app
        .world_mut()
        .spawn(IpropModelInstance {
            model: model.clone(),
            request: ModelRequest::seed(5),
        })
        .id();
    run_until(&mut app, |world| {
        world.get::<IpropModelGenerated>(second).is_some()
    });
    assert_eq!(app.world().resource::<IpropTextureCache>().len(), held);
    assert_eq!(app.world().resource::<Assets<Image>>().len(), images_before);

    // A change builds again and replaces the meshes; a failure is reported.
    app.world_mut()
        .get_mut::<IpropModelInstance>(second)
        .unwrap()
        .request = ModelRequest::seed(77);
    run_until(&mut app, |world| {
        world
            .get::<IpropModelGenerated>(second)
            .is_some_and(|g| g.vertex_count > 0)
    });
    let failing = app
        .world_mut()
        .spawn(IpropModelInstance {
            model,
            request: ModelRequest::seed(5).input("wide", InputValue::F32(0.0)),
        })
        .id();
    run_until(&mut app, |world| {
        world.get::<IpropFailed>(failing).is_some()
    });
    assert_eq!(
        app.world().get::<IpropFailed>(failing).unwrap().0.reason,
        3010
    );
}
