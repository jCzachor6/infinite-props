# Interfejs modeli w pięciu wiązaniach: Python, Swift, C#/Unity, Kotlin/Android, SFML

Status: **propozycja z 2026-11-18** dla wykonawcy (Codex), na stanie commita
`4f1521b` gałęzi `feat/iprop3d` (M5, C API modeli 0.3). W drzewie trwa M6
(roboczy rozdział 20 [wizji](2026-11-16-iprop3d.md), jeszcze poza commitem);
według niego hosty i ich API się nie zmieniają, więc plan go pomija i nie
dotyka jego plików. **Żadna decyzja z rozdziału 3 nie ma zgody użytkownika:
to moje propozycje.** Każde zdanie o kodzie podaje plik; czego z kodu nie
ustaliłem, jest pytaniem w rozdziale 7. Kontrakt modeli to
[`spec/models-v1.2.md`](../../spec/models-v1.2.md), podgląd do wydania 1.2.
Commit, push i CI tylko na słowo użytkownika (wizja 14.2); drzewo jest
współdzielone z M6, więc własny katalog budowania.

## 1. Cel i zakres

Modele mają dziś trzy hosty: C ([`iprop3d.h`](../../backends/capi3d/iprop3d.h)),
przeglądarkę (`backends/html5/js/api3d.js`) i Godota
(`backends/godot/src/iprop_model_godot.hpp`). Pięć wiązań z tytułu ma tylko
2D, co [`docs/native-bindings.md`](../native-bindings.md) mówi w sekcji
„Models (preview)”. Cel: każde dostaje interfejs modeli **obok** interfejsu
2D, który zostaje bez zmian. **Interfejs modeli** to komplet sześciu rzeczy:

| # | Element | Co opakowuje |
| --- | --- | --- |
| 1 | załadowanie `.iprop3d` z bajtów, walidacja raz | `iprop3d_model_load`, `iprop3d_model_free` |
| 2 | opis pliku: rozmiar nominalny, `units_per_meter`, paliwo, wejścia, programy skór | `iprop3d_model_query`, `iprop3d_model_skin_program`, `iprop3d_model_input_count/get` |
| 3 | generacja: seed, wejścia po nazwie (typy jak w 2D), poziom szczegółowości 0–7, limit pracy, termin, anulowanie, tekstury znane hostowi | `iprop3d_model_parameters_create`, `iprop_parameters_set_*_named`, `iprop3d_generate` |
| 4 | wynik: granice, liczniki, powierzchnie (liczby materiału i pięć tablic), tekstury z mapami, kosztem i flagą `reused`, wyjścia `point3d` i `box3d` | `iprop3d_result_query`, `_surface`, `_texture`, `_texture_image`, `_output` |
| 5 | błędy typem znanym z 2D: status, powód, etap, położenie; błąd w teksturze z aliasem skóry w komunikacie | `iprop_error_ex` |
| 6 | wersja API, limity generacji, stempel tekstur | `iprop3d_library_query`, `iprop3d_texture_stamp` |

**Etap A, rdzeń (wszystkie pięć):** elementy 1–6, pomocnik pamięci tekstur,
generacja jednego modelu z wielu wątków, testy zgodności z `ipgen --dump`,
budowa, pakiet, dokumentacja. **Etap B, pomocniki (po rdzeniu wiązania):**

| Wiązanie | Pomocnik | Zalecenie |
| --- | --- | --- |
| Python | `ModelTexture.to_pillow()` | tak |
| Swift | `makeCGImage()` i `makeMetalTexture(device:)` tekstur modelu; `SCNGeometry` i `SCNMaterial` (SceneKit) | tak; SceneKit z bramką obrazem (pytanie 6 w rozdz. 7) |
| C#/Unity | `ModelTexture.ToTexture2D()`, `GeneratedModel.ToMesh()` | tak, z bramką: testy Edytora u użytkownika, bo Edytor nie działa w CI (`TODO.md`) |
| Kotlin/Android | `ModelTexture.toBitmap()` | tak |
| SFML | tekstury modelu w `sf::Texture`; tryb `iprop_sfml --model-textures KATALOG` | tak |

**Poza zakresem:** `Material` w Unity (zależy od potoku renderowania i liczy
gładkość w miejsce chropowatości: to moja wiedza o Unity, nie fakt
z repozytorium; do tego Edytor nie działa w CI), importer `.iprop3d` w Unity,
`LODGroup`, `SCNLevelOfDetail`, `MDLMesh`, RealityKit, okno 3D w SFML, pliki
`IPTX` w wiązaniach, API asynchroniczne, modele w CLI `iprop-python`, każda
zmiana w `libipropc`, `iprop.h` i kontrakcie modeli.

**Interfejs modeli w SFML.** SFML jest biblioteką 2D, a ten backend to
wzorcowy klej C++ nad C API (`backends/sfml/src/Host.hpp` idzie do SDK)
i dowód, że C API działa od końca do końca (`backends/sfml/README.md`). Dla
modeli znaczy to: nagłówek `ModelHost.hpp` z uchwytami RAII i własną kopią
wyniku, jego testy zgodności i transport tekstur przez `sf::Image`
i `sf::Texture` co do bajta. Rysowanie siatki wymagałoby surowego OpenGL obok
SFML i nie ma bramki co do bitu, więc zostaje poza zakresem.

## 2. Stan wyjściowy

### 2.1 C API modeli (`backends/capi3d/iprop3d.h`, API 0.3)

Uchwyty `iprop3d_model` i `iprop3d_result`, 27 funkcji:

```c
iprop3d_library_info_init / iprop3d_library_query              /* wersja API i limity generacji */
iprop3d_model_load / iprop3d_model_free
iprop3d_model_info_init / iprop3d_model_query
iprop3d_skin_program_info_init / iprop3d_model_skin_program
iprop3d_model_input_count / _input_get / _input_find           /* deskryptor iprop_input_info_ex */
iprop3d_model_parameters_create                                /* zwykły iprop_parameters */
iprop3d_generate_options_init / iprop3d_generate / iprop3d_result_free
iprop3d_result_info_init / iprop3d_result_query
iprop3d_surface_view_init / iprop3d_result_surface
iprop3d_texture_view_init / iprop3d_result_texture / iprop3d_texture_stamp
iprop3d_result_texture_image / _texture_image_find             /* deskryptor iprop_aux_image_view_ex */
iprop3d_output_view_init / iprop3d_result_output / _result_output_find
```

Rozmiary deskryptorów pochodzą z przypięć `LAYOUT` w
`backends/capi3d/test/models.c` (platformy 64-bitowe). Każdy poza
`iprop3d_known_texture` zaczyna się od `struct_size`; poza nim
i `iprop3d_library_info` każdy ma też `flags`.

| Deskryptor | Bajty | Pola |
| --- | --- | --- |
| `iprop3d_library_info` | 40 | `api_major`, `api_minor`, `max_vertices`, `max_triangles`, `max_textures`, `max_texture_side`, `max_texels` |
| `iprop3d_model_info` | 48 | `size_x`, `size_y`, `size_z`, `units_per_meter`, `fuel`, `input_count`, `skin_program_count`, `container_flags` |
| `iprop3d_skin_program_info` | 40 | `program_index`, `width`, `height`, `name` |
| `iprop3d_known_texture` | 32 | `key`, `fuel`, `work`, `image_kinds`, `reserved` |
| `iprop3d_generate_options` | 32 | `seed`, `lod`, `known_textures`, `known_texture_count` |
| `iprop3d_result_info` | 104 | `bounds_min[3]`, `bounds_max[3]`, `size[3]`, `units_per_meter`, `vertex_count`, `triangle_count`, `surface_count`, `texture_count`, `fuel_budget`, `fuel_used`, `work_budget`, `work_used`, `output_count` |
| `iprop3d_surface_view` | 96 | `surface_index`, `texture_index`, `roughness`, `metallic`, `emission`, `alpha_mode`, `alpha_cutoff`, `two_sided`, `crisp`, `vertex_count`, `index_count`, `positions`, `normals`, `tangents`, `uvs`, `indices` |
| `iprop3d_texture_view` | 120 | `texture_index`, `repeats`, `key`, `name`, `asset_width`, `asset_height`, `width`, `height`, `byte_count`, `row_stride`, `pixel_format`, `alpha_mode`, `origin`, `image_count`, `pixels`, `fuel`, `work`, `image_kinds`, `reused` |
| `iprop3d_output_view` | 56 | `output_index`, `kind`, `list`, `value_count`, `floats_per_value`, `name`, `values` |

