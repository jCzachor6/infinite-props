# Realizacja review develop → main

Cel: wdrożyć całą listę z `CODE_REVIEW_DEVELOP_MAIN.md`, zachowując historyczne golden 0.1/0.2. Raport review opisuje stan sprzed napraw; ten plik śledzi wdrożenie i dowody sprawdzenia. Status `[ ]` oznacza, że wymaganie nie jest jeszcze ukończone i zweryfikowane.

**Stan na 2026-10-02: wszystkie wymagania zakończone.** Końcowy audyt
poniżej dotyczy stanu `ad7469a` na `develop`; późniejszy commit zamyka
wyłącznie dokumentację i raport. Każdy etap implementacji ma osobny
lokalny commit.

## Wymagania

- [x] R1 / A6: typowane ramki walidatora, poprawne scalenia i osiągalność; bezpieczne mul/div; granica C ABI; testy gałęzi i odtworzenie awarii.
- [x] R2: jawne reguły NaN nowych operacji, specyfikacja, porównanie surowych bitów native/WASM, historyczne golden.
- [x] R3: jedna oczekiwana wersja produktu dla instalacji/wydania, aktualne CI i dokumentacja, przechodzące shared/static install smoke i release check.
- [x] R4 / A3: wspólna geometria części, `D4Transform`, fabryka `PartRenderPlan`, skala/origin/zagnieżdżenia i własności transformacji.
- [x] R5 / A5: typowana pula zmiennych tymczasowych z czasem życia, test długiej sekwencji i zachowania efektów/RNG.
- [x] A1: wspólny `OpcodeSpec` używany przez nazwy/assembler/walidator oraz osobny `BuiltinSpec` dla semantyki i emisji.
- [x] A2: resolver planów wywołań i typowany plan wyrażeń, usunięcie powtarzanego typowania przez emiter, testy kolejności argumentów i efektów.
- [x] A4: czyste moduły matematyczne i adaptery VM, wspólny wielomian quintic/fade, fabryka przygotowanego kernela Béziera.
- [x] A5: wspólne rozstrzyganie palet i emisja indeksu/ramp.
- [x] A7: wspólna lista źródeł runtime dla native/WASM, świeże buildy obu konfiguracji.
- [x] Ramp: składanie czystych stałych, szybszy wybór dynamicznego przedziału, jawny kontrakt fuel, pomiary i testy efektów ubocznych.
- [x] Bézier: pomiar przygotowanego kernela, zachowanie bitów i normatywnego fuel.
- [x] Diagnostyka: pełna walidacja transformacji w semantyce, lokalizacje CLI zrozumiałe dla edytora.
- [x] API vec2: dostępne len/dot/rot w `.ips`, spójna konwencja dodatnich obrotów, specyfikacja i testy.
- [x] Wspólny zestaw przypadków granicznych: NaN/inf/±0, przepływ typów, limity zasobów i relacja obrazu do geometrii.
- [x] Końcowy audyt: wszystkie wymagania sprawdzone, native + backendy + sanitizery + WASM + instalacja/wydanie, aktualna dokumentacja i brak niezamierzonych zmian golden.

## Decyzje

- Zlecenie wdrożenia całej listy obejmuje opisane w review korekty kontraktu 0.3. Specyfikacja jest aktualizowana przed zachowaniem. Historyczne specyfikacje 0.1/0.2 pozostają zamrożone.
- Refaktory używają typów wartościowych, statycznych opisów i fabryk planów; nie wprowadzają osobnej hierarchii klas wirtualnych dla każdej instrukcji.
- Zgodnie z późniejszą dyspozycją użytkownika każda ukończona i sprawdzona
  poprawka otrzymuje osobny lokalny commit. Push i publikacja nie są zlecone.

## Weryfikacja i postęp

R1 / A6: build native i CTest 6/6 PASS. Nowe `control_validation_test.cpp`
sprawdza typy na złączeniach, obie krawędzie `br_if`, osiągalność, strukturę
`else` oraz kolejność wyników helpera. Testy defense wymuszają błędne typy
mul/div bez walidacji. C-only conformance mutuje poprawny plik do obu
reprodukcji R1 i otrzymuje `IPROP_E_FORMAT` przed wykonaniem.

R5: `TemporaryLocals` używa zakresowych uchwytów; emiter korzysta z puli
w mnożeniach, rozpakowaniu wektorów, round, logice i paletach. Test 2048
kolejnych mnożeń w obu kolejnościach wymaga najwyżej dwóch lokalnych slotów
(poza parametrami). Testy mnożeń/RNG: 5 przypadków, 242 asercje PASS;
pełny CTest ponownie 6/6 PASS po tej zmianie.

