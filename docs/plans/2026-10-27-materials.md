# Plan: Materiały w Infinite Props (feat/materials)

> Status: **Draft** — ustalenia po wywiadzie decyzyjnym, przed akceptacją. Gałąź: `feat/materials` (z `develop` @ `8259a1b`). Draft staje się Final dopiero po jawnej akceptacji użytkownika; zaznaczenie pól w sekcji „Bramka akceptacji" oznacza zgodę na budowę dokładnie w tym kształcie. Akceptacja tego dokumentu nie zatwierdza późniejszych etapów (specyfikacja, implementacja) — każdy etap wraca do osobnej akceptacji.

## Cel

- [ ] Dodać do `.ips` nazwany, wielokrotnego użytku **materiał**: deklarowany raz opis wyglądu powierzchni (barwa, nazwane odcienie, krawędź, zwężanie), stosowany spójnie w całym propie, między partami i między propami.
- [ ] Zastąpić dzisiejszą ręczną konwencję z `docs/language/idioms.md` (gradient bazowy + `color.shade/mix/hsv` + `stepper(dark to light)` + osobne `weather`/noise/maski) jednym bytem języka, bez zmiany zachowania istniejących programów poza świadomą zmianą specyfikacji pod nowy typ VM.

## Ustalone decyzje (pytania z 2026-10-27)

- [x] **Wydanie:** materiały wchodzą w release **0.8**. Brak wymogu wstecznej kompatybilności: format, język i C API/ABI mogą się zmienić przełamująco, a stare `.iprop` mogą wymagać rekompilacji. Ostrożnie znaczy: tylko świadome, udokumentowane zmiany przełamujące, z wpisem migracyjnym i aktualizacją specyfikacji przed kodem; nic nie łamiemy przy okazji ani po cichu.
- [x] **Zakres v1:** kolory + `edge` + `taper`. Bez tekstury i zużycia w pierwszej wersji.
- [x] **Technika:** nowy typ VM (nie lowering). Zmiana specyfikacji `spec/iprop-v0.7.md` przed kodem; akceptowana rekompilacja istniejących `.iprop` ze źródeł `.ips`.
- [x] **Forma:** osobny byt w osobnym pliku. Plik materiału nie ma assetu i nie wolno definiować materiału w pliku assetu. Jeden plik może zawierać jeden materiał albo bibliotekę wielu materiałów. Plik materiału może mieć własne `palette`/`const`, których pola materiałów używają.
- [x] **Import:** ten sam import co party: `import bark from "bark.ips"`. Bez osobnej składni importu materiałów.
- [x] **Tworzenie instancji:** w `draw` przez wywołanie nazwy definicji, np. `m = Bark()`. Utworzenie losuje wszystkie zakresy pól raz, każde pole osobno, w kolejności zapisu w definicji. Zakres pola może mieć `with` (np. `base = #a to #b with gaussian(...)`).
- [x] **Użycie:** instancja przekazywana jako parametr nazwanej definicji, np. `branch(m: Bark)`. Każda definicja jest osobnym typem: kompilator zna jej pola. Part przyjmuje materiał bezpośrednio jako parametr (nie przez numer wyboru w v1; numer wyboru przez input pozostaje możliwą konwencją użytkownika poza językiem).
- [x] **Przechowywanie:** lokale, parametry i zwroty helperów, `list<Bark>` (lista tylko jednej definicji, bez mieszania rodzajów) oraz `const` na poziomie assetu. `const` z zakresem losuje raz przy pierwszym użyciu. Brak `sampler<Bark>` i `stepper<Bark>` w v1.
- [x] **Host input:** materiał nie jest inputem assetu dla hosta/gry (jak `layer`/`mask`).
- [x] **Niemutowalność:** instancja zamrożona po utworzeniu; nadpisanie pola (np. `m.base = #ff0000`) to błąd.
- [x] **Warianty:** brak dziedziczenia/nadpisywania definicji w v1; wariant to osobna definicja.
- [x] **Główny przykład v1:** drzewo z gałęziami (osobna grubość i kąt per gałąź, wspólna kora z materiału).
- [x] **Rysowanie:** materiał daje tylko pola. Gałąź liczy i rysuje helper ręcznie (np. zwężany `poly`) z `m.base`, nazwanych odcieni, `m.edge`, `m.edge_width` i `m.taper`. Bez nowego prymitywu `branch(...)` w języku.

## Jak wygląda materiał (wizja po ustaleniach)

