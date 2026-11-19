//! The binding fixtures every native binding checks itself against, made by
//! `backends/test/prepare-bindings.py` from the tools of the same build:
//! the 2D programs with their expected images and outputs, and the binary
//! model corpus (`model-corpus.bin`, IPMCRP01) of `ipgen --dump`. Set
//! IPROP_FIXTURES to that directory; without it the tests say so and pass.

use iprop::{
    AlphaMode, AuxKind, InputValue, Limits, Model, ModelOutput, ModelRequest, Program, Request,
};
use std::path::{Path, PathBuf};
use std::sync::Arc;

fn fixtures() -> Option<PathBuf> {
    match std::env::var_os("IPROP_FIXTURES") {
        Some(dir) => Some(PathBuf::from(dir)),
        None => {
            eprintln!("IPROP_FIXTURES is not set: skipped");
            None
        }
    }
}

fn read(dir: &Path, name: &str) -> Vec<u8> {
    std::fs::read(dir.join(name)).unwrap_or_else(|e| panic!("{name}: {e}"))
}

fn fnv(bytes: &[u8]) -> u64 {
    bytes.iter().fold(0xcbf29ce484222325u64, |h, &b| {
        (h ^ b as u64).wrapping_mul(0x100000001b3)
    })
}

struct Reader<'a> {
    data: &'a [u8],
    at: usize,
}

impl<'a> Reader<'a> {
    fn take(&mut self, n: usize) -> &'a [u8] {
        let slice = &self.data[self.at..self.at + n];
        self.at += n;
        slice
    }
    fn word(&mut self) -> u32 {
        u32::from_le_bytes(self.take(4).try_into().unwrap())
    }
    fn wide(&mut self) -> u64 {
        u64::from_le_bytes(self.take(8).try_into().unwrap())
    }
    fn blob(&mut self) -> &'a [u8] {
        let n = self.word() as usize;
        self.take(n)
    }
}

fn bytes_of<T: Copy, const N: usize>(values: &[[T; N]]) -> Vec<u8> {
    let size = size_of::<T>() * N * values.len();
    unsafe { std::slice::from_raw_parts(values.as_ptr() as *const u8, size) }.to_vec()
}