Z `iprop.h` bez zmian: `iprop_status_ex`, `iprop_error_ex`,
`iprop_input_info_ex`, `iprop_parameters` z rodziną `iprop_parameters_*`,
`iprop_execution_limits_ex`, `iprop_cancellation`, `iprop_aux_image_view_ex`,
`iprop_utf8_view`. Stałe: `IPROP3D_MAX_LOD` 7, `IPROP3D_ALPHA_OPAQUE/CUTOUT/BLEND`
0/1/2, `IPROP3D_OUTPUT_POINT3D/BOX3D` 0/1 (makra `UINT32_C`). Reguły, na
których stoi plan (nagłówek i `iprop3d.cpp`):

- tablice, piksele i nazwy są pożyczone z wyniku do `iprop3d_result_free`;
  wynik przeżywa `iprop3d_model_free` i kolejne generacje; nieudane wywołanie
  nie zmienia wyjść;
- zestaw parametrów innego załadowania modelu daje powód 4007
  (`FOREIGN_PARAMETERS`); `lod > 7` i rekord znanej tekstury z `reserved != 0`
  albo z bitami `image_kinds` poza `0x3E` dają `E_ARGUMENT`, zanim coś ruszy;
- tekstura nazwana jako znana nie jest liczona: generacja pobiera zapisane
  paliwo i pracę, a widok ma `reused = 1`, rozmiary, klucz, koszt i rodzaje,
  bez pikseli i z `image_count = 0`;
- jeden model wolno generować z kilku wątków naraz, każdy z własnym zestawem
  parametrów.

### 2.2 Skąd dziś bierze się kod modeli

| Co | Stan | Plik |
| --- | --- | --- |
| biblioteka C modeli | tylko statyczna: cel `iprop_capi3d` (alias `iprop::capi3d`, plik `libiprop3dc.a`); łączy `iprop::capi_static` jako `PUBLIC`, a z nim definicję `IPROP_CAPI_STATIC`, i `iprop::model` jako `PRIVATE` | `backends/capi3d/CMakeLists.txt` |
| `libipropc` | cel `iprop_capi` (`SHARED`) ze źródeł `cmake/CApiSources.cmake` i `iprop::runtime`; `SOVERSION` 0.5, na Windows `ipropc-0.5.dll`; bez kodu modeli | `backends/capi/CMakeLists.txt`, `cmake/CAbi.cmake` |
| eksport symboli | `IPROP_API` to `dllexport` tylko z `IPROP_CAPI_BUILD` i bez `IPROP_CAPI_STATIC`; żaden plik CMake nie ustawia widoczności, więc poza Windows biblioteka współdzielona eksportuje wszystko | `backends/capi/iprop.h` |
| instalowany SDK | `iprop_runtime`, `iprop_capi`, `iprop_capi_static`, `iprop.h`, `ipropc.pc`; z modeli żadnej biblioteki. Źródła wiązań `apple`, `unity`, `android`, `python` idą do `share/iprop/backends/` według rozszerzeń plików | `cmake/Install.cmake` |
| budowa dla urządzeń | `IPROP_RUNTIME_ONLY=ON` kończy konfigurację po `runtime` i `backends/capi`: bez `model/` i `backends/capi3d` | `CMakeLists.txt` |
| Godot | łączy `iprop::capi3d` statycznie we własną bibliotekę rozszerzenia | `backends/godot/CMakeLists.txt` |
| WASM | `iprop3d.wasm` jest nadzbiorem: te same listy źródeł 2D i 3D w jednym module, obok nietkniętego `iprop.wasm` | `backends/html5/CMakeLists.txt` |
| granica 2D i 3D | `iprop3d.cpp` dołącza prywatne `Handles.hpp`, `ExecutionOptions.hpp` i `CheckedBoundary.hpp`, czyta `parameters->values`, bierze `cancellation->token`, trzyma w modelu `iprop_program root` | `backends/capi3d/iprop3d.cpp`, `backends/capi/Handles.hpp` |

Ostatni wiersz przesądza rozdział 3: uchwyty `iprop_parameters`
i `iprop_cancellation` przechodzą między funkcjami 2D i 3D jako obiekty C++,
więc oba zestawy funkcji muszą pochodzić **z jednej kopii** kodu C API 2D.

### 2.3 Pięć wiązań dziś

| | Python | Swift | C#/Unity | Kotlin/Android | SFML |
| --- | --- | --- | --- | --- | --- |
| Pliki | `backends/python/src/infinite_props/{__init__,_native,__main__}.py` | `backends/apple/Sources/InfiniteProps/*.swift`, `Package.swift`, `build-xcframework.py` | `backends/unity/Packages/dev.infiniteprops.runtime/Runtime/*.cs`, `Editor/`, `backends/unity/package-native.py` | `backends/android/iprop/src/main/java/dev/infiniteprops/*.kt`, `src/main/cpp/iprop_jni.cpp` | `backends/sfml/src/{Host.hpp,Render.hpp,host.cpp}` |
| Kod natywny | `ctypes.CDLL`; ścieżka z `library=`, potem `IPROP_LIBRARY`, potem `find_library("ipropc")` | statycznie: `Artifacts/CIprop.xcframework`, w nim `libCIprop.a` = `libipropc.a` + `libiprop_runtime.a` (budowa z `IPROP_RUNTIME_ONLY`); bez artefaktu `.systemLibrary(pkgConfig: "ipropc")` | `[DllImport]` z `ipropc`, na Windows `ipropc-0.5`, na iOS `__Internal`; `package-native.py` buduje cel `iprop_capi` do `Plugins/`, na iOS statyczne `libipropc.a` | `libiprop_jni.so` łączy `iprop::capi_static` z `IPROP_RUNTIME_ONLY ON`; `System.loadLibrary("iprop_jni")` | `iprop_capi_static` w plikach wykonywalnych |
| Wersja | `iprop_library_query_ex`: API 0.7 lub nowsze, ABI 0.5, bity 18–22 (`_native.py`) | to samo w `requireRuntime` | to samo w konstruktorze `Program` | to samo w `load` (`iprop_jni.cpp`) | brak, łączenie statyczne |
| Życie i własność | `Program` z `RLock`, `weakref.finalize`, `with`; wynik: zamrożone `dataclass` z `bytes` | `final class Program` z `NSLock`, `close()`, `deinit`; wynik: `struct` z `Data` i `[Float]` | `Program : IDisposable`, `lock`, `SafeHandle`; wynik: klasy z tablicami | `Program : AutoCloseable`, `@Synchronized`; `data class` z tablicami | `std::unique_ptr` z `*_free`; `copyResult` daje wektory, widoki pożyczone zostają dostępne |
| Błędy | `IpropError(RuntimeError)` | `struct IpropError: Error` | `IpropException` | `IpropException`, budowany w JNI (`guarded`) | `Failure : std::runtime_error` |
| Limity | `generate(work_limit, timeout_ns, cancellation)`; żeton tylko z tej samej biblioteki | `ExecutionLimits`, `CancellationToken` | `ExecutionLimits` z `System.Threading.CancellationToken` (`ExecutionScope`) | `ExecutionLimits`, `CancellationToken` z dzierżawami | `ExecutionLimits`, `createCancellation()` |
| Wejścia | `Color`, `F32`, `U32`, `I32` po nazwie | `InputValue`, `InputOverride` | `InputValue` | `InputValue` | `setParameter` tekstem na `iprop_parameters` |
| Testy | `backends/python/tests/test_*.py` przez `backends/test/check-python.py` (ctest `binding_python`, na GCC także pod ASan); `backends/test/check-pixel-corpus.py` | `backends/apple/Tests/InfinitePropsTests/*.swift` (`IPROP_FIXTURES`, `IPROP_PIXEL_CORPUS_DIR`) | `backends/unity/tests/Conformance.csproj` (.NET 8, jawna lista plików) przez `backends/test/check-csharp.py`; `Tests/Editor/` tylko lokalnie | `backends/android/tests/shared/*.kt` przez `backends/android/tests/run-host.py` (JVM hosta, `-Xcheck:jni`, jawna lista plików) i `RuntimeTests.kt` na emulatorze; `backends/android/tests/check-aar.py` | `backends/sfml/test/*.cpp` w ctest (`sfml_*`) |

