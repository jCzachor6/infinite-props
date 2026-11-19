//! A few props on the ground under the sun, turning slowly. Run it from the
//! repository with the binding fixtures as its assets:
//!
//! ```sh
//! IPROP_FIXTURES=$PWD/build/binding-fixtures cargo run --release \
//!     --manifest-path backends/bevy/Cargo.toml -p bevy_iprop --example showcase --features demo
//! ```
//!
//! With IPROP_SCREENSHOT set to a PNG path it saves one frame once every
//! model is built, then quits.

use bevy::prelude::*;
use bevy::render::view::screenshot::{Screenshot, save_to_disk};
use bevy_iprop::iprop::{InputValue, ModelRequest};
use bevy_iprop::{IpropModelGenerated, IpropModelInstance, IpropPlugin};

const PROPS: [(&str, u32, f32); 3] = [
    ("model-hydrant.iprop3d", 4, -1.5),
    ("model-bench.iprop3d", 2, 0.2),
    ("model-sandbags.iprop3d", 1, 2.3),
];

#[derive(Component)]
struct Turning;

fn main() {
    let assets = std::env::var("IPROP_FIXTURES").expect("set IPROP_FIXTURES to the binding fixtures");
    App::new()
        .add_plugins(
            DefaultPlugins
                .set(AssetPlugin { file_path: assets, ..default() })
                .set(WindowPlugin {
                    primary_window: Some(Window {
                        title: "Infinite Props in Bevy".into(),
                        resolution: (1280, 720).into(),
                        ..default()
                    }),
                    ..default()
                }),
        )
        .add_plugins(IpropPlugin)
        .insert_resource(ClearColor(Color::srgb(0.17, 0.19, 0.23)))
        .add_systems(Startup, setup)
        .add_systems(Update, (turn, capture))
        .run();
}

fn setup(
    mut commands: Commands,
    assets: Res<AssetServer>,
    mut meshes: ResMut<Assets<Mesh>>,
    mut materials: ResMut<Assets<StandardMaterial>>,
) {
    for (file, seed, x) in PROPS {
        let mut request = ModelRequest::seed(seed);
        if file == "model-hydrant.iprop3d" {
            request = request.input("paint", InputValue::Color([47, 111, 74, 255]));
        }
        commands.spawn((
            IpropModelInstance { model: assets.load(file), request },
            Transform::from_xyz(x, 0.0, 0.0).with_rotation(Quat::from_rotation_y(-0.5)),
            Turning,
        ));
    }
    commands.spawn((
        Mesh3d(meshes.add(Plane3d::default().mesh().size(24.0, 24.0))),
        MeshMaterial3d(materials.add(StandardMaterial {
            base_color: Color::srgb(0.32, 0.33, 0.35),
            perceptual_roughness: 0.9,
            ..default()
        })),
    ));
    commands.spawn((
        DirectionalLight { illuminance: 9000.0, shadow_maps_enabled: true, ..default() },
        Transform::from_xyz(-3.0, 6.0, 5.0).looking_at(Vec3::ZERO, Vec3::Y),
    ));
    commands.spawn((
        Camera3d::default(),
        Transform::from_xyz(0.3, 1.25, 3.3).looking_at(Vec3::new(0.3, 0.35, 0.0), Vec3::Y),
    ));
}

fn turn(time: Res<Time>, mut props: Query<&mut Transform, With<Turning>>) {
    if std::env::var_os("IPROP_SCREENSHOT").is_some() {
        return;
    }
    for mut transform in &mut props {
        transform.rotate_y(0.25 * time.delta_secs());
    }
}

fn capture(
    mut commands: Commands,
    built: Query<(), With<IpropModelGenerated>>,
    mut frames: Local<u32>,
    mut exit: MessageWriter<AppExit>,
) {
    let Some(path) = std::env::var_os("IPROP_SCREENSHOT") else { return };
    if built.iter().count() < PROPS.len() {
        return;
    }
    *frames += 1;
    if *frames == 30 {
        commands.spawn(Screenshot::primary_window()).observe(save_to_disk(path));
    }
    if *frames == 90 {
        exit.write(AppExit::Success);
    }
}