- [ ] **Definicja (tylko osobny plik), pola specjalne:** `base` (wymagane, `color`: stała lub zakres), `edge` (opcjonalne, `color`: stała lub zakres; gdy brak, `edge` = `base`, czyli krawędź ma kolor wypełnienia), `taper` (opcjonalne, `f32`: stała lub zakres; gdy brak, `1.0` = brak zwężania), `edge_width` (opcjonalne, `i32` w pikselach: stała lub zakres; gdy brak, `0`).
- [ ] **Pola nazwane przez autora:** dowolne dodatkowe pola typów `color`, `i32`/`f32` i `vec2`, każde jako stała lub zakres, np. odcienie `shadow`/`highlight` zapisywane wprost jako kolory/zakresy (nie wyliczane od `base`). Brak sztywnej listy tonów: każdy materiał nazywa odcienie sam, a dostęp to pola instancji, np. `m.shadow`.
- [ ] **Dostęp:** pola instancji `m.base`, `m.edge`, `m.taper`, `m.edge_width` oraz pola nazwane z definicji. Brak `m.tone(n)` i brak pozycji `0..1` w v1.
- [ ] **Geometria zostaje poza materiałem:** pozycja, kąt, długość i grubość konkretnego kształtu podaje wywołanie rysujące (np. `branch(start, angle, length, width, m)`), materiał nadaje mu krawędź, zwężanie i cieniowanie.

- [x] **Zmiana po review (2026-10-28):** z pól specjalnych zostaje tylko wymagane `base` (`color`). `edge`, `taper` i `edge_width` są zwykłymi polami autora, bez wartości domyślnych w języku. Pole może czytać wcześniejsze pola po nazwie (np. `shadow = color.mix(base, #000000, 0.45)`), a tworzenie przyjmuje nadpisania po nazwie: `forest.Bark(base = #2a1a10)`. Materiały są kwalifikowane aliasem importu (`forest.Bark`, `m: forest.Bark`), a `palette`/`const` pliku materiałów są prywatne dla tego pliku.

## Przypadki użycia (kryteria akceptacji funkcji)

- [ ] **Drzewo:** materiał kory stosuje się do każdej gałęzi z osobna; każda gałąź ma własną grubość i kąt z rekurencji seeda, a materiał zapewnia tę samą korę: obrys w kolorze `edge` o szerokości `edge_width` px, zwężanie `taper` ku końcowi i nazwane odcienie autora na górę/dół gałęzi. Helper może pominąć kreskę na łączeniu z pniem (koniec startowy bez krawędzi, start lekko wewnątrz pnia), bo materiał tylko proponuje krawędź, a helper decyduje per bok/koniec.
- [ ] **Broń i ekwipunek:** stal, skóra rękojeści i mosiądz jako trzy materiały na jednej geometrii; rzadkość (common → legendary) jako wariant materiału, nie osobny asset.
- [ ] **Roślinność:** kora i liście jako dwa materiały; sezonowość przez podmianę zakresu liści przy tej samej geometrii.
- [ ] **Budynki i teren:** drewno, dach i kamień dzielące paletę tonów, aby sąsiednie kafle i budynki trzymały jeden art direction.
- [ ] **Postacie:** skóra, włosy i tkanina; baza skóry losowana raz, więc wszystkie części ciała mają ten sam odcień w danym seedzie.
- [ ] **Paczki assetów i ikony UI:** wspólna biblioteka materiałów dla całej paczki; re-skin paczki to edycja deklaracji materiałów, a typ/rzadkość ikony to podmiana materiału przy jednej geometrii.

## Podejście techniczne

- [x] **Decyzja wykonawcza (odnotowana przy implementacji, do przeglądu):** zamiast nowego typu wartości VM z nowymi opcode'ami, kompilator rozwija instancję materiału do jej pól skalarnych (`color`/`i32`/`f32`/`vec2`) przed generacją kodu. Semantyka języka z tego planu jest zachowana (osobny typ per definicja sprawdzany przez kompilator, drawy tylko przy tworzeniu, kolejność pól, niemutowalność), a format `.iprop`, VM, validator i wszystkie backendy działają bez zmian. Kontrakt zapisany w `spec/materials-0.8.md`. Powód: pełny typ VM wymagałby przebudowy deskryptorów typów, validatora stosu i conformance wszystkich backendów, bez zmiany zachowania widocznego dla autora `.ips`.
- [x] Odrzucone: sama biblioteka helperów bez wsparcia języka, parametry PBR/3D oraz tekstura/zużycie w v1.
- [ ] Poza zakresem tej implementacji (świadomie, zapisane w kontrakcie 0.8): zwracanie materiału z helpera, `list<Bark>`, `sampler`/`stepper` materiałów, `const material` losujący raz oraz przekazywanie materiału bezpośrednio do partu (part tworzy materiał w środku albo dostaje selektor liczbowy).

## Kroki budowy