#[test]
fn every_case_of_the_model_corpus_is_the_native_dump() {
    let Some(dir) = fixtures() else { return };
    let data = read(&dir, "model-corpus.bin");
    let summary = String::from_utf8(read(&dir, "model-corpus.json")).unwrap();
    let mut r = Reader { data: &data, at: 0 };
    assert_eq!(r.take(8), b"IPMCRP01");
    let models: Vec<Model> = (0..r.word())
        .map(|_| Model::load(r.blob()).expect("a corpus model loads"))
        .collect();
    let cases = r.word();
    assert!(
        summary.contains(&format!("\"cases\": {cases}")),
        "the summary counts {cases} cases"
    );
    for _ in 0..cases {
        let label = String::from_utf8(r.blob().to_vec()).unwrap();
        let (index, seed, lod, count) = (r.word(), r.word(), r.word(), r.word());
        let mut request = ModelRequest::seed(seed).lod(lod);
        for _ in 0..count {
            let key = String::from_utf8(r.blob().to_vec()).unwrap();
            let (kind, value) = (r.word(), r.word());
            let value = match kind {
                0 => InputValue::Color(value.to_le_bytes()),
                1 => InputValue::F32(f32::from_bits(value)),
                _ => InputValue::U32(value),
            };
            request.inputs.push((key, value));
        }
        let model = &models[index as usize];
        let result = model
            .generate(&request)
            .unwrap_or_else(|e| panic!("{label}: {e}"));
        assert_eq!(
            result,
            model.generate(&request).unwrap(),
            "{label}: a second generation"
        );
        let floats: Vec<u32> = result
            .size
            .iter()
            .chain([result.units_per_meter].iter())
            .chain(result.bounds_min.iter())
            .chain(result.bounds_max.iter())
            .map(|f| f.to_bits())
            .collect();
        let expected: Vec<u32> = (0..10).map(|_| r.word()).collect();
        assert_eq!(floats, expected, "{label}: size and bounds");
        assert_eq!(
            (result.vertex_count, result.triangle_count),
            (r.word(), r.word()),
            "{label}"
        );
        assert_eq!(
            (
                result.fuel_budget,
                result.fuel_used,
                result.work_budget,
                result.work_used
            ),
            (r.wide(), r.wide(), r.wide(), r.wide()),
            "{label}: counters"
        );
        assert_eq!(result.textures.len() as u32, r.word(), "{label}: textures");
        for t in &result.textures {
            assert_eq!((t.key, t.name.as_bytes()), (r.wide(), r.blob()), "{label}");
            assert_eq!(
                (
                    t.width,
                    t.height,
                    t.asset_width,
                    t.asset_height,
                    t.repeats as u32
                ),
                (r.word(), r.word(), r.word(), r.word(), r.word()),
                "{label}: {}",
                t.name
            );
            assert_eq!(fnv(&t.rgba), r.wide(), "{label}: {} pixels", t.name);
            assert_eq!(
                t.images.len() as u32,
                r.word(),
                "{label}: {} images",
                t.name
            );
            for image in &t.images {
                assert_eq!(
                    (image.kind.raw(), image.name.as_bytes()),
                    (r.word(), r.blob()),
                    "{label}"
                );
                assert_eq!(
                    fnv(&image.rgba),
                    r.wide(),
                    "{label}: {} {}",
                    t.name,
                    image.name
                );
            }
        }
        assert_eq!(result.surfaces.len() as u32, r.word(), "{label}: surfaces");
        for s in &result.surfaces {
            let (mode, cutoff) = match s.alpha {
                AlphaMode::Opaque => (0, s.alpha_cutoff),
                AlphaMode::Cutout(c) => (1, c),
                AlphaMode::Blend => (2, s.alpha_cutoff),
            };
            let head = [
                s.texture as u32,
                s.roughness.to_bits(),
                s.metallic.to_bits(),
                s.emission.to_bits(),
                mode,
                cutoff.to_bits(),
                s.two_sided as u32,
                s.crisp as u32,
            ];
            let expected: Vec<u32> = (0..8).map(|_| r.word()).collect();
            assert_eq!(head.to_vec(), expected, "{label}: material");
            assert_eq!(bytes_of(&s.positions), r.blob(), "{label}: positions");
            assert_eq!(bytes_of(&s.normals), r.blob(), "{label}: normals");
            assert_eq!(bytes_of(&s.tangents), r.blob(), "{label}: tangents");
            assert_eq!(bytes_of(&s.uvs), r.blob(), "{label}: uvs");
            let indices: Vec<u8> = s.indices.iter().flat_map(|i| i.to_le_bytes()).collect();
            assert_eq!(indices, r.blob(), "{label}: indices");
        }
        assert_eq!(result.outputs.len() as u32, r.word(), "{label}: outputs");
        for o in &result.outputs {
            let (kind, list, values): (u32, bool, Vec<f32>) = match o {
                ModelOutput::Points { list, points, .. } => {
                    (0, *list, points.iter().flatten().copied().collect())
                }
                ModelOutput::Boxes { list, boxes, .. } => (
                    1,
                    *list,
                    boxes
                        .iter()
                        .flat_map(|b| {
                            b.center
                                .into_iter()
                                .chain(b.size)
                                .chain(b.axes.into_iter().flatten())
                        })
                        .collect(),
                ),
            };
            let count = match o {
                ModelOutput::Points { points, .. } => points.len(),
                ModelOutput::Boxes { boxes, .. } => boxes.len(),
            } as u32;
            assert_eq!(
                (o.name().as_bytes(), kind, list as u32, count),
                (r.blob(), r.word(), r.word(), r.word()),
                "{label}"
            );
            let bits: Vec<u8> = values
                .iter()
                .flat_map(|v| v.to_bits().to_le_bytes())
                .collect();
            assert_eq!(bits, r.blob(), "{label}: {} values", o.name());
        }
    }
    assert_eq!(r.at, data.len(), "the whole corpus was read");
}