A7: `cmake/RuntimeSources.cmake` zasila oba niezależne buildy. Świeże
konfiguracje Release w `/tmp/iprop-fixes.C5KhDL/native` i `wasm` zbudowane
poprawnie. Porównanie między nimi: 7 golden, 116 obrazów i outputów,
19 powodów trapów PASS (`shared-conformance.log`).

R3: install smoke odczytuje oczekiwaną wersję z głównego CMake, a CI
korzysta z domyślnej wersji skryptu wydania. Ustawiono release target Godot
w workflow. Dokumentacja wskazuje produkt 0.3.0 i nadal API C 0.2.
`python3 scripts/release.py --check` PASS po zbudowaniu Godot
`template_release`: CTest 6/6, przeniesiona instalacja, konsumenci C
shared/static oraz zainstalowany Godot (8 corpus entries + outputs).
Log: `/tmp/iprop-fixes.C5KhDL/release-check.log`; archiwa nie były tworzone.

R2: nowe operacje wektorowe kanonizują NaN do `0x7FC00000` przez klasyfikację
bitów; konstrukcja, odczyt komponentów i historyczne operacje skalarne
zachowują bity. Wspólny korpus 22 wzorców obejmuje ±0, subnormale,
nieskończoności i NaN z różnymi znakami/payloadami, także sygnalizujące.
CTest 6/6 PASS; test natywny 9118 asercji PASS. WASM: 6292 dokładne
porównania bitów, 116 obrazów/outputów, 7 golden i 19 trapów PASS.
Harness Node 9/9 PASS. Log: `/tmp/iprop-fixes.C5KhDL/vector-conformance.log`.

R4 / A3: `D4Transform` centralizuje mapy pikseli, punktów i prostokątów,
kompozycję, odwrotność i składanie parametrów. `PartRenderPlan::create`
przygotowuje skalę, wymiary, origin i fuel; odrzuca złe zakresy przed
konwersją/zaalokowaniem. D4 zamieniające osie renderują z zamienionymi
skalami, zgodnie z poprawionym §10 specyfikacji. Zachowana jest dotychczasowa
konwencja pozycjonowania i lokalnych outputów. Testy obejmują 64 kompozycje,
odwrotności, relację pixel-center/point, wszystkie D4 przy trzech skalach,
niezerowe origin/at, helpery, ownership i zagnieżdżone zamiany osi.
CTest 6/6 PASS; native/WASM: 7 golden, 116 obrazów/outputów, 19 trapów,
6292 wartości bitowych PASS (`d4-conformance.log`).

Diagnostyka: semantyka i kompilator używają `resolvePartTransform` oraz
wspólnego składania stałych w warstwie języka. Błędne domeny, zmienne,
typy i powtórzone argumenty transformacji są odrzucane przed emisją.
`SourceError` zachowuje ścieżkę importu także przy parsowaniu, cyklach,
brakujących plikach i błędach planowania/assembly. Edytor obsługuje faktyczny
jednoliniowy format CLI oraz historyczny format dwuliniowy. CTest 7/7 PASS,
w tym nowy test integracyjny edytor + prawdziwy `ipc` dla pięciu etapów błędu.

API vec2: `.ips` udostępnia `vec2.len`, `vec2.dot` i `vec2.rot`, w tym
promocję całkowitego argumentu kąta. `vec2.rot` i `polar` stosują teraz
konwencję CCW w osi Y w dół, zgodną z D4; poprawiono specyfikację 0.3,
dokumentację i kolorowanie składni. Historyczny skalarny atan2 i sektor fold
zachowano. CTest 7/7 PASS; native/WASM: 121 obrazów/outputów, 6292 porównania
bitowe, 19 trapów i 7 golden PASS (`vec2-api-conformance.log`).

A4, etap 1: czyste moduły `Scalar`, `Vector`, `Sdf`, `Bezier`, `Curves`
i `Color` nie zależą od Execution. Osobne adaptery VM obsługują stos,
trapy i ip; `arithmetic.cpp` ma 301 linii zamiast 1181. Wspólny
`quinticPolynomial` zasila noise i clamped quintic bez zmiany zaokrągleń.
CTest 7/7 PASS; native/WASM: 7 golden, 121 obrazów/outputów, 19 trapów,
6292 wyniki bitowe PASS (`math-extract-conformance.log`).
Pozostaje fabryka i pomiar przygotowanego kernela, więc A4 nie jest jeszcze
odhaczone jako całość.