- [x] 1. Audyt (bez kodu): zinwentaryzować dzisiejsze „materiały" w `examples/language/` i showcase (baza, odcienie, krawędź) i spisać wymagania oraz świadome nie-cele (brak PBR, brak tekstury/zużycia, materiał nie rysuje sam).
- [x] 2. Kontrakt 0.8: `spec/materials-0.8.md` (bez przepisywania zamrożonego `spec/iprop-v0.7.md`): definicje, tworzenie, drawy w kolejności pól, zakres i wyłączenia.
- [x] 3. Front-end (`language/ips/`): parser plików materiałów (wiele definicji, własne `palette`/`const`, zakaz definicji w pliku assetu), typy per definicja, semantyka pól specjalnych i nazwanych, diagnostyki.
- [x] 4. Kompilator (`compiler/` + `language/ips/Materials.cpp`): rozwijanie instancji do pól skalarnych, drawy w kolejności pól, odczyty bez drawów; testy w `tests/compiler/materials_test.cpp` zarejestrowane w `tests/CMakeLists.txt`.
- [x] 5. Runtime: bez zmian (rozwinięte pola to istniejące typy VM); istniejące testy runtime i golden przechodzą jako regresja.
- [x] 6. Narzędzia: `ipc` kompiluje, `ipval` waliduje, a `ipgen` generuje przykład drzewa (zweryfikowane ręcznie na seed 7).
- [x] 7. Dokumentacja i przykłady: rozdział `docs/language/materials.md` (wpis w `docs/language/book.json` i overview), przykład drzewa `examples/language/material-tree.ips` + `examples/language/materials/forest.ips` z wpisem w `cases.json` (helper ręcznie rysuje zwężane gałęzie z pól materiału).
- [x] 8. Edytor: `material` dodane do gramatyki VS Code w `editor/syntaxes/ips.tmLanguage.json`.
- [x] 9. Wydanie 0.8: wpis w `CHANGELOG.md` (Unreleased, cel 0.8). Bumpy wersji produktu/języka/ABI zostają na właściwy krok release'owy, nie na gałąź funkcji.

## Walidacja

- [x] Build: `cmake --build build` przechodzi (konfiguracja Release w `build/`).
- [x] Testy: `ctest --test-dir build` — 71/74 zaliczone, w tym nowe `tests/compiler/materials_test.cpp` (osobny plik, odrzucenie definicji w pliku assetu, brak `base`, nieznane pole, determinizm renderu). Pozostałe 3 porażki są środowiskowe i niezależne od zmiany: `sfml_render`/`sfml_conformance` (SFML bez displaya; binarka nie linkuje zmienianego kodu) oraz `godot_conformance` (zewnętrzny silnik Godot).
- [x] Determinizm: test materiałów porównuje dwukrotne wykonanie tego samego `.iprop` (seed 7) bajt w bajt; drawy powstają tylko przy tworzeniu instancji, w kolejności pól definicji.
- [x] Kontrola ręczna: `ipc` + `ipval` + `ipgen --seed 7` na przykładzie drzewa przechodzą; `ipsheet` renderuje siatkę seedów.

## Ryzyka i pytania otwarte

- [ ] Nowy typ VM dotyka formatu, validatora i wszystkich backendów; koszt jest znacząco większy niż lowering i wymaga osobnej akceptacji specyfikacji w kroku 2.
- [ ] `const` losujący raz przy pierwszym użyciu potrzebuje precyzyjnej reguły strumienia RNG w specyfikacji (czyj strumień, co przy partach), inaczej determinizm między kompozycjami będzie nieudowodniony.
- [ ] Pola nazwane o tych samych nazwach w różnych definicjach są osobnymi typami; pomyłka typu (`Bark` vs `Stone`) musi dawać czytelną diagnostykę kompilatora.
- [ ] Zwężanie i krawędź działają tylko dla kształtów rysowanych wzdłuż osi przez helper (np. zwężany `poly`); dla prymitywów bez osi `taper` nie ma zastosowania i przykład/docs muszą to mówić wprost.
- [ ] Wersje dla 0.8 (język, format, C API/ABI) ustala bramka specyfikacji; brak wstecznej kompatybilności zwalnia z adapterów, ale każda zmiana przełamująca musi być wymieniona w nocie migracyjnej wydania.

## Bramka akceptacji

- [ ] Akceptuję ustalenia zebrane powyżej (osobny plik, nowy typ VM, `Bark()` w `draw`, parametr `m: Bark`, pola specjalne i nazwane, helper rysuje ręcznie).
- [ ] Akceptuję przypadek drzewa jako główne kryterium akceptacji v1 (osobna grubość i kąt per gałąź, wspólna kora z materiału).
- [ ] Akceptuję koszt nowego typu VM: zmiana specyfikacji przed kodem, aktualizacja narzędzi i backendów, akceptowana rekompilacja `.iprop`.
- [ ] Akceptuję kolejność kroków i walidację powyżej; budowa zaczyna się dopiero po zaznaczeniu tych pól.