#[test]
fn a_model_describes_itself_and_its_skin_is_the_2d_generation() {
    let Some(dir) = fixtures() else { return };
    let library = iprop::model_library().unwrap();
    assert_eq!(
        (
            library.api,
            library.max_vertices,
            library.max_triangles,
            library.max_textures,
            library.max_texture_side,
            library.max_texels
        ),
        ((0, 3), 65536, 131072, 32, 2048, 4194304)
    );
    assert_ne!(library.texture_stamp, 0);
    let crate_model = Model::load(&read(&dir, "model-crate.iprop3d")).unwrap();
    assert_eq!(crate_model.info().size, [12.0, 10.0, 8.0]);
    assert_eq!(crate_model.info().units_per_meter, 50.0);
    let inputs: Vec<(String, InputValue)> = crate_model
        .inputs()
        .iter()
        .map(|i| (i.name.clone(), i.default))
        .collect();
    assert_eq!(
        inputs,
        vec![
            ("wide".into(), InputValue::F32(8.0)),
            ("stain".into(), InputValue::Color([51, 102, 153, 255])),
            ("extra".into(), InputValue::U32(1)),
            ("rounding".into(), InputValue::F32(0.5)),
        ]
    );
    let skin = &crate_model.skin_programs()[0];
    assert_eq!(
        (skin.name.as_str(), skin.width, skin.height),
        ("tile", 16, 8)
    );

    // The crate's first texture is what the 2D API makes from the same
    // program, seed, size and inputs.
    let texture = crate_model
        .generate(&ModelRequest::seed(5))
        .unwrap()
        .textures[0]
        .clone();
    let tile = Program::load(&read(&dir, "model-tile.iprop")).unwrap();
    let request = Request {
        seed: 77,
        size: Some((32, 16)),
        ..Default::default()
    }
    .input("tint", InputValue::Color([51, 102, 153, 255]));
    let image = tile.generate(&request).unwrap();
    assert_eq!((texture.width, texture.height), (image.width, image.height));
    assert_eq!(texture.rgba, image.rgba);
    assert_eq!(texture.images, image.images);
}

#[test]
fn a_program_generates_its_fixture_with_maps_and_outputs() {
    let Some(dir) = fixtures() else { return };
    let program = Program::load(&read(&dir, "bindings.iprop")).unwrap();
    let names: Vec<&str> = program.inputs().iter().map(|i| i.name.as_str()).collect();
    assert_eq!(names, ["tint", "amount", "offset"]);
    assert_eq!(
        program.inputs()[0].default,
        InputValue::Color([0xaa, 0xbb, 0xcc, 0xff])
    );
    let request = Request::seed(4294967295)
        .input("tint", InputValue::Color([0x12, 0x34, 0x56, 0xff]))
        .input("amount", InputValue::F32(-0.0))
        .input("offset", InputValue::U32(4294967289));
    let image = program.generate(&request).unwrap();
    assert_eq!((image.width, image.height), (8, 4));
    assert_eq!(image.rgba, read(&dir, "bindings.rgba"));
    assert_eq!(image.fuel_used, 76);
    let anchor = &image.outputs[0];
    assert_eq!((anchor.name.as_str(), anchor.stride), ("anchor", 2));
    assert_eq!(
        anchor
            .values
            .iter()
            .map(|v| v.to_bits())
            .collect::<Vec<_>>(),
        [2147483648, 3235905536]
    );
    let marks = image.outputs.iter().find(|o| o.name == "marks").unwrap();
    assert!(marks.list);
    assert_eq!(marks.values, [3.0, 4.0, 5.0, 6.0]);
    assert!(
        image
            .outputs
            .iter()
            .find(|o| o.name == "unset")
            .unwrap()
            .values
            .is_empty()
    );

    let maps = Program::load(&read(&dir, "binding-maps.iprop"))
        .unwrap()
        .generate(&Request::seed(5))
        .unwrap();
    assert_eq!(maps.rgba, read(&dir, "binding-maps.rgba"));
    for (kind, name) in [
        (AuxKind::Normal, "normal"),
        (AuxKind::Roughness, "roughness"),
        (AuxKind::Metallic, "metallic"),
        (AuxKind::Ao, "ao"),
        (AuxKind::Emission, "emission"),
    ] {
        assert_eq!(
            maps.image(kind).unwrap().rgba,
            read(&dir, &format!("binding-maps.{name}.rgba")),
            "{name}"
        );
    }
}

