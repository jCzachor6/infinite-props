# Realizacja review develop → main

Cel: wdrożyć całą listę z `CODE_REVIEW_DEVELOP_MAIN.md`, zachowując historyczne golden 0.1/0.2. Raport review opisuje stan sprzed napraw; ten plik śledzi wdrożenie i dowody sprawdzenia. Status `[ ]` oznacza, że wymaganie nie jest jeszcze ukończone i zweryfikowane.

## Wymagania

- [x] R1 / A6: typowane ramki walidatora, poprawne scalenia i osiągalność; bezpieczne mul/div; granica C ABI; testy gałęzi i odtworzenie awarii.
- [x] R2: jawne reguły NaN nowych operacji, specyfikacja, porównanie surowych bitów native/WASM, historyczne golden.
- [x] R3: jedna oczekiwana wersja produktu dla instalacji/wydania, aktualne CI i dokumentacja, przechodzące shared/static install smoke i release check.
- [x] R4 / A3: wspólna geometria części, `D4Transform`, fabryka `PartRenderPlan`, skala/origin/zagnieżdżenia i własności transformacji.
- [x] R5 / A5: typowana pula zmiennych tymczasowych z czasem życia, test długiej sekwencji i zachowania efektów/RNG.
- [ ] A1: wspólny `OpcodeSpec` używany przez nazwy/assembler/walidator oraz osobny `BuiltinSpec` dla semantyki i emisji.
- [ ] A2: resolver planów wywołań i typowany plan wyrażeń, usunięcie powtarzanego typowania przez emiter, testy kolejności argumentów i efektów.
- [x] A4: czyste moduły matematyczne i adaptery VM, wspólny wielomian quintic/fade, fabryka przygotowanego kernela Béziera.
- [ ] A5: wspólne rozstrzyganie palet i emisja indeksu/ramp.
- [x] A7: wspólna lista źródeł runtime dla native/WASM, świeże buildy obu konfiguracji.
- [ ] Ramp: składanie czystych stałych, szybszy wybór dynamicznego przedziału, jawny kontrakt fuel, pomiary i testy efektów ubocznych.
- [x] Bézier: pomiar przygotowanego kernela, zachowanie bitów i normatywnego fuel.
- [x] Diagnostyka: pełna walidacja transformacji w semantyce, lokalizacje CLI zrozumiałe dla edytora.
- [x] API vec2: dostępne len/dot/rot w `.ips`, spójna konwencja dodatnich obrotów, specyfikacja i testy.
- [ ] Wspólny zestaw przypadków granicznych: NaN/inf/±0, przepływ typów, limity zasobów i relacja obrazu do geometrii.
- [ ] Końcowy audyt: wszystkie wymagania sprawdzone, native + backendy + sanitizery + WASM + instalacja/wydanie, aktualna dokumentacja i brak niezamierzonych zmian golden.

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

W toku: BuiltinSpec i typowany plan wywołań.