A4, etap 2 / Bézier: fabryki `BezierKernel::quadratic/cubic` przygotowują
różnice punktów, drugie pochodne, skończoność i degenerację; rasteryzator
używa jednego kernela na całą krzywą. 6864 zapytania dają hashe surowych
f32 identyczne z 9bda0c2. Normatywne fuel i obraz benchmarku bez zmian
(29365; FNV 31c9b9f5ac360167). Mediana 7 naprzemiennych serii po 80 generacji:
3,55157 ms → 3,43472 ms, ok. 3,3% mniej czasu na lokalnym fixture.
Procedura i ograniczenia: `benchmarks/README.md`. CTest 7/7 i pełne
porównanie native/WASM PASS (`bezier-conformance.log`).

A1, etap 1: `OpcodeSpec` jest jednym źródłem 134 mnemoników, operandów,
minimalnych wersji, polityki aux i 106 stałych sygnatur stosu (w tym
alternatywa scalar/vec2). Używają go assembler, disassembler i walidator.
Sterowanie, metadane i odczyt slotów mają nadal jawną walidację.
Test przekrojowy porównuje sygnatury z wykonaniem VM, odrzucanie każdej
niezgodnej klasy argumentu, niedomiar stosu, aux i bramki wersji także
w martwym kodzie. CTest 7/7 oraz pełne native/WASM PASS
(`opcode-conformance.log`). Pozostaje osobny opis builtinów języka.

Dodatkowy fix wykryty podczas A1: `round(i32)` wykonuje promocję do f32
zgodnie z sygnaturą, zamiast generować program odrzucany przez walidator.
Test obejmuje literał i helper; półprzezroczysty piksel wykrywa powtórne
wywołanie helpera. CTest 7/7 i test celowany PASS.

A1, etap 2: `BuiltinSpec` opisuje 59 builtinów języka, parametry, wynik,
przeciążenia z przypisanym opcode oraz specjalne emisje round/poly.
Semantyka i emiter współdzielą dobór przeciążenia; usunięto łańcuch
powtórzonych sygnatur i mapowania nazw w emiterze. Test przekrojowy
kompiluje i wykonuje każde przeciążenie, sprawdza promocje oraz zgodność
z niezależnym kontraktem VM. CTest 7/7; native/WASM/golden PASS
(`builtin-conformance.log`).

A5: `ResolvedPalette` współdzieli walidację, typ i zdekodowane wartości
między semantyką i planowaniem. `PaletteLowerer` skupia wybór elementu,
interpolację i czas życia slotów. Pierwszy refaktor zachowuje dotychczasową
emisję i fuel; optymalizacja jest osobnym punktem. Test sprawdza jednakowe
nazwy palet o różnych typach w root i części oraz dwa aliasy tego samego AST.
CTest 7/7 i pełne native/WASM/golden PASS (`palette-conformance.log`).

A2, etap 1: plan przechowuje typy i czyste stałe każdego wyrażenia
wykonywalnego. `Typer` pamięta wynik w zakresie funkcji; zapisywanie
i odtwarzanie zakresu obejmuje cache. Emiter nie posiada silnika typowania.
`ConstantFolder` pamięta również nieudane złożenia, usuwając ponowną
rekurencję dynamicznych poddrzew. Test 512 wywołań odwiedza ponownie
wszystkie poddrzewa i nadal wymaga dokładnie 512 rozstrzygnięć; osobny
alias ma niezależny cache. Plan działa po zniszczeniu plannera.
CTest 7/7 i native/WASM/golden PASS (`typed-plan-conformance.log`).

Dodatkowy fix argumentów części: odtworzono błąd walidacji programu przy
przekazaniu i32 do uniformu f32. Emiter wykonuje teraz dozwoloną promocję,
a semantyka odrzuca powtórzone nazwy wszystkich argumentów. Test helpera
z półprzezroczystym pikselem potwierdza pojedynczą ewaluację. Specyfikacja
0.3 opisuje obie reguły. CTest 7/7 PASS.

A2, etap 2: `CallResolver` buduje wariant `BuiltinCall`/`UserCall`/
`PartCall`/`RampCall`. Plan zawiera przeciążenie, promocje, kolejność
argumentów, lokalizację, zdekodowane domyślne uniformy, indeks celu i D4.
Emiter nie wyszukuje nazw, przeciążeń, palet ani transformacji. Zasady
typowania wywołań są w osobnym `TypingCalls.cpp`. Test zapisuje kolejność
22 efektów i bity RNG w builtinach, round, poly, helperach, uniformach,
seed/variant/archetype, placement i singleton ramp. Zachowano i opisano
historyczną kolejność ewaluacji. CTest 7/7 oraz pełne native/WASM/golden
PASS (`call-plan-conformance.log`).