W wiązaniach zarządzanych generacja zaczyna od wartości domyślnych, wynik
jest własną kopią, a wywołania jednego `Program` są szeregowane („Shared
contract” w `docs/native-bindings.md`). Przyrządy buduje
`backends/test/prepare-bindings.py` w `build/binding-fixtures/`; korpus pikseli
`IPXCRP02` z `backends/html5/test/pixel-corpus.js` każde wiązanie odtwarza
własnym czytnikiem słów i blobów. Zadania CI (`build-test`: Python, C#, na
macOS Swift; `android-bindings`; `native-backends`: SFML, Godot) opisuje
[`docs/ci.md`](../ci.md); CI rusza tylko ręcznie.

### 2.4 Wzorzec: hosty modeli i ich testy

| Co | Jak | Plik |
| --- | --- | --- |
| API w JS | `generate({ seed, uniforms, lod, textures, known, workLimit, timeoutMs, signal, cancelFlag })`; `createTextureCache()` z `known()` i `complete(result)`; `createPool()`; tablice jako własne kopie; klucz jako 16 cyfr szesnastkowych | `backends/html5/js/api3d.js` |
| API w Godocie | `generate(seed, limits, lod)`, `generate_async`: wątek główny klonuje zestaw (`iprop_parameters_clone`), w puli biegnie samo `iprop3d_generate`, zadanie trzyma migawkę znanych tekstur (`Job::held`, `Job::keep`) do odczytu wyniku; surowe tablice zostają jak z runtime, siatka silnika ma dwa przeliczenia (metry, odwrócone trójkąty) | `backends/godot/src/iprop_model_godot.cpp` |
| wyrocznia | `ipgen plik.iprop3d --seed N [--lod L] [--uniform a=b] --dump`: JSON z każdym floatem jako bitami binary32 i każdym obrazem jako FNV-1a 64; bez kosztu pojedynczej tekstury, jej rodzajów i flagi `reused` | `tools/model_tools.cpp` (`writeModelDump`) |
| test WASM | korpus 9 modeli i 63 generacji wobec zrzutu, przypięty korpus, kodowania pliku, skóra równa generacji 2D, opis, błędy, poziomy, pamięć tekstur, pula | `backends/html5/test/model-conformance.js` |
| testy Godota | `check_corpus`, `check_mesh` (siatka wobec surowych tablic), `check_model`, `check_node`, `check_texture_cache`; poziomy i wątki | `backends/godot/project/models.gd`, `scale.gd` |
| testy C i C++ | `models.c`, `descriptors.c`, `errors.c`, `conformance.cpp`; 8 wątków na jednym modelu, ale tylko na poziomie C++ | `backends/capi3d/test/`, `tests/model/scale_test.cpp` |
| przypięty korpus | 80 przypadków z 25 źródeł, SHA-256 każdej części wyniku; pilnuje równości między platformami, wiązania go nie powtarzają | `tests/fixtures/model-corpus-v1.2.json`, `tests/model/corpus_test.cpp` |

### 2.5 Nieaktualne zdania w repozytorium

Nie przepisywać ich do wiązań; poprawić w kroku dokumentacji (5.4):

- tabela rozmiarów w `docs/c-api-models.md` podaje 12, 96 i 96 bajtów dla
  opcji, wyniku i tekstury i nie ma dwóch deskryptorów (prawdziwe są w 2.1);
- komentarz na górze `iprop3d.h` mówi „API 0.2”, a `iprop3d.cpp`
  i `backends/capi3d/CMakeLists.txt` „API 0.1”; makro mówi 0.3;
- `iprop3d.h` („handles are not thread-safe”) i akapit „Handles”
  w `docs/c-api-models.md` przeczą gwarancji wątków API 0.3 z tych samych plików;
- `spec/models-v1.2.md` („Fuel”) mówi, że tekstura wydaje budżet własnego
  programu, a `iprop3d.h` i `CHANGELOG.md`, że wspólny: to zdanie poprawia M6
  w tym samym pliku, wiązania go nie ruszają.

## 3. Decyzje do podjęcia

### 3.1 Skąd wiązanie bierze symbole `iprop3d_*`

| Wariant | Co to jest | Koszt | Ocena |
| --- | --- | --- | --- |
| A | kod modeli w `libipropc` | łamie zasadę 1.5 i odrzuconą alternatywę 15.2 wizji; biblioteka z ABI 0.5 dostałaby symbole bez obietnicy ABI; każdy użytkownik 2D płaci rozmiarem | odrzucony |
| B | druga biblioteka współdzielona z samym kodem modeli, łączona dynamicznie z `libipropc` | `iprop3d.cpp` i `iprop::model` potrzebują prywatnych typów C++ i VM 2D, których `libipropc` nie udostępnia jako ABI (na Windows eksportuje tylko `IPROP_API`); trzeba by poszerzyć ABI 0.5 albo mieć dwie kopie runtime, między którymi krążą uchwyty | odrzucony |
| C | **biblioteka-nadzbiór**: jedna współdzielona z C API 2D, C API modeli i oboma runtime; eksportuje `iprop_*` i `iprop3d_*`; ten sam skład co `iprop3d.wasm` | nowy artefakt na platformę; proces z 2D przez `libipropc` i modelami przez nadzbiór ma kod 2D dwa razy; uchwyty nie mogą przechodzić między bibliotekami | **Python** (`ctypes` ładuje nadzbiór dla obiektów modeli); **C#/Unity na desktopie i Androidzie** (druga wtyczka `iprop3dc` obok `ipropc`) |
| D | łączenie statyczne `iprop::capi3d` we własny plik natywny wiązania, jak w Godocie | rośnie ten plik; `IPROP_RUNTIME_ONLY` musi umieć skonfigurować `model/` i `backends/capi3d` | **Swift** (`libCIprop.a` dostaje `libiprop3dc.a` i `libiprop_model.a`); **Kotlin** (`libiprop_jni.so`); **SFML**; **Unity na iOS** (jedno archiwum z 2D i 3D, nigdy dwa z obiektami 2D) |

Zmierzony koszt kodu modeli: `iprop3d.wasm` 1 052 538 B przy `iprop.wasm`
866 282 B (wizja 19.4); rozszerzenie Godota z 1 235 480 do 1 435 048 B po M1
(wizja 11). W lokalnym `build-3d` (arm64, Release) `libipropc.1.1.0.dylib` ma
1 207 984 B, `libiprop_model.a` 397 272 B, `libiprop3dc.a` 54 248 B. Rozmiaru
nadzbioru nikt nie zna, bo nikt go nie zbudował: zmierzyć w etapie 0.

### 3.2 Tabela decyzji

