# Realizacja review develop → main

Cel: wdrożyć całą listę z `CODE_REVIEW_DEVELOP_MAIN.md`, zachowując historyczne golden 0.1/0.2. Raport review opisuje stan sprzed napraw; ten plik śledzi wdrożenie i dowody sprawdzenia. Status `[ ]` oznacza, że wymaganie nie jest jeszcze ukończone i zweryfikowane.

## Wymagania

- [x] R1 / A6: typowane ramki walidatora, poprawne scalenia i osiągalność; bezpieczne mul/div; granica C ABI; testy gałęzi i odtworzenie awarii.
- [ ] R2: jawne reguły NaN nowych operacji, specyfikacja, porównanie surowych bitów native/WASM, historyczne golden.
- [ ] R3: jedna oczekiwana wersja produktu dla instalacji/wydania, aktualne CI i dokumentacja, przechodzące shared/static install smoke i release check.
- [ ] R4 / A3: wspólna geometria części, `D4Transform`, fabryka `PartRenderPlan`, skala/origin/zagnieżdżenia i własności transformacji.
- [x] R5 / A5: typowana pula zmiennych tymczasowych z czasem życia, test długiej sekwencji i zachowania efektów/RNG.
- [ ] A1: wspólny `OpcodeSpec` używany przez nazwy/assembler/walidator oraz osobny `BuiltinSpec` dla semantyki i emisji.
- [ ] A2: resolver planów wywołań i typowany plan wyrażeń, usunięcie powtarzanego typowania przez emiter, testy kolejności argumentów i efektów.
- [ ] A4: czyste moduły matematyczne i adaptery VM, wspólny wielomian quintic/fade, fabryka przygotowanego kernela Béziera.
- [ ] A5: wspólne rozstrzyganie palet i emisja indeksu/ramp.
- [x] A7: wspólna lista źródeł runtime dla native/WASM, świeże buildy obu konfiguracji.
- [ ] Ramp: składanie czystych stałych, szybszy wybór dynamicznego przedziału, jawny kontrakt fuel, pomiary i testy efektów ubocznych.
- [ ] Bézier: pomiar przygotowanego kernela, zachowanie bitów i normatywnego fuel.
- [ ] Diagnostyka: pełna walidacja transformacji w semantyce, lokalizacje CLI zrozumiałe dla edytora.
- [ ] API vec2: dostępne len/dot/rot w `.ips`, spójna konwencja dodatnich obrotów, specyfikacja i testy.
- [ ] Wspólny zestaw przypadków granicznych: NaN/inf/±0, przepływ typów, limity zasobów i relacja obrazu do geometrii.
- [ ] Końcowy audyt: wszystkie wymagania sprawdzone, native + backendy + sanitizery + WASM + instalacja/wydanie, aktualna dokumentacja i brak niezamierzonych zmian golden.

## Decyzje

- Zlecenie wdrożenia całej listy obejmuje opisane w review korekty kontraktu 0.3. Specyfikacja jest aktualizowana przed zachowaniem. Historyczne specyfikacje 0.1/0.2 pozostają zamrożone.
- Refaktory używają typów wartościowych, statycznych opisów i fabryk planów; nie wprowadzają osobnej hierarchii klas wirtualnych dla każdej instrukcji.
- Nie wykonujemy commitów, push ani publikacji w ramach tego zlecenia.

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

W toku: R3; następnie deterministyczność i wydzielenie matematyki.