Granice koloru: `color.shade` dodaje w i64 przed ograniczeniem do kanału,
a `color.mix` stosuje jawny clamp NaN→0. Korpus ujawnił, że optymalizacja
składania min/max zmieniała obsługę sygnalizującego NaN w Release; wspólna
funkcja klasyfikuje teraz bity zamiast polegać na predykacie FP.
Native i WASM weryfikują te same 22 wzorce float, a shade skrajne i32;
29 kolorów ma niezależne oczekiwane RGBA. CTest 7/7, pełna konformancja
(`color-conformance.log`) i 137 asercji color pod ASan/UBSan/
float-cast-overflow PASS. Historyczne golden bez zmian.

Granice round: odtworzono 7 błędnych wyników spośród 16 przypadków
(INT_MIN, −inf, sąsiedzi 0.5 i duże nieparzyste liczby całkowite).
Emisja porównuje część ułamkową z 0.5 przed korektą floor/ceil, a dopiero
na końcu wykonuje nasycającą konwersję. Specyfikacja opisuje reguły i fuel.
CTest 7/7 PASS; fixture `.ips` sprawdza porównania i32 przed zamianą na
output f32, aby nie maskować błędu o 1. Native/WASM: 126 obrazów/outputów,
7 golden, 19 trapów, 6292 wyniki wektorów i 29 kolorów PASS
(`round-conformance.log`).

Ramp: czyste stałe są obliczane w planie tym samym algorytmem f32/koloru;
dynamiczny indeks wybiera element w drzewie z O(log N) porównań. Wspólna
emisja indeksowania zachowuje ostatni element dla ujemnych i za dużych
indeksów. Spec §12.11 jawnie dopuszcza zmianę fuel nowej emisji, bez zmiany
kosztu instrukcji/historycznych binariów. Benchmark 64×64 dla N=1/8/32/64/128:
stałe zawsze 626 B i 71309 fuel, dynamiczne N=128 296589 fuel zamiast
wyczerpania miliona. Wszystkie obrazy zgodne z poprzednią emisją, także
N=64/128 po podniesieniu wyłącznie budżetu kopii poprzedniego bytecode.
Procedura i tabela: `benchmarks/ramp.py` i `benchmarks/README.md`.
5460 asercji w 11 testach ramp/palet, CTest 7/7 oraz native/WASM:
131 obrazów/outputów, 7 golden, 19 trapów, 6292 wyniki wektorów i 29 kolorów
PASS (`ramp-conformance.log`). Testy obejmują liniową implementację
referencyjną, każdy stop i sąsiednie floaty, surowy korpus, RNG/rysowanie,
pojedynczą ewaluację, trapy i ograniczenie fuel.

## Końcowy audyt wymagań

Każdą pozycję pierwotnego review porównano z bieżącą implementacją i
zakresem testów; poniższe dowody odnoszą się do kodu po wszystkich naprawach.