| # | Pytanie | Propozycja | Dlaczego | Kto decyduje |
| --- | --- | --- | --- | --- |
| 1 | dystrybucja biblioteki modeli | 3.1: nadzbiór dla Pythona i C#, łączenie statyczne dla Swifta, Kotlina, SFML i Unity na iOS; `libipropc` bez zmian | wynika z `Handles.hpp` i z zasady 1.5 wizji | użytkownik |
| 2 | nazwa nadzbioru | cel `iprop_capi3d_shared`, `OUTPUT_NAME iprop3dc`, bez `VERSION` i `SOVERSION`; na Windows `iprop3dc.dll`, a statyczny `iprop_capi3d` dostaje tam nazwę `iprop3dc_static` | wzór `ipropc` i `ipropc_static`; podgląd nie obiecuje ABI, więc nie udaje go numerem | wykonawca; nazwa z numerem przy zamrożeniu: użytkownik |
| 3 | czy nadzbiór wchodzi do SDK | nie przed zamrożeniem; decyzja przy wydaniu 1.2 | tak mówią dziś `docs/c-api-models.md` i wizja 9.2. Skutek: SDK niesie źródła wiązań z klasami modeli, ale bez biblioteki | użytkownik |
| 4 | zachowanie bez biblioteki modeli | Python: `OSError` z nazwą `IPROP3D_LIBRARY`; C#: `NotSupportedException` z poleceniem `package-native.py --models`; Swift: kod modeli kompiluje się tylko, gdy artefakt niesie `iprop3d.h`; Kotlin i SFML mają modele zawsze | jasny błąd zamiast `DllNotFoundException` i błędu łączenia; ścieżka `pkgConfig: "ipropc"` dalej buduje pakiet 2D | wykonawca |
| 5 | wersja w podglądzie | przy pierwszym użyciu modeli `iprop3d_library_query` musi dać **dokładnie** `(0, 3)`; do tego dotychczasowy test 2D na tej samej bibliotece; skrypt porównuje stałe wiązań z makrami nagłówka (5.1) | podgląd może zmienić każdą deklarację; deskryptor z rozmiarem nie chroni przed zmianą znaczenia pola | wykonawca |
| 6 | nazwy i kształt API | `Program` → `Model`, `GeneratedImage` → `GeneratedModel`; te same typy wejść, limitów, żetonu i błędu co w 2D; liczniki w `statistics`; `lod`, `textures`, `known` jako parametry nazwane; szkice w rozdz. 4 | użytkownik wiązania zna już idiom 2D | wykonawca, nazwy do wglądu użytkownika |
| 7 | typ klucza tekstury | liczba 64-bitowa bez znaku (`int`, `UInt64`, `ulong`, `ULong`, `std::uint64_t`) i pomocnik dający 16 małych cyfr szesnastkowych | typ z C; JS i Godot mają tekst, bo nie mają `u64`; plik `IPTX` nazywa się tymi cyframi (spec) | wykonawca |
| 8 | własność dużych tablic | jedna kopia do typu języka, żaden wskaźnik natywny nie wychodzi: Python `bytes` (z `memoryview.cast("f")`), Swift `[Float]`, `[UInt32]` i `Data` dla pikseli, C# `float[]`, `int[]`, `byte[]`, Kotlin `FloatArray`, `IntArray`, `ByteArray`. SFML: kopie w `copyModel`, a widoki pożyczone zostają w surowym C | reguła „No borrowed native pointer escapes” z `docs/native-bindings.md`; tekstura użyta ponownie nie niesie pikseli, więc duże kopie robi się raz | wykonawca |
| 9 | pamięć tekstur | klasa `TextureCache` w każdym wiązaniu: `generate(textures: cache)` zwraca wynik kompletny (tekstura użyta ponownie to wpis pamięci), niższy poziom `known:` zwraca tekstury bez pikseli; generacja trzyma migawkę wpisów do odczytu wyniku; bez zapisu na dysk | odpowiednik `createTextureCache()`; migawka, jak `Job::held` w Godocie, spełnia regułę „host trzyma tekstury, aż zużyje wynik” także przy wątkach | wykonawca |
| 10 | `lod` | parametr `lod` 0–7, domyślnie 0, i stała 7; wybór poziomu zostaje u wołającego | wizja 19.2, decyzja 1: poziom wybiera host | wykonawca |
| 11 | wątki | `generate` jednego `Model` wolno wołać z wielu wątków: zestaw parametrów tworzony na wywołanie pod blokadą modelu, `iprop3d_generate` i odczyt wyniku bez blokady, `close` odracza zwolnienie do końca trwających generacji, `TextureCache` ma własną blokadę. SFML: bez blokad, reguły z nagłówka | gwarancja API 0.3; ten sam podział ma `generate_async` w Godocie | wykonawca; zależy od pytania 2 w rozdz. 7 |
| 12 | pomocniki silników | tabela etapu B w rozdz. 1 | SceneKit da się sprawdzić na macOS, Unity tylko w Edytorze, `Material` w Unity nie ma jednej odpowiedzi | użytkownik |
| 13 | format zgodności | korpus binarny `IPMCRP01` zrobiony z `ipgen --dump` przez `prepare-bindings.py` (5.2) | każde wiązanie ma już czytnik słów i blobów dla `IPXCRP02`; test na urządzeniu i w Edytorze nie uruchomi `ipgen` | wykonawca |
| 14 | Android: modele zawsze w `libiprop_jni.so` | tak, jedna biblioteka; zmierzyć rozmiar na ABI przed i po | dwie biblioteki JNI miałyby dwie kopie 2D i żeton anulowania przechodzący między nimi | użytkownik, po pomiarze |

## 4. Plan dla każdego wiązania

Reguły wspólne. Jedna generacja to: pod blokadą modelu
`iprop3d_model_parameters_create`, `iprop_parameters_set_*_named` i migawka
znanych tekstur; bez blokady `iprop3d_generate`, odczyt wszystkiego do kopii,
`iprop3d_result_free` i `iprop_parameters_free`. Obiekt modelu woła **jedną**
bibliotekę, także dla parametrów, żetonu i błędów. Makra `UINT32_C` nie
trafiają do Swifta ani C#: stałe wpisać liczbami i pokryć testem. Typy wyniku
(`GeneratedModel`, `Surface`, `ModelTexture`, `ModelOutput`, `SkinProgram`,
`KnownTexture`, `ModelLibrary`) mają pola z tabeli 2.1 w pisowni języka;
szkice pokazują tylko to, co się różni. Testy wspólne (wartości z `models.c`
i `model-conformance.js`):

| # | Test | Treść |
| --- | --- | --- |
| T1 | korpus | każdy przypadek `model-corpus.bin`: granice, liczniki, tekstury (klucz, nazwa, rozmiary, `repeats`, skrót FNV-1a 64 pikseli i każdej mapy), powierzchnie (liczby materiału co do bitu, pięć tablic co do bajta), wyjścia; druga generacja równa i niezależna |
| T2 | opis | `model-crate.iprop3d`: rozmiar 12 × 10 × 8, 50 jednostek na metr, wejścia `wide` (f32, 8), `stain` (kolor `#336699`), `extra` (u32, 1), `rounding` (f32, `0.5`), skóra `tile` 16 × 8; biblioteka: API (0, 3), limity 65 536, 131 072, 32, 2048, 4 194 304, stempel niezerowy |
| T3 | skóra = 2D | tekstura 0 skrzynki przy seedzie 5 równa generacji 2D `model-tile.iprop` w tym samym wiązaniu (seed 77, 32 × 16, `tint` = `#336699`), razem z mapą normalnych |
| T4 | błędy | plik 2D jako model i model jako program 2D: status 2, powód 1002; puste i ucięte bajty: status 2; `wide = 0`: status 3, powód 3010, etap 4, „box size must be positive”; `rounding = 100`: komunikat od „skin 'tile': ”; limit pracy 1: 3015; limit 200: 3015 i „skin 'tile'”; żeton anulowany: 3016; nieznane wejście: 4005; zły typ: 4004; `lod = 8`: błąd argumentu; plik z `--debug`: linia źródła; po każdym błędzie model generuje dalej |
| T5 | poziomy | `model-lod.iprop3d`, seed 3, `lod` 0–3 równe przypadkom korpusu; mniej wierzchołków na 1 i 2, tyle samo na 3 co na 2; wyjścia i klucze tekstur jak na poziomie 0 |
| T6 | tekstury znane | z pamięcią: druga generacja ma same `reused`, te same obiekty pikseli, liczniki i geometrię równe generacji bez pamięci; z samymi rekordami: brak pikseli i map, rozmiary zostają; pamięć bez wpisu: błąd; rekord z bitem poza `0x3E`: status 4; inny seed: brak `reused` |
| T7 | wątki | 8 wątków po 6 generacji na jednym modelu (seedy 100 + i, `lod` = i mod 3, jak `tests/model/scale_test.cpp`) równe szeregowym; to samo ze wspólną pamięcią; zamknięcie modelu w trakcie nie psuje trwających generacji |
| T8 | własność | wynik przeżywa zamknięcie modelu; podwójne zamknięcie; generacja po zamknięciu daje błąd „zamknięty” w idiomie wiązania |
| T9 | układy (tylko FFI) | rozmiary i przesunięcia lustrzanych struktur równe przypięciom z `models.c` na platformie 64-bitowej |