#[test]
fn failures_carry_their_reason_and_leave_the_handles_usable() {
    let Some(dir) = fixtures() else { return };
    let model_bytes = read(&dir, "model-crate.iprop3d");
    let program_bytes = read(&dir, "model-tile.iprop");
    // A file of the other kind fails while loading (bad magic).
    assert_eq!(
        Model::load(&program_bytes)
            .err()
            .map(|e| (e.status, e.reason)),
        Some((2, 1002))
    );
    assert_eq!(
        Program::load(&model_bytes)
            .err()
            .map(|e| (e.status, e.reason)),
        Some((2, 1002))
    );
    assert_eq!(Model::load(&[]).err().map(|e| e.status), Some(2));

    let model = Model::load(&model_bytes).unwrap();
    let broken = model
        .generate(&ModelRequest::seed(5).input("wide", InputValue::F32(0.0)))
        .unwrap_err();
    assert_eq!((broken.status, broken.reason, broken.stage), (3, 3010, 4));
    assert!(
        broken.message.contains("box size must be positive"),
        "{}",
        broken.message
    );
    let skin = model
        .generate(&ModelRequest::seed(5).input("rounding", InputValue::F32(100.0)))
        .unwrap_err();
    assert!(
        skin.message.starts_with("skin 'tile': "),
        "{}",
        skin.message
    );
    let starved = ModelRequest {
        limits: Limits {
            work_limit: 1,
            timeout: None,
        },
        ..ModelRequest::seed(5)
    };
    assert_eq!(model.generate(&starved).unwrap_err().reason, 3015);
    assert_eq!(
        model
            .generate(&ModelRequest::seed(5).input("nothing", InputValue::U32(1)))
            .unwrap_err()
            .reason,
        4005
    );
    assert_eq!(
        model
            .generate(&ModelRequest::seed(5).input("wide", InputValue::U32(1)))
            .unwrap_err()
            .reason,
        4004
    );
    assert_eq!(
        model
            .generate(&ModelRequest::seed(5).lod(8))
            .unwrap_err()
            .status,
        4
    );
    // The model still generates, with the default work limit of a model.
    let fine = model.generate(&ModelRequest::seed(5)).unwrap();
    assert_eq!(fine.work_budget, 600_000_000);

    let debug = Model::load(&read(&dir, "model-crate-debug.iprop3d")).unwrap();
    let located = debug
        .generate(&ModelRequest::seed(5).input("wide", InputValue::F32(0.0)))
        .unwrap_err();
    assert!(located.debug_line > 0, "a debug build names the line");

    let trap = Program::load(&read(&dir, "binding-trap.iprop"))
        .unwrap()
        .generate(&Request::seed(1))
        .unwrap_err();
    assert_eq!(trap.status, 3);
}

#[test]
fn levels_and_known_textures_follow_the_model_api() {
    let Some(dir) = fixtures() else { return };
    let model = Model::load(&read(&dir, "model-lod.iprop3d")).unwrap();
    let levels: Vec<_> = (0..4)
        .map(|lod| model.generate(&ModelRequest::seed(3).lod(lod)).unwrap())
        .collect();
    assert!(levels[1].vertex_count < levels[0].vertex_count);
    assert!(levels[2].vertex_count < levels[1].vertex_count);
    assert_eq!(levels[3].vertex_count, levels[2].vertex_count);
    for level in &levels {
        assert_eq!(level.outputs, levels[0].outputs);
        let keys: Vec<u64> = level.textures.iter().map(|t| t.key).collect();
        assert_eq!(
            keys,
            levels[0].textures.iter().map(|t| t.key).collect::<Vec<_>>()
        );
    }
    // Known textures are charged, not made: the same geometry and counters,
    // no pixels.
    let known: Vec<_> = levels[0].textures.iter().map(|t| t.known()).collect();
    let reused = model
        .generate(&ModelRequest {
            known,
            ..ModelRequest::seed(3)
        })
        .unwrap();
    assert!(
        reused
            .textures
            .iter()
            .all(|t| t.reused && t.rgba.is_empty() && t.images.is_empty())
    );
    assert_eq!(reused.surfaces, levels[0].surfaces);
    assert_eq!(
        (reused.fuel_used, reused.work_used),
        (levels[0].fuel_used, levels[0].work_used)
    );
}

#[test]
fn one_model_generates_on_eight_threads_as_it_does_on_one() {
    let Some(dir) = fixtures() else { return };
    let model = Arc::new(Model::load(&read(&dir, "model-lod.iprop3d")).unwrap());
    let requests: Vec<ModelRequest> = (0..48)
        .map(|i| ModelRequest::seed(100 + i / 3).lod(i % 3))
        .collect();
    let serial: Vec<_> = requests
        .iter()
        .map(|r| model.generate(r).unwrap())
        .collect();
    let threads: Vec<_> = (0..8)
        .map(|t| {
            let model = Arc::clone(&model);
            let requests = requests.clone();
            std::thread::spawn(move || {
                requests
                    .iter()
                    .enumerate()
                    .filter(|(i, _)| i % 8 == t)
                    .map(|(i, r)| (i, model.generate(r).unwrap()))
                    .collect::<Vec<_>>()
            })
        })
        .collect();
    for thread in threads {
        for (i, result) in thread.join().unwrap() {
            assert_eq!(result, serial[i], "request {i}");
        }
    }
}
