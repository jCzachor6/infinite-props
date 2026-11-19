//! Links the static C API of programs and models. By default it builds them
//! with CMake from the repository this crate lives in (a runtime-only build
//! with the model libraries); IPROP_LIB_DIR names a directory that already
//! holds the four static libraries instead.

use std::env;
use std::fs;
use std::path::{Path, PathBuf};

fn main() {
    println!("cargo:rerun-if-env-changed=IPROP_LIB_DIR");
    let target = env::var("TARGET").expect("cargo sets TARGET");
    let msvc = target.contains("msvc");
    // In link order: each library before the ones it uses.
    let names: [&str; 4] = if msvc {
        [
            "iprop3dc_static",
            "ipropc_static",
            "iprop_model",
            "iprop_runtime",
        ]
    } else {
        ["iprop3dc", "ipropc", "iprop_model", "iprop_runtime"]
    };

    let mut zlib = None;
    let directories = if let Some(dir) = env::var_os("IPROP_LIB_DIR") {
        vec![PathBuf::from(dir)]
    } else {
        let manifest =
            PathBuf::from(env::var("CARGO_MANIFEST_DIR").expect("cargo sets CARGO_MANIFEST_DIR"));
        let root = manifest.join("../../..").canonicalize().expect(
            "without IPROP_LIB_DIR the crate builds the C API from its infinite-props checkout",
        );
        for part in [
            "CMakeLists.txt",
            "cmake",
            "runtime",
            "model",
            "backends/capi",
            "backends/capi3d",
        ] {
            println!("cargo:rerun-if-changed={}", root.join(part).display());
        }
        let out = cmake::Config::new(&root)
            .define("IPROP_RUNTIME_ONLY", "ON")
            .define("IPROP_RUNTIME_MODELS", "ON")
            .define("IPROP_SFML", "OFF")
            .define("IPROP_GODOT", "OFF")
            .profile("Release")
            .build_target("iprop_capi3d")
            .build();
        let build = out.join("build");
        if msvc {
            zlib = cached_zlib(&build.join("CMakeCache.txt"));
        }
        let mut found = Vec::new();
        for name in names {
            let file = if msvc {
                format!("{name}.lib")
            } else {
                format!("lib{name}.a")
            };
            let path =
                find(&build, &file).unwrap_or_else(|| panic!("the CMake build made no {file}"));
            found.push(
                path.parent()
                    .expect("a library has a directory")
                    .to_path_buf(),
            );
        }
        found.sort();
        found.dedup();
        found
    };

    for dir in &directories {
        println!("cargo:rustc-link-search=native={}", dir.display());
    }
    for name in names {
        println!("cargo:rustc-link-lib=static={name}");
    }
    match zlib {
        Some(path) => {
            println!(
                "cargo:rustc-link-search=native={}",
                path.parent().expect("zlib has a directory").display()
            );
            let stem = path
                .file_stem()
                .expect("zlib has a name")
                .to_string_lossy()
                .into_owned();
            println!("cargo:rustc-link-lib={stem}");
        }
        None if msvc => println!("cargo:rustc-link-lib=zlib"),
        None => println!("cargo:rustc-link-lib=z"),
    }
    if target.contains("apple") {
        println!("cargo:rustc-link-lib=c++");
    } else if !msvc {
        println!("cargo:rustc-link-lib=stdc++");
    }
}

fn find(dir: &Path, file: &str) -> Option<PathBuf> {
    for entry in fs::read_dir(dir).ok()?.flatten() {
        let path = entry.path();
        if path.is_dir() {
            if let Some(found) = find(&path, file) {
                return Some(found);
            }
        } else if path.file_name().is_some_and(|name| name == file) {
            return Some(path);
        }
    }
    None
}

// The zlib the CMake build found, for MSVC, which has no system zlib.
fn cached_zlib(cache: &Path) -> Option<PathBuf> {
    let text = fs::read_to_string(cache).ok()?;
    for key in ["ZLIB_LIBRARY_RELEASE:FILEPATH=", "ZLIB_LIBRARY:FILEPATH="] {
        if let Some(line) = text.lines().find(|line| line.starts_with(key)) {
            let path = PathBuf::from(&line[key.len()..]);
            if path.is_file() {
                return Some(path);
            }
        }
    }
    None
}