### 4.1 Python

| Aspekt | Plan |
| --- | --- |
| Pliki | `backends/python/src/infinite_props/_native.py`: struktury dziewięciu deskryptorów z 2.1 i `class ModelNative(Native)` (te same `call`, `sized`, `Error`; nadzbiór eksportuje też 2D). Nowy `infinite_props/models.py`; `__init__.py` eksportuje nazwy. Nowy `backends/python/tests/test_models.py` |
| Kod natywny | `library=`, potem `IPROP3D_LIBRARY`, potem `find_library("iprop3dc")`. Żeton musi pochodzić z tej samej biblioteki: warunek z `Program.generate` przechodzi do `Model.generate`. `CDLL` zwalnia GIL na czas wywołania (korzysta z tego już `test_execution_limits.py`), więc T7 biegnie równolegle |
| Testy | T1–T9; `to_pillow()` pomijane bez Pillow, jak dziś |
| Budowa i pakiet | nic do kompilowania; `pyproject.toml` bez zmian |
| CI | `backends/test/check-python.py` dostaje `--models ŚCIEŻKA` (ustawia `IPROP3D_LIBRARY`); `tests/CMakeLists.txt` podaje w `binding_python` `$<TARGET_FILE:iprop_capi3d_shared>`, więc test idzie też pod ASan; krok Pythona w `build-test` podaje plik z `build/backends/capi3d/` |

```python
class Model:                                    # odpowiednik Program
    def __init__(self, source, *, library=None)
    metadata: ModelMetadata                     # size, units_per_meter, fuel, container_flags
    inputs: tuple[Input, ...]                   # typ z 2D
    skin_programs: tuple[SkinProgram, ...]
    def generate(self, seed=0, *, inputs=None, lod=0, textures=None, known=None,
                 work_limit=0, timeout_ns=0, cancellation=None) -> GeneratedModel
    def cancellation_token(self) -> CancellationToken
    def close(self)                             # także `with`
class TextureCache: known(), get(key), discard(key), clear(), __len__
def model_library(library=None) -> ModelLibrary # api, limity, max_lod, texture_stamp
# Surface: pięć tablic jako bytes, floats(name) -> memoryview; ModelTexture: key: int, rgba: bytes, to_pillow();
# ModelOutput: value_bits i values, jak Output w 2D
```

### 4.2 Swift

| Aspekt | Plan |
| --- | --- |
| Pliki | w `backends/apple/`. `build-xcframework.py`: `-DIPROP_RUNTIME_MODELS=ON`, cele `iprop_capi_static` i `iprop_capi3d`, `libtool` łączy cztery archiwa, do `Headers/` idzie też `iprop3d.h`, mapa modułu dostaje drugi nagłówek. `Package.swift`: definicja `IPROP_MODELS` tylko wtedy, gdy artefakt niesie `iprop3d.h`, i na **obu** celach, `InfiniteProps` i `InfinitePropsTests` (ustawienia Swifta są na cel; inaczej `ModelTests.swift` nie skompiluje się na ścieżce `pkgConfig` albo zniknie po cichu). `Sources/InfiniteProps/InfiniteProps.swift`: `string(_:)` i `requireRuntime()` z `private` na dostęp w module. Nowe `Models.swift` i (etap B) `AppleModels.swift`, oba w `#if IPROP_MODELS`. Nowy `Tests/InfinitePropsTests/ModelTests.swift` |
| Kod natywny | jedno archiwum `libCIprop.a` z 2D i 3D. `libtool` zobaczy dwa obiekty `geometry.cpp.o`, bo plik `geometry.cpp` jest w `cmake/RuntimeSources.cmake` i w `cmake/ModelSources.cmake`: sprawdzić, że zostają oba. Ścieżka `pkgConfig: "ipropc"` zostaje samym 2D (decyzja 4) |
| Testy | T1–T8; T7 przez `DispatchQueue.concurrentPerform`, jak `testConcurrentProgramCalls`. Etap B: SceneKit sprawdzany strukturalnie jak `check_mesh` (liczba źródeł i elementów, bajty źródeł równe tablicom, materiał z liczb powierzchni) i obrazem z `SCNRenderer`. Konwencji SceneKit (początek UV, układ stycznej) repozytorium nie podaje: wyprowadzić z dokumentacji Apple, zapisać w README, potwierdzić renderem płyty z wypukłym kwadratem, jak w wizji 10.5 |
| Budowa i pakiet | budowniczy nie nadpisuje artefaktu, więc stary `Artifacts/CIprop.xcframework` trzeba usunąć; zapisać rozmiar `libCIprop.a` przed i po |
| CI | polecenia kroku Apple w `build-test` bez zmian; robią więcej |

```swift
public final class Model: @unchecked Sendable {
    public convenience init(contentsOf url: URL) throws
    public init(bytes: Data) throws
    public let metadata: ModelMetadata              // size: SIMD3<Float>, unitsPerMeter, fuel, containerFlags
    public let inputs: [Input]                      // typ z 2D
    public let skinPrograms: [SkinProgram]
    public func generate(seed: UInt32 = 0, inputs: [String: InputValue] = [:], lod: UInt32 = 0,
                         textures: TextureCache? = nil, known: [KnownTexture] = [],
                         limits: ExecutionLimits = ExecutionLimits()) throws -> GeneratedModel
    // oraz przeciążenie z `overrides: [InputOverride]`, jak w Program
    public func close()
    public static func library() throws -> ModelLibrary
}
public final class TextureCache: @unchecked Sendable   // known, texture(for:), remove(_:), removeAll(), count
// Surface: positions, normals, tangents, uvs: [Float]; indices: [UInt32]; ModelTexture: key: UInt64, rgba: Data
```

### 4.3 C#/Unity