| Wymaganie | Bieżący dowód implementacji i sprawdzenia |
| --- | --- |
| R1 / A6 | `ControlFrame` w `runtime/validator.cpp`; kontrola typów w `vector_ops.cpp`; wszystkie wyjątki z load/generate przechwycone w C API. `control_validation_test.cpp`, `defense_test.cpp` i mutowany plik `vector-branch` w C-only conformance. |
| R2 | `math/Float.hpp` i `math/vector.cpp`; 22 surowe wzorce współdzielone przez testy C++ i Node. 6292 porównania bitów native/WASM, oba znaki/payloady NaN, sNaN, inf, ±0 i subnormale. Transport zachowuje wejściowe bity. |
| R3 | Wersja z głównego CMake, oficjalny install smoke, CI bez starego argumentu 0.2.0. Końcowy `release.py --check`: przeniesiona instalacja, konsumenci C shared/static i zainstalowany Godot PASS. |
| R4 / A3 | `D4Transform` i `PartRenderPlan::create`; test 64 kompozycji, odwrotności, wszystkich D4 przy asymetrycznej skali/origin/at i zagnieżdżonych wywołań. Obraz porównany z niezależnymi granicami prostokąta i punktem-kotwicą w jego środku. Test kotwicy domknięty w `ad7469a`. |
| R5 | `TemporaryLocals::Lease`; 2048 kolejnych mnożeń w obu kolejnościach, maksymalnie dwa dodatkowe sloty; porównanie z sekwencyjną referencją RNG, zagnieżdżeń i round. |
| A1 | 134 deskryptory `OpcodeSpec`, 59 `BuiltinSpec`; wspólne nazwy/assembler/walidator oraz dobór przeciążenia. Test każdej stałej sygnatury VM i każdego przeciążenia języka. |
| A2 / wielokrotne typowanie | Typy/stałe w `PlannedFunction`, warianty w `CallResolver`; emiter korzysta z gotowych planów. Test 512 wywołań bez ponownego rozstrzygania poddrzew oraz 22 uporządkowanych efektów i bitów RNG. |
| A4 / Bézier | Czyste moduły Scalar/Vector/Sdf/Bezier/Curves/Color i adaptery VM; wspólny quintic. Kernel przygotowywany raz na krzywą, 6864 zapytania zachowują stare hashe; pomiar i niezmienione fuel/obraz w `benchmarks/README.md`. |
| A5 | Wspólne `ResolvedPalette`, `PaletteLowerer` i typowana pula slotów; test palet tego samego imienia w root/części i wspólnego AST pod dwoma aliasami. |
| A7 | `cmake/RuntimeSources.cmake`; końcowe świeże buildy Release native i WASM wykonane od konfiguracji do linkowania, a następnie porównane ze sobą. |
| Ramp | Planowanie czystych stałych, drzewo dynamiczne, kontrakt fuel w §12.11; benchmark 1/8/32/64/128 stopów, liniowa referencja, sąsiedzi stopów, efekty i trapy. |
| Diagnostyka | `resolvePartTransform`, ścieżka w `SourceError`, format CLI; test edytora uruchamia prawdziwy ipc dla pięciu etapów błędu, w tym importów. |
| API vec2 / obrót | Publiczne len/dot/rot, parser i highlighting; zgodna konwencja CCW vec2/polar/D4, testy źródłowe i fixture native/WASM. |
| Wspólne przypadki graniczne | Korpus f32 dla wektorów, koloru i ramp; typowane gałęzie/C ABI, granice rozmiarów i konwersji części, limity stosu/wywołań/locals oraz ograniczenie fuel dużych ramp. |

### Końcowe uruchomienia

Artefakty lokalne: `/tmp/iprop-fixes.C5KhDL/` (katalog tymczasowy; trwałe
przypadki, skrypty i oczekiwane wyniki są w repozytorium).

| Sprawdzenie | Wynik / log |
| --- | --- |
| Build Release z C API, SFML i Godot | PASS; końcowy kod zbudowany przed weryfikacją wydania. |
| Pełny CTest | 7/7 PASS, w tym C API, SFML render/errors/conformance, Godot, smoke i diagnostyka edytora; `final-ctest.log`. |
| Natywny Catch2 | 387 przypadków, 333222 asercje PASS; `final-native-tests.log`. |
| ASan + UBSan + float-cast-overflow, Debug | Pełny rebuild i CTest 3/3 PASS (C API, cały smoke, diagnostyka); `final-sanitized-build.log`, `final-sanitized-test.log`. |
| Świeży native Release bez zależności graficznych | PASS w `final-native`; `final-native-configure.log`, `final-native-build.log`. |
| Świeży Emscripten 6.0.10 Release | PASS w `final-wasm`; `final-wasm-configure.log`, `final-wasm-build.log`. |
| Konformancja obu świeżych buildów | 7 historycznych golden, 131 obrazów/outputów, 19 trapów, 6292 bitowe wyniki wektorów, 29 kolorów PASS; `final-conformance.log`. |
| Testy harnessu Node | 9/9 PASS; `final-node-harness.log`. |
| Oficjalny release/install check | PASS: narzędzia i C shared/static z przeniesionej instalacji, Godot 8 corpus entries + outputy + PNG; `final-release-check.log`. Archiwów nie tworzono. |
| Zamrożone artefakty | `git diff main...HEAD -- tests/golden spec/iprop-v0.1.md spec/iprop-v0.2.md` pusty; brak zmian binariów przykładów podczas napraw względem 367a158. |
| Higiena diffu | `git diff --check main...HEAD` PASS; aktualne README, HTML5 README, changelog i spec 0.3. |

Sprawdzenia wykonano lokalnie na macOS arm64. Zdalne CI oraz natywne
buildy Linux/Windows nie były uruchamiane; nie stanowią deklarowanych
wyników tego audytu. C API zachowuje wersję 0.2. Zmiany kontraktu 0.3
(NaN, kierunek kąta, skala D4 i nowa emisja/fuel) są opisane w specyfikacji;
historyczne specyfikacje i golden pozostają zamrożone.