| Aspekt | Plan |
| --- | --- |
| Pliki | w `backends/unity/Packages/dev.infiniteprops.runtime/`: `Runtime/Model.cs` (czysty C#), etap B `Runtime/UnityModels.cs`, `Tests/Editor/ModelCorpus.cs` (wspólny dla Edytora i testu .NET, jak `PixelCorpus.cs`), każdy z nowym plikiem `.meta` o własnym GUID. `backends/unity/tests/ModelConformance.cs` i wpisy `<Compile>` w `Conformance.csproj`; `Conformance.cs` woła `ModelConformance.Run` |
| Kod natywny | `Native3D` z `[DllImport("iprop3dc")]` (iOS: `__Internal`) deklaruje funkcje modeli **i własne** kopie potrzebnych funkcji 2D (`iprop_library_query_ex`, `iprop_parameters_set_*_named`, `iprop_parameters_free`, `iprop_cancellation_create/cancel/free`). `Native.*` i `ExecutionScope` są związane z `ipropc` i nie mogą obsłużyć modelu; struktury (`Error`, `Limits`, `Utf8View`, `AuxImage`) są wspólne. `SafeHandle` przekazany do P/Invoke odracza zwolnienie do końca wywołania (zachowanie platformy .NET; pokrywa to T7) |
| Testy | T1–T9 w `ModelConformance.cs`, T7 przez `Parallel.For`. Etap B: testy Edytora dla `ToMesh()` i `ToTexture2D()` wzorowane na `check_mesh`. Konwersji do konwencji Unity (skrętność układu, kierunek trójkątów, początek UV, znak stycznej) repozytorium nie podaje. Jedyny fakt z kodu: `GeneratedImage.Upload` odwraca wiersze tekstury (`UnityImages.cs`), więc UV modelu muszą to uwzględnić. Wyprowadzić, zapisać w README jako tabelę, potwierdzić oświetlonym renderem w Edytorze użytkownika |
| Budowa i pakiet | `backends/unity/package-native.py --models` dodaje na każdej platformie `-DIPROP_RUNTIME_MODELS=ON` (jego `build()` zawsze podaje `-DIPROP_RUNTIME_ONLY=ON`, więc bez tego celu modeli nie ma), buduje cel `iprop_capi3d_shared` i publikuje `libiprop3dc.dylib`, `iprop3dc.dll`, `libiprop3dc.so` (także trzy ABI Androida) z plikami `.meta`; na iOS jedno archiwum z czterech, jak w 4.2 |
| CI | `backends/test/check-csharp.py` dostaje `--models ŚCIEŻKA` i dopisuje jej katalog do ścieżki ładowania; krok C# w `build-test` ją podaje |

```csharp
public sealed class Model : IDisposable {
    public Model(byte[] bytes);
    public ModelMetadata Metadata { get; }           // SizeX, SizeY, SizeZ, UnitsPerMeter, Fuel, ContainerFlags
    public IReadOnlyList<Input> Inputs { get; }      // typ z 2D
    public IReadOnlyList<SkinProgram> SkinPrograms { get; }
    public GeneratedModel Generate(uint seed = 0, IReadOnlyDictionary<string, InputValue> inputs = null,
        uint lod = 0, TextureCache textures = null, IReadOnlyList<KnownTexture> known = null,
        ExecutionLimits limits = null);
    public static ModelLibrary Library { get; }
    public void Dispose();
}
public sealed class TextureCache                     // Known, Find(key), Remove(key), Clear(), Count
// Surface: float[] Positions, Normals, Tangents, Uvs; int[] Indices; ModelTexture: ulong Key, byte[] Rgba
```

### 4.4 Kotlin/Android

| Aspekt | Plan |
| --- | --- |
| Pliki | w `backends/android/`. `iprop/src/main/cpp/CMakeLists.txt`: `IPROP_RUNTIME_MODELS ON`, `iprop_jni` łączy `iprop::capi3d`. Nowy `iprop3d_jni.cpp` w tej samej bibliotece; `Failure`, `guarded`, `bytes`, `longs` wynieść z `iprop_jni.cpp` do wspólnego nagłówka. `iprop/src/main/java/dev/infiniteprops/Model.kt` z `internal object ModelNative`; `AndroidImages.kt`: `ModelTexture.toBitmap()`. Nowy `tests/shared/ModelConformance.kt` (`verifyModels(load)`), wołany z `tests/Main.kt` i `RuntimeTests.kt` |
| Kod natywny | zestaw parametrów jest uchwytem na wywołanie (`createParameters`, `set`, `freeParameters`), nie polem `Context` jak w 2D, bo inaczej generacje jednego modelu nie pobiegną równolegle. Tablice przez `NewFloatArray` i `SetFloatArrayRegion` (jedna kopia). Znane tekstury jako `LongArray`, cztery wartości na rekord. `close` odracza zwolnienie jak `CancellationToken.withHandle` (`ExecutionLimits.kt`) |
| Testy | T1–T8, T7 przez `Executors.newFixedThreadPool(8)`; bity floatów porównywane przez `toRawBits`; na JVM hosta z `-Xcheck:jni` i na emulatorze; `toBitmap()` w `RuntimeTests.kt` |
| Budowa i pakiet | AAR zawsze z modelami (decyzja 14); zapisać rozmiar `libiprop_jni.so` na ABI przed i po |
| CI | zadanie `android-bindings` woła już `prepare-bindings.py`, a zadanie Gradle kopiuje całe `build/binding-fixtures` do zasobów testu: poleceń nie trzeba zmieniać; uzupełnić listy z 5.3 |

```kotlin
class Model(bytes: ByteArray) : AutoCloseable {
    val metadata: ModelMetadata                // sizeX, sizeY, sizeZ, unitsPerMeter, fuel, containerFlags
    val inputs: List<Input>                    // typ z 2D
    val skinPrograms: List<SkinProgram>
    @JvmOverloads fun generate(seed: UInt = 0u, inputs: Map<String, InputValue> = emptyMap(), lod: Int = 0,
        textures: TextureCache? = null, known: List<KnownTexture> = emptyList(),
        limits: ExecutionLimits = ExecutionLimits()): GeneratedModel
    override fun close()
    companion object { fun library(): ModelLibrary }
}
class TextureCache                             // known, get(key), remove(key), clear(), size
// Surface: positions, normals, tangents, uvs: FloatArray; indices: IntArray; ModelTexture: key: ULong, rgba: ByteArray
```

### 4.5 SFML

| Aspekt | Plan |
| --- | --- |
| Pliki | nowy `backends/sfml/src/ModelHost.hpp` (bez zależności od SFML, jak `Host.hpp`); etap B: `ModelRender.hpp` i tryb `--model-textures` w `host.cpp`. Nowe `backends/sfml/test/models.cpp` i `model-textures.cpp` |
| Kod natywny | cele w `backends/sfml/CMakeLists.txt` łączą `iprop::capi3d`, który niesie statyczne C API 2D; żadnych blokad w nagłówku, reguły wątków z `iprop3d.h` |
| Testy | `sfml_models` (bez okna, z `Threads::Threads`, jak `sfml_execution_limits`): T1–T8 przez `copyModel` oraz to, co ma tylko C++: kopia przeżywa `iprop3d_result_free`, widok pożyczony jest ważny do niego. `sfml_model_textures`: każda tekstura i mapa korpusu po przejściu przez `sf::Image` i `sf::Texture::copyToImage()` ma skrót ze zrzutu |
| Budowa i pakiet | `cmake/Install.cmake` i `resources()` w `scripts/language_docs.py` wymieniają `Host.hpp` i `Render.hpp` z nazwy: dopisać nowe nagłówki |
| CI | `native-backends` uruchamia całe `ctest` pod `xvfb-run`: nowe testy wejdą bez zmian w pliku CI |

```cpp
namespace iprop::sfml_host {
using Model = std::unique_ptr<iprop3d_model, decltype(&iprop3d_model_free)>;
using ModelResult = std::unique_ptr<iprop3d_result, decltype(&iprop3d_result_free)>;
iprop3d_model* loadModel(const std::vector<std::uint8_t>& bytes);
iprop3d_model_info queryModel(const iprop3d_model* model);
Parameters createModelParameters(const iprop3d_model* model);   // setParameter z Host.hpp działa bez zmian
struct ModelRequest { std::uint32_t seed = 0, lod = 0; ExecutionLimits limits;
                      std::span<const iprop3d_known_texture> known; };
iprop3d_result* generateModel(const iprop3d_model* model, const ModelRequest& request,
                              const iprop_parameters* parameters = nullptr);
GeneratedModel copyModel(const iprop3d_result* result);         // surfaces, textures z images, outputs, liczniki
class TextureCache;                                             // known(), complete(GeneratedModel&)
void uploadModelTexture(sf::Texture& texture, const ModelTexture& source, bool crisp);  // ModelRender.hpp
}
```

## 5. Wspólne elementy

### 5.1 Budowa (etap 0)

1. `CMakeLists.txt`: opcja `IPROP_RUNTIME_MODELS` (domyślnie `OFF`). W gałęzi
   `IPROP_RUNTIME_ONLY`, przed `return()`, dodaje `model` i `backends/capi3d`.
   Bez opcji budowa dla urządzeń ma te same cele co dziś.
2. `backends/capi3d/CMakeLists.txt`: cel `iprop_capi3d_shared` (`SHARED`) ze
   źródeł `IPROP_CAPI_IMPLEMENTATION_SOURCES` **i**
   `IPROP_CAPI3D_IMPLEMENTATION_SOURCES`, z definicją `IPROP_CAPI_BUILD`,
   katalogami `backends/capi`, `backends/capi3d` i katalogiem wygenerowanego
   `BuildVersion.hpp`, łączony prywatnie z `iprop::model`. Nie łączyć go
   z `iprop::capi3d`: ten niesie `IPROP_CAPI_STATIC`, więc na Windows nic by
   nie eksportował. Cel nie trafia do `cmake/Install.cmake` (decyzja 3).
3. Testy C: `capi3d_models_shared` (istniejące `test/models.c` z nadzbiorem:
   dowód eksportu, ważny na Windows) i `capi3d_threads` (nowe
   `test/threads.cpp`: N wątków, każdy tworzy zestaw, ustawia `wide`,
   generuje, czyta i zwalnia; wynik równy szeregowemu). Dziś żaden test na
   poziomie C nie robi tego, co będą robić wiązania.
4. `scripts/check_model_bindings.py` jako ctest `model_binding_versions`:
   stała wersji w każdym wiązaniu równa makrom `IPROP3D_API_VERSION_*`;
   każde źródło z `corpus` w `model-conformance.js` jest też w korpusie wiązań.

### 5.2 Przyrządy i korpus

Wszystko jest **produktem budowy**, nigdy plikiem w repozytorium: format
modeli jest podglądem, a ceny paliwa jeszcze się zmienią. Nowy
`backends/test/model_bindings.py`, wołany z `prepare-bindings.py` (z flagą
`--models-only` dla CMake), pisze do `build/binding-fixtures/`:

| Plik | Źródło | Po co |
| --- | --- | --- |
| `model-crate.iprop3d`, `model-crate-debug.iprop3d` (`--debug --no-compress`) | `backends/capi3d/test/crate.ips` | T2, T3, T4, T6 |
| `model-tile.iprop` | `backends/capi3d/test/tile.ips` | T3, T4 |
| `model-empty.iprop3d`, `model-outputs.iprop3d` | `backends/capi3d/test/{empty,outputs}.ips` | wynik pusty, wyjścia |
| `model-lod.iprop3d` | `examples/language/model-lod.ips` | T5, T6, T7 |
| `model-corpus.bin`, `model-corpus.json` | poniżej | T1 |

**Przypadki korpusu:** lista `corpus` z `model-conformance.js` z jej seedami
i wejściami (9 modeli, 63 generacje), `examples/models/boulder.ips` z `models.gd`
(seedy 1 i 6, bez wejść i z `rough`) i `model-lod.ips` przy seedzie 3 na
poziomach 0–3. Skrypt kompiluje każdy model własnym `ipc`, woła własne
`ipgen --dump` i przepisuje zrzut. Wartość `f32` w wejściach musi być dokładna
w binary32 (skrypt to sprawdza), bo `ipgen` czyta ją przez `std::stof`;
dlatego `rough` to `0.875`, nie `0.9` jak w `models.gd`.

**Format `IPMCRP01`** (little-endian; `u64` to dwa słowa, niższe pierwsze;
`blob` to `u32` długości i bajty, jak w `IPXCRP02`):

```
"IPMCRP01"
u32 liczba programów; każdy: blob (skompilowany .iprop3d)
u32 liczba przypadków; każdy:
  blob etykieta ("examples/models/chair.ips seed 5 finish=5 fabric=2 lod 0")
  u32 program, u32 seed, u32 lod
  u32 n; n × { blob nazwa, u32 typ (0 kolor, 1 f32, 2 u32), u32 wartość (RGBA od r, bity albo liczba) }
  3 × u32 size, u32 units_per_meter, 3 × u32 bounds.min, 3 × u32 bounds.max          (bity binary32)
  u32 vertices, u32 triangles, u64 fuel.budget, u64 fuel.spent, u64 work.budget, u64 work.spent
  u32 n; n × { u64 klucz, blob nazwa, u32 width, u32 height, u32 asset_w, u32 asset_h, u32 repeats,
               u64 skrót koloru, u32 m; m × { u32 rodzaj, blob nazwa, u64 skrót } }
  u32 n; n × { u32 tekstura, u32 roughness, u32 metallic, u32 emission, u32 alpha, u32 cutoff,
               u32 two_sided, u32 crisp, blob positions, blob normals, blob tangents, blob uvs, blob indices }
  u32 n; n × { blob nazwa, u32 rodzaj (0 point3d, 1 box3d), u32 lista, u32 liczba wartości, blob wartości }
```

Skrót to FNV-1a 64 bajtów RGBA (`fnv1a64` w `model-conformance.js`). Typ
wejścia skrypt bierze z listy `inputs` zwykłego wyjścia `ipgen`.
`model-corpus.json` niesie format, liczbę przypadków i SHA-256 pliku; wiązanie
sprawdza go przed odczytem, jak przy korpusie pikseli. Korpus powstaje
z narzędzi tej samej budowy co testowana biblioteka, więc sprawdza warstwę
wiązania; równości między platformami pilnuje `model_corpus`. Testy SFML
zależą od celu `backend_model_fixtures` w `backends/ResultTests.cmake`.

### 5.3 Listy, które trzeba uzupełnić ręcznie

| Lista | Plik | Co dopisać |
| --- | --- | --- |
| pliki `backends/test/` w SDK | `cmake/Install.cmake`, `resources()` w `scripts/language_docs.py` | `model_bindings.py` |
| nagłówki SFML w SDK | te same dwa pliki | `ModelHost.hpp`, `ModelRender.hpp` |
| przyrządy CMake | `backends/ResultTests.cmake` | `backend_model_fixtures` |
| źródła testu C# | `backends/unity/tests/Conformance.csproj` | `Model.cs`, `ModelCorpus.cs`, `ModelConformance.cs` |
| pliki `.meta` pakietu Unity | katalog pakietu | po jednym na nowy plik, nowy GUID |
| projekt Edytora | `backends/unity/tests/prepare-editor.py` | sprawdzić, że wtyczka modeli jest w `Plugins/` |
| źródła testu JVM | `backends/android/tests/run-host.py` | `Model.kt`, `ModelConformance.kt` |
| klasy w AAR | `backends/android/tests/check-aar.py` | `Model`, `GeneratedModel`, `ModelNative` |
| reguły R8 | `backends/android/iprop/consumer-rules.pro` | `dev.infiniteprops.ModelNative` |
| argumenty ctest | `tests/CMakeLists.txt` | `--models` w `binding_python` |
| kroki CI | `.github/workflows/ci.yml` | ścieżka nadzbioru w krokach Pythona i C# |

Linki w README wiązań muszą trafiać w pliki z `resources()`: sprawdza to
`verify_links` w `scripts/language_docs.py` i pętla w
`scripts/install_smoke.cmake`. `backends/capi3d/README.md`
i `docs/c-api-models.md` w tym zbiorze są; `tests/` i `docs/plans/` nie.

### 5.4 Dokumentacja

| Plik | Zmiana |
| --- | --- |
| README pięciu wiązań i pakietu Unity | sekcja „Models (preview)”: budowa, przykład, pamięć tekstur, wątki, polecenie weryfikacji |
| `docs/native-bindings.md` | pięć wierszy w tabeli „Models (preview)”, bez zdania o braku interfejsu; akapit „Build and verification” |
| `docs/c-api-models.md`, `backends/capi3d/README.md` | cel współdzielony, nowe testy, poprawki z 2.5 |
| `docs/building.md`, `docs/ci.md` | opcja `IPROP_RUNTIME_MODELS`; nowe testy i kroki |
| `docs/language/migration-v1.2.md`, `CHANGELOG.md` (`## Unreleased`), `docs/language/models.md` | zdania o braku interfejsu; akapit o hostach na końcu rozdziału modeli |
| wizja | rozdział ze stanem po wykonaniu |

Po każdej zmianie: `python3 scripts/language_docs.py check --compiler <build>/tools/ipc`.

### 5.5 Wersje

Nic się nie zmienia: produkt 1.1.0 (`CMakeLists.txt`,
`backends/python/pyproject.toml`, `package.json` pakietu Unity; równość
sprawdza `scripts/release_preflight.py`), C API 2D 0.8 i ABI 0.5, C API modeli
0.3. W `iprop3d.h` zmieniają się tylko komentarze z 2.5 i ewentualne zdanie
z pytania 2. Numery podnosi dopiero przygotowanie wydania.

## 6. Kolejność i bramki

| # | Etap | Dlaczego w tym miejscu |
| --- | --- | --- |
| 0 | wspólne: 5.1, 5.2, 5.3 | bez nadzbioru, opcji budowy i korpusu nie ruszy żadne wiązanie |
| 1 | **Python** | najkrótsza pętla i ten sam język co skrypt korpusu; jako pierwszy sprawdza nadzbiór, format `IPMCRP01`, semantykę pamięci i wątków; biegnie w `ctest` na każdej platformie, także pod ASan |
| 2 | SFML | ścieżka statyczna bez pakowania; `copyModel` ustala kod odczytu, który powtarza JNI |
| 3 | Swift | ścieżka statyczna ze zmianą pakowania; całość, z SceneKit, da się sprawdzić na tym Macu |
| 4 | Kotlin/Android | JNI sprawdzalne na JVM hosta; emulator dopiero w CI |
| 5 | C#/Unity | drugi użytkownik nadzbioru; test .NET lokalnie, pomocnik `Mesh` czeka na Edytor użytkownika |

Wiązanie jest **zrobione**, gdy na końcowym drzewie przechodzą:

1. `ctest` w pełnej budowie natywnej, w tym `capi3d_*`, `capi3d_threads`,
   `binding_python`, `model_binding_versions`;
2. polecenie z sekcji „Verify” README tego wiązania, rozszerzone o modele:
   Python przez `check-python.py --models`, Swift przez `build-xcframework.py`
   i `swift test`, C# przez `check-csharp.py --models`, Kotlin przez
   `backends/android/tests/run-host.py`, SFML przez `ctest -R sfml` w budowie z SFML;
3. testy 2D tego wiązania i korpus pikseli, bez zmian;
4. nic istniejącego się nie ruszyło: zmiany nie dotykają `backends/capi/`,
   `runtime/` ani `model/`; lista eksportów `libipropc` jest ta sama przed
   i po; `scripts/check_abi_boundary.py` przechodzi; budowa
   z `IPROP_RUNTIME_ONLY` bez nowej opcji nie ma celu `iprop_model`;
5. kontrola dokumentacji z 5.4 i zapisane rozmiary przed i po: nadzbiór,
   `libiprop_jni.so` na ABI, `libCIprop.a`.

Lokalnie (macOS arm64) nie uruchomimy: MSVC i eksportu z `iprop3dc.dll`,
testów na emulatorze Androida, Edytora Unity, wycinków iOS poza kompilacją.
To zostaje rundzie CI na słowo użytkownika i jego Edytorowi; w stanie wpisać
jako niesprawdzone, nie jako zaliczone. TSan dla T7 i `capi3d_threads`
uruchomić raz lokalnie.

## 7. Ryzyka i pytania otwarte

Pytania; żadnego nie rozstrzygnąłem sam:

| # | Pytanie | Co wiadomo |
| --- | --- | --- |
| 1 | **SDK.** Czy przy wydaniu 1.2 nadzbiór wchodzi do SDK (biblioteka, `iprop3d.h`, plik `.pc`, cel CMake)? | do tego czasu użytkownik SDK ma źródła wiązań z klasami modeli i sam buduje bibliotekę |
| 2 | **Kontrakt wątków.** Czy wolno wołać `iprop3d_model_parameters_create` i `iprop_parameters_clone`, gdy inny wątek generuje ten sam model, i kto dopisuje to zdanie do `iprop3d.h`? | nagłówek gwarantuje tylko równoległe `iprop3d_generate`; kod tylko czyta (`backends/capi/parameters.cpp`) i Godot już tak robi; bez tego zdania decyzja 11 stoi na implementacji, nie na kontrakcie |
| 3 | **Windows.** Czy `IPROP_CAPI_BUILD` wystarcza, żeby `iprop3dc.dll` eksportował oba zestawy symboli, i czy zlib łączy się jak w `iprop_capi`? | nadzbioru nie budował nigdy MSVC; odpowie dopiero CI |
| 4 | **Android.** Ile rośnie `libiprop_jni.so` i czy ma powstać wariant AAR bez modeli? | pomiar w etapie 4; decyzja 14 |
| 5 | **Unity.** Kto uruchamia testy Edytora dla `ToMesh()`? Jakie są poprawne konwersje konwencji? Czy `Material` ma wejść i dla którego potoku? Czy na iOS z IL2CPP importy `__Internal` wymagają symboli modeli w archiwum zawsze? | repozytorium nie odpowiada na żadne z nich; Edytor nie działa w CI (`TODO.md`) |
| 6 | **SceneKit.** Jaki jest początek UV i układ źródła stycznych? | do potwierdzenia dokumentacją Apple i renderem, nie założeniem |
| 7 | **Python.** Czy README ma zalecać `IPROP_LIBRARY` ustawione na nadzbiór, żeby 2D i modele dzieliły bibliotekę i żetony? | nadzbiór eksportuje całe C API 2D |
| 8 | **`known`.** Czy niski poziom z samymi rekordami jest potrzebny w każdym wiązaniu, czy wystarczy `TextureCache`? | JS ma oba |
| 9 | **M6.** Czy praca rusza w trakcie M6? | wspólne pliki to `CHANGELOG.md`, `docs/ci.md` i wizja; M6 dopisze model do `corpus` w `model-conformance.js`, a test z 5.1 punkt 4 wymusi go w korpusie wiązań |

Ryzyka:

| Ryzyko | Skutek | Jak je ograniczyć |
| --- | --- | --- |
| API 0.3 zmieni się przed zamrożeniem (etap testów po M6, strojenie cen) | pięć wiązań do poprawienia naraz | dokładny test wersji (decyzja 5) i skrypt z 5.1; korpus z bieżących narzędzi, więc zmiana cen nie wymaga przypinania od nowa |
| dwie kopie kodu 2D w procesie (wariant C) | uchwyt z `ipropc` podany modelowi | obiekt modelu woła jedną bibliotekę; Python ma już test tożsamości biblioteki przy żetonie, C# ma osobne `Native3D` |
| ręczne lustra struktur w `ctypes` i C# | ciche przesunięcie pola po zmianie nagłówka | T9 i skrypt wersji; na 32-bitowym Androidzie w Unity luster nie sprawdza żaden test |
| pomocniki silników bez bramki co do bitu | odwrócone ściany albo wypukłości, jak błąd `Basis` w Godocie (wizja 17.4) | test struktury jak `check_mesh` i render płyty z wypukłością; Unity dopiero po Edytorze użytkownika |
| wątki bez TSan | wyścig widoczny dopiero u użytkownika | `capi3d_threads` i T7 raz pod TSan; M5 go nie uruchamiał (wizja 19.4) |
| korpus modeli pod ASan: `binding_python` biegnie tam w budowie Debug i dwa razy (`malloc` i `pymalloc` w `check-python.py`), z cięższymi modelami (hydrant, bench) | zadanie sanitizerów wyraźnie dłuższe | zmierzyć; `model_bindings.py` przyjmuje skróconą listę przypadków, jeśli czas jest za długi |
| listy z 5.3 pominięte | plik nie trafia do SDK, testu albo AAR | bramka 2 uruchamia każdą z tych ścieżek |
| stare artefakty (`Artifacts/CIprop.xcframework`, `Plugins/`) bez modeli | błąd kompilacji albo `DllNotFoundException` | decyzja 4: definicja `IPROP_MODELS` zależy od nagłówka w artefakcie, C# zgłasza brak wtyczki wprost |
