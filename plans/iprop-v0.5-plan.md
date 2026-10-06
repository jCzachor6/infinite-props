# Infinite Props 0.5 — język, diagnostyka i podstawy C API

**Status:** wdrożenie w toku na `develop`; checkboxy oznaczają zadania zweryfikowane i ukończone. Pozostałe funkcje są planowane.
**Data:** 2026-10-05.
**Baza:** wydane `v0.4.0`, commit `66d34c2831585afb231022da9296528a912e962e` na `main`.
**Cel:** poprawne źródło i czytelna diagnostyka dla człowieka/LLM oraz C API, które opisuje asset, sprawdza parametry i ma rozszerzalny kontrakt.

Ten dokument i [plan 0.6](iprop-v0.6-plan.md) zastępują poprzedni, wspólny plan 0.5. Łączny zakres obejmuje wszystkie 56 pierwotnych zadań oraz wszystkie propozycje C API. Mapa na końcu rozlicza przeniesione i podzielone zadania. Plan rozwija [plan 0.4](iprop-v0.4-plan.md), istniejący [podręcznik](../docs/language/index.md), formatter, rejestr builtinów, `--describe-language` i testy dokumentacji.

## Podział pół na pół

Oba wydania zawierają język/narzędzia oraz C API. Podział wynika z zależności: 0.5 udostępnia diagnostykę, binder, parametry i metadane; 0.6 wykorzystuje je w nowej składni, kontekstach, ograniczeniach wykonania i integracjach. Nie czekamy do 0.6 z całym C API.

| Obszar | 0.5 | 0.6 |
| --- | --- | --- |
| Język i narzędzia LLM | Poprawki parsera/defaultów/palet, named args, JSON/check, komentarze w formatterze, kompaktowa referencja, pomiar bazowy | `let`/`fn`, analiza inicjalizacji, konstruktory geometrii, gramatyka i adapter, pełna pętla napraw i oceny |
| C API | Rozszerzalne ABI, wersje/capabilities, introspekcja, walidowane parametry, błędy, typowane odczyty, przenoszenie wyniku VM i podstawowe statystyki | Konteksty i współbieżność, limity/anulowanie, bufory hosta, batch, C API kompilatora, RAII i binding |
| Zgodność i wydanie | Rozdzielenie wersji, stary klient z nową biblioteką, migracja przykładów i SDK | Rozszerzenie macierzy o współbieżność, limity, bindingi, gramatykę i nowe paczki |
| Wstępny nakład względny | **110 punktów — 50%** | **110 punktów — 50%** |

Punkty są szacunkiem względnej złożoności całych etapów, uwzględniającym implementację, migrację i weryfikację; nie są godzinami ani pomiarem. Równa liczba checkboxów nie oznacza równej pracy. Po bazowych reproduktorach i pomiarach aktualizujemy estymaty obu planów razem, zachowując zależności oraz podobny nakład. Nie usuwamy zadań, aby wyrównać tabelę.

## Etapy 0.5 i zależności

| Etap | Zakres | Priorytet | Punkty | Zależności |
| --- | --- | --- | ---: | --- |
| 0 | Kontrakty, baza i reproduktory | P0 | 8 | v0.4.0 |
| A | Niekonsekwencje języka | P0 | 8 | 0 |
| B | Pełne sprawdzanie i diagnostyka JSON | P0 | 13 | 0, A |
| C | Wspólny binder i named args | P0 | 8 | A, B |
| F | Komentarze i bezpieczny formatter | P1 | 8 | A, C |
| G | Eksport możliwości i referencja LLM | P1 | 5 | B, C, F |
| V | Wersjonowanie i rozszerzalne C ABI | P0 | 8 | 0 |
| R | Introspekcja programu | P0 | 8 | V |
| P | Parametry i walidacja uniformów | P0 | 8 | R, X |
| X | Strukturalne błędy i nowe wejścia `_ex` | P0 | 8 | V, B |
| O | Obraz i typowane odczyty wyjść | P1 | 5 | R, X |
| M | Odbiór wyniku bez zbędnej kopii, statystyki | P1 | 5 | O, 0 |
| I | Korpus, podstawowa naprawa i pomiar 0.4 → 0.5 | P1 | 5 | G, B; korpus od 0 |
| J | Integracje, CI i paczki | P0 | 13 | wszystkie powyższe |
| **Razem** | | | **110** | |

Przykładowa kolejność commitów: `0 → V → A → B → X → R → P → C → F → O → M → G → I → J`. Etapy mają małe, kolejno działające kroki. Pierwszy kamień milowy: poprawki/check/binder oraz ABI/reflection/errors/parametry. Drugi: formatter, odczyty wyniku, referencja i pomiary. Trzeci: zgodność i wydanie. Prace 0.6 zaczynają się z utrwalonej bazy 0.5.

## Wersje i zgodność

| Kontrakt | Wydane 0.4 | Docelowe 0.5 | Docelowe 0.6 |
| --- | --- | --- | --- |
| Produkt | 0.4.0 | 0.5.0 | 0.6.0 |
| Język źródłowy | 0.4 | 0.5 | 0.6 |
| Format `.iprop` | 0.4; zgodność z 0.1–0.3 | 0.4 | 0.4 |
| C API runtime — zakres funkcji | 0.2 | 0.3, rozszerzenia addytywne | 0.4, rozszerzenia addytywne |
| Linia binarnej kompatybilności / SONAME | 0.4, obecnie wyliczane z produktu | 0.4, jawna osobna konfiguracja | 0.4, ta sama linia ABI |
| C API kompilatora | brak | brak osobnej biblioteki | 0.1, osobne `ipropcompiler` |

Numer SONAME 0.4 pozostaje historyczną nazwą obecnej linii binarnej, a nie numerem C API ani formatu. `backends/capi/CMakeLists.txt` dziś wyprowadza go z `PROJECT_VERSION`; V/J zastępują to niezależną konfiguracją. Stare binaria szukające biblioteki z SONAME 0.4 muszą nadal działać. Tak samo sprawdzamy install-name na macOS i eksporty DLL na Windows. Zmiana ABI poza tym kontraktem wymaga nowej decyzji i migracji; nie jest ukrytą częścią 0.5/0.6.

## Decyzje projektowe

1. **Oddzielamy produkt, język, format, C API i linię ABI.** Nową składnię obniżamy do istniejącej VM. Preflight wybiera rzeczywistą specyfikację formatu 0.4, bez fikcyjnych `spec/iprop-v0.5.md` i `spec/iprop-v0.6.md`.
2. **Zachowujemy zaakceptowane źródła i zachowanie 0.4.** Aliasy, pozycyjne wywołania, helpery bez `fn`, literały `{...}` i deklaracje przez pierwsze przypisanie pozostają. Reguły liczb, trapów, transformacji, RNG i defaultów nie zmieniają się.
3. **Styl kanoniczny rozwijamy w dwóch krokach.** 0.5: słowa ASCII, named args i średniki, przy istniejącej składni deklaracji/helperów. 0.6: dodatkowo `fn`, inicjalizowane `let` i nazwane konstruktory. Referencja 0.5 nie podpowiada składni dostępnej dopiero w 0.6; Unicode pozostaje w stringach i komentarzach.
4. **`let`/`fn` i analiza inicjalizacji wchodzą razem w 0.6.** `let` jest mutowalny, wymaga inicjalizatora, ma zasięg funkcyjny bez shadowingu. `let x: f32 = 1;` dopuszcza obecne poszerzenie, kolejne przypisania wymagają dokładnego typu. `const` i zasięgi blokowe pozostają poza tymi wydaniami.
5. **Nowe nazwy nie przejmują istniejących symboli.** W 0.6 `let`/`fn` są kontekstowe; stare `let = 1`, `let(...)`, helper `fn(...)`, helper `vec2` i alias części `shape` zachowują wiązania. Przypadki kolizji mają reproduktory i testy.
6. **`input i32` jest źródłowym zapisem istniejącego integer uniformu.** Default mieści się matematycznie w `[-2147483648, 2147483647]` przed zawinięciem. Binarnie pozostaje `u32` z tym samym wzorcem bitów, w `draw` — `i32`. C API introspekcji pokazuje typ binarny `u32`; informacja o źródłowym `i32` pochodzi wyłącznie z kompilatora. Dawne defaulty `u32`, w tym `0xffffffff`, zachowują znaczenie; ujemny default `u32` sugeruje `i32`.
7. **Defaulty/palety mają mały, jawny zakres stałych.** Literały, nawiasy i pojedynczy unary minus dla liczb; bez dowolnych wyrażeń, wywołań i RNG. Konwersje i negacja używają deterministycznych reguł `i32`/`f32`, `-0.0` zachowuje znak. Kontrola defaultu `i32` nie zmienia zwykłych literałów integer.
8. **Konstruktory geometrii w 0.6 zachowują istniejące możliwości.** `vec2` jest wartością lokalnych/parametrów/wyników helperów. `shape.circle` i `shape.rect` opisują obecne złożenia outputów; nie dodają rekordów ani typów VM.
9. **Binder oddziela wiązanie od wykonania.** Helper: kolejność parametrów. Część: override'y uniformów według deklaracji → `seed` → `v` → `a` → `at`. `poly`: współrzędne przed kolorem. Dynamiczne wyrażenie wykonuje się raz, formatter nie zmienia efektów RNG.
10. **Stare C API pozostaje kompatybilne.** Nie zmieniamy layoutów, enumów, sygnatur, kolejności slotów ani czasu życia danych API 0.2. Jego ignorowanie nieznanych uniformów, pierwszy wpis przy duplikacie i trap przy niezgodnym typie pozostają w starym wejściu. Nowe API ma jawną, rygorystyczną walidację.
11. **Nowe deskryptory mają `struct_size` i inicjalizator.** Ustalamy defaulty, zakres odczytu/zapisu i obsługę nieznanych flag. `struct_size` nie rozwiązuje sam rozmiaru elementów tablicy: nowe kolekcje używają accessorów lub jawnego stride. Nie dopisujemy pola do starego `iprop_output`.
12. **Uchwyty rozdzielają odpowiedzialności.** `program`: program i metadane; `parameters`: walidowane wartości; `context` od 0.6: workspace wykonania; `result`: niezależny wynik. Parametry związane z programem utrzymują potrzebne dane przy życiu. Wynik pozostaje poprawny po zwolnieniu programu i po kolejnej generacji.
13. **Współbieżność ma kontrakt dla każdego uchwytu.** W 0.5 obowiązuje dotychczasowa reguła niezależnych uchwytów; nie deklarujemy nowego wsparcia współdzielonego programu. 0.6 dopuszcza je po audycie niezmienności i weryfikacji. Parametrów nie mutujemy podczas ich odczytu przez generację.
14. **Nazwy nowych funkcji w planie są robocze.** Zamrażamy je po review nagłówka i prototypu klienta C99; wymagania dotyczące semantyki, zgodności i ownership są kryteriami akceptacji.

## 0. Kontrakt i punkt odniesienia

- [x] **05-0.1** Utrwalić wydane v0.4.0 i SHA z nagłówka, źródła, diagnostyki, obrazy, outputy, trapy, stan RNG, koszty/rozmiary optimized/reference oraz stary nagłówek i binarnego klienta C API 0.2.
  Dowód: `benchmarks/baselines/v0.4.0/manifest.json`, zamrożony klient `backends/capi/test/legacy/v0.2`, 87 źródeł, 690 par renderów, 60 par trapów i 5 pełnych stanów RNG; weryfikacja zachowanych artefaktów oraz 11/11 testów bazowych zakończona powodzeniem. Platforma klienta: macOS arm64.

- [x] **05-0.2** Zapisać powyższe decyzje w rozdziałach źródłowego podręcznika i krótkim dokumencie migracji 0.4 → 0.5. Rozdzielić kontrakt składni, reguły typowania i semantykę wykonania.
  Dowód: rozdział `docs/language/migration-v0.5.md` oddziela składnię, typowanie i wykonanie, opisuje wersje, ABI, ownership oraz granicę 0.6. Podręcznik i plany są pakowane offline; walidacja generowanej książki i 9 testów dokumentacji przeszły.

- [x] **05-0.3** Zdefiniować stały zestaw zadań LLM i procedurę pomiaru przed modyfikacją kompilatora. Zachować wyniki bazowe oraz konfigurację umożliwiającą porównania w 05-I i 06-I.
  Dowód: `benchmarks/llm/` — stałe 24 zadania i protokół, zamrożony handbook/export, dokładny prompt i rzeczywiste odpowiedzi modelu. Baza 0.4: 24/24 kompilacji i publicznych kryteriów, 194 rendery; pełne surowe dowody z hashami, 7 testów runnera. Jedna próba na zadanie, zero napraw; nieznane metryki oraz ludzka ocena marble jawnie niezmierzone/pending.

- [x] **05-0.4** Przygotować małe reproduktory każdej niekonsekwencji z A i kolizji named args oraz błędów C API z V/R/P/X; kolizje let/fn/geometrii realizuje 06-0.2. Odnotować rzeczywisty wynik obecnego kompilatora, a nie samo powodzenie parsera.

Dowód 05-0.4: `benchmarks/baselines/v0.4.0/reproducers.json` zachowuje 89 minimalnych źródeł, 178 rzeczywistych kompilacji, 40 obserwacji C API i 45 kontrolowanych prób OOM. Wykryto dwie ucieczki wyjątków z catch oraz akceptowanie niepoprawnego enumu; przyszłe API jawnie oznaczone jako niedostępne w 0.4.

**Akceptacja:** każda proponowana konstrukcja ma określone znaczenie, przykład poprawny i przykład błędny. Baza 0.4 ma identyfikator commita; wyniki pomiarów mają opis warunków. Brak pomiaru LLM jest oznaczony jako brak danych.

## A. Usunięcie niekonsekwencji języka — P0

- [x] **05-A.1** Ujednolicić parser porównań: obie strony `<`, `<=`, `>` i `>=` korzystają z poziomu `shift`. Sprawdzić priorytety, łańcuchy porównań i istniejące nawiasy. `value < 1 << bits` ma być równoważne `value < (1 << bits)`.
  Dowód: cztery operatory porównania używają shift po obu stronach. Testy AST, priorytetów, łańcuchów, optimized/reference i RNG: 503 asercje / 9 przypadków; kontrola negatywna starego parsera odrzuca nowe przypadki. Podręcznik/grammar i walidacja przykładów przeszły.

- [x] **05-A.2** Przyjmować signed defaulty, np. `input f32 offset = -0.5;`, i signed elementy palet. Walidować stałą przed zapisem; assembler i defaulty części korzystają z już rozwiązanej wartości, zamiast zakładać w AST bezpośredni `Literal`.
  Dowód: `DeclarationConstants` rozwiązuje literały/nawiasy/jeden minus; Planner cache i oba zapisy root/part używają tej samej wartości. Zachowane signed zero/U32; naprawiony integer-spelled f32 default części. 441 testów / 338950 asercji, 31 przykładów i 700 optimized/reference par obraz/output przeszły; historyczne odrzucenie negative-default zachowane w bazie.

- [x] **05-A.3** Wprowadzić wspólny typ palety liczbowej: `[0, 0.5, 1]` staje się `f32`, `[0, 1, 2]` pozostaje `i32`. Mieszanie koloru z liczbą nadal jest błędem; legacy booleany zachowują integerowe znaczenie. `ramp` nadal odrzuca czysto integerową paletę.
  Dowód: wspólny resolver poszerza wyłącznie palety liczbowe do f32, niezależnie od pozycji elementu; int/bool pozostają i32, mieszanie koloru oraz integer ramp są odrzucane. Smoke i dokumentacja przeszły; testy porównują bajty programu, RGBA, raw float bits, negative zero i stan RNG względem jawnych f32, z optymalizacją i bez.
- [x] **05-A.4** Usunąć rozjazd parsera, typowania, Planera i assemblera dla `input i32`. Obsłużyć signed defaulty części, nadpisania uniformów oraz kodowanie i odczyt wzorców bitów zgodnie z decyzją 6.
  Dowód: input i32 przechodzi cały compiler, resolver sprawdza matematyczny zakres przed wrapping; metadata root/parts nadal koduje U32. Smoke, dokumentacja i optimizer acceptance przeszły; testy granic, odrzuceń poza zakresem, byte parity z U32, defaultów części, typed override i hostowych bit patterns (w tym INT_MIN i -1). Przykład signed-input jest wykonywalny; historyczny błąd 0.4 pozostaje w baseline.
- [x] **05-A.5** Akceptować opcjonalny średnik po imporcie, `size`, `origin`, `variants`, `archetypes`, `input`, `output` i `palette`. Zachować postacie bez średnika; newline nadal jest białym znakiem, bez automatycznego kończenia instrukcji.
  Dowód: parser przyjmuje jeden opcjonalny średnik po wszystkich ośmiu deklaracjach; newline pozostaje whitespace, formy legacy działają. Smoke i dokumentacja przeszły; testy malformed boundaries oraz zgodności AST, bytecode, RGBA, raw output bits i RNG dla importowanych programów w obu trybach optymalizacji.

**Miejsca zmian:** [parser](../language/ips/Parser.cpp), [wyrażenia parsera](../language/ips/ParserExpr.cpp), [semantyka](../language/ips/Semantics.cpp), [palety](../language/ips/Palette.cpp), [Planner](../compiler/Plan.cpp), [assembler](../compiler/Assemble.cpp), [resolver wywołań](../compiler/CallResolver.cpp).

**Akceptacja:** nowe pozytywne przypadki obejmują granice `i32`, odrzucenie overflow, `-0.0`, palety z minusami i poszerzeniem oraz root/part defaults. Wcześniej poprawne źródła zachowują obraz, outputy i efekty. Każda naprawa jest osobnym commitem z odpowiadającym jej przykładem i regresją.

## B. Pełne sprawdzanie i diagnostyka JSON — P0

- [x] **05-B.1** Dodać `ipc entry.ips --check --diagnostics=json`. Przejść przez importy, parse, typing, Planner, lowering, składanie w pamięci i walidację pliku. Sam `checkAsset` nie wystarcza. Tryb nie zapisuje `.iprop`, formatowanego źródła ani innych artefaktów; sprzeczne opcje zapisu są błędem CLI.
  Dowód: compileSource współdzieli pełny pipeline import→parse→typing→Planner→lowering→assemble→strip/compress→validate między compile/check, z owning snapshots i jednym odczytem importu. Real CLI no-write/conflict/import/Planner oraz smoke, editor, dokumentacja i optimizer acceptance przeszły (6/6 CTest). JSON to jawny scaffold schema 0 do kolejnych B.2–B.4.
- [x] **05-B.2** Wprowadzić wspólną strukturę diagnostyki i dwa renderery: tekstowy oraz JSON. Kody błędów są stabilne, niezależne od treści komunikatu; renderer tekstowy zachowuje czytelne obecne komunikaty.
  Dowód: wspólne ips::Diagnostic, typed code/stage/severity oraz tekstowy i JSON renderer. Kody przypisane przez przyczyny i granice faz, bez parsowania treści; registry/kontrakt w docs/compiler-diagnostics.md. 5/5 CTest; testy różnych komunikatów z tym samym kodem, signed range, parse/lex/typing/Planner, I/O vs unexpected loader i tekstowej warning severity.
- [x] **05-B.3** Ustalić protokół: `schema_version`, wersja produktu/języka, status i lista diagnostyk; każda zawiera `code`, `severity`, `message`, plik i zakres, opcjonalnie typ oczekiwany/otrzymany, sugestie, powiązane lokalizacje i fix-ity. Pozycje: zero-based offset bajtowy, one-based linia i kolumna w Unicode codepointach, koniec zakresu wyłączny. Lokalizacja może być pusta dla błędu CLI/I/O.
  Dowód: schema 1 oddziela produkt i język; end-exclusive token ranges z zero-based UTF-8 byte offsets i one-based codepoint positions, nullable I/O/CLI, expected/actual types i jawne optional fields. SHA-256 exact snapshot identity (4 wektory, niezależny hashlib). 5/5 CTest; emoji/CRLF/tab/EOF oraz 14 real CLI reports przeszły walidację Draft 2020-12.
- [x] **05-B.4** JSON jest pojedynczym dokumentem na stdout, także przy błędzie; bez kolorów i logów wymieszanych z danymi. Exit codes: `0` — brak błędów, `1` — błędny program/import, `2` — błędne wywołanie CLI, `3` — błąd wewnętrzny. Ostrzeżenia same nie powodują kodu `1`. Fix-it zawiera zakres i tekst zamiany oraz identyfikator treści źródła, aby klient nie stosował go do zmienionego pliku.
  Dowód: typed status/exit mapping 0/1/2/3, warning-only status 0; JSON buforowany przed publikacją, bez logów/kolorów, literalny fallback bez alokacji obiektów. Source-bound fix-it U32→I32 naprawia tylko poprawny signed zakres. Smoke/check/docs/editor i fault testy przeszły; wszystkie 1605 alokacji ścieżki sukcesu sprawdzone one-shot/persistent (3218 uruchomień, poprawny pojedynczy JSON). Schema zweryfikowana również dla fix-it, I/O, CLI i exit 3 fallback.
- [x] **05-B.5** Zebrać kilka niezależnych błędów z limitowaną liczbą komunikatów i bez kaskad po nieznanym typie. Synchronizować parser na bezpiecznych granicach; nie przekazywać uszkodzonego AST do generowania. Dodać diagnostyki również dla ograniczeń wykrywanych dopiero przez Planner i walidator.
  Dowód: parseRecovering/parseDiagnostics z clamp 1–20, semicolon/token/brace sync bez newline i valid-only AST; semantic recovery z unknown-type suppression oraz przywracaniem scope/loop state. Wspólny limit na import graph, każdy import snapshot/hash, shared alias check raz; Planner i rzeczywisty validator nadal w check. Smoke 473 przypadki/340368 asercji, 5/5 targeted CTest oraz 3220 fault subprocesses (1606 alokacji) przeszły; invalid compile/check nie nadpisuje artefaktów.
- [x] **05-B.6** Przestawić edytor na JSON i zakresy; zachować fallback dla kompilatora 0.4. Konwersję codepointów na UTF-16 wykonywać tylko na granicy VS Code. Sugestie i fix-ity udostępnić jako działania naprawcze.
  Dowód: editor schema1 JSON transport/ranges/severity/code/related locations, UTF-16 tylko na granicy VSCode. SHA i document-version guarded quick fixes/suggestions; dirty/stale/cancelled results odrzucane. Legacy fallback wyłącznie unsupported option, unique temp + cleanup. 27/27 Node testów z real current ipc i frozen0.4 przeszły; obejmują importy, CRLF/tab/emoji/BOM/mixed EOL/invalidUTF8, dwa real błędy B.5, multi-edit i Problems quick-fix własnego importu. VSCode boundary sprawdzony przez harness, nie live aplikację.

**Miejsca zmian:** [SourceError](../language/ips/SourceError.hpp), [CLI](../tools/ipc.cpp), wspólny pipeline kompilacji, [diagnostyka edytora](../editor/diagnostics.js).

**Akceptacja:** sukces i wszystkie klasy błędów dają poprawny JSON oraz właściwy exit code. Testy obejmują importowany plik, brak importu, CRLF, taby, emoji, kilka błędów, ograniczenie liczby błędów, bezpieczny fix-it i brak zapisu plików. `--check` i zwykła kompilacja zgodnie przyjmują/odrzucają ten sam program i zestaw flag.

## C. Jeden binder i nazwane argumenty wszędzie — P0

- [x] **05-C.1** Rozszerzyć istniejący `bindArguments` na builtin, helper oraz część; pozostawić tę samą granicę dla konstruktorów dodawanych w 06-E. Wspólnie sprawdzać nieznane nazwy, duplikaty, brak wymaganych parametrów i mieszanie argumentów pozycyjnych z nazwanymi. Parametry opcjonalne i variadic pozostają opisane w sygnaturze.
  Dowód: wspólny bindArguments dla builtin/helper/part; sygnatura required/optional/named-only, brak/unknown/duplicate/mixed args walidowane w jednym miejscu. Typowanie i CallResolver części korzystają z tej samej sygnatury zamiast osobnych map. Variadic nadal rozwijane z builtin registry; named helper/control activation osobno C.2/C.3. 6/6 CTest (smoke, real CLI, docs/editor, optimizer) przeszły; istniejące byte/RGBA/RNG i defaulty zachowane.
- [x] **05-C.2** Obsłużyć helpery, np. `paint(tint=#9dc, x=8)`, z wiązaniem po nazwach z deklaracji. Wykonywać argumenty w kolejności sygnatury; stare wywołania pozycyjne pozostają identyczne. Zachować reguły poszerzania argumentów.
  Dowód: helper names z deklaracji przez wspólny binder; named/mixed args wykonywane raz w declaration order, widening i32→f32 zachowane. Root i part helper namespaces, byte parity optimized/reference i RNG effect trace dla 3 seedów; duplicate/missing/unknown/wrong-type i duplicate parameter declarations odrzucone. Smoke 478 przypadków/340466 asercji, real CLI check/compile oraz docs przeszły. Dokumentacja opisuje nową regułę unikalnych parametrów.
- [x] **05-C.3** Umożliwić `branch.draw(seed=seed, v=v, a=a, at={8, 4})`. Użyć istniejących zastrzeżonych nazw `seed`, `v`, `a`; nie rezerwować dodatkowo `variant`/`archetype`, które mogą być nazwami uniformów. Trzy dotychczasowe argumenty pozycyjne pozostają obsługiwane. Zapis `at=vec2(...)` dołącza w [06-E](iprop-v0.6-plan.md#e-nazwane-konstruktory-geometrii).
  Dowód: named/mixed seed/v/a używają wspólnego bindera; at/transform/uniforms pozostają named-only, variant/archetype nie są aliasami kontroli. 489 smoke testów / 354160 asercji; CLI/check/editor/optimizer przeszły. Testy porównują bytecode, RGBA, raw output bits/defaulty, jednokrotną ewaluację i odtworzenie RNG; dokumentacja 31 przykładów przeszła na snapshotcie śledzonych źródeł (lokalne kopie „ 2” poza zakresem).
- [x] **05-C.4** Przenieść rezultat wiązania do planu wywołania używanego przez typowanie, emitter, formatter i eksport sygnatur. Jawnie zapisać kolejność wykonania oraz defaulty; nie powtarzać mapowania nazw w każdej warstwie.
  Dowód: frontend CallSignature/BoundCallPlan zapisuje sloty, evaluationOrder i deferred defaulty; Typer cache scope-aware, lowering nie binduje ponownie, emitter części konsumuje jedną uporządkowaną listę. Formatter/eksport używają tych samych sygnatur, unknown imports zachowują zapis. Review, full SFML/Godot build i 22/22 CTest przeszły (493 smoke / 355631 asercji); testy zmiany nazw po bindingu, lazy scopes, wspólnego AST aliasów, default -0.0, poly/part/RNG i byte parity po formatowaniu.
- [x] **05-C.5** Sprawdzić efekty na przykładach z `unit()`/`range()`: permutacje named args, mieszane postacie, pominięte override'y, defaulty, `poly`, `pick`/`choose` i `ramp`. Nie zmienić przywracania streamu rodzica po części.
  Dowód: nowy test obejmuje 48 kolejności argumentów range/pick/choose/ramp/poly, każdą named i mixed, optymalizację on/off i cztery seedy. Wykonywalne bytes po stripDebug, RGBA, raw f32 trace wartości/efektów i końcowe probe RNG są zgodne z niezależnym referencyjnym programem. Wraz z testami części sprawdza pominięte override/defaulty (-0), deklaracyjną kolejność i stream rodzica po 12 losowaniach dziecka. Focused 27372 assertions/4 cases oraz pełny smoke przeszły; implementacja nie wymagała zmiany.

**Miejsca zmian:** [BuiltinArguments](../language/ips/BuiltinArguments.hpp), [TypingCalls](../language/ips/TypingCalls.cpp), [CallResolver](../compiler/CallResolver.hpp), emitter wywołań i formatter.

**Akceptacja:** pozycyjne i równoważne nazwane wywołania dają identyczny wynik i końcowy stan RNG. Wszystkie rodzaje wywołań podają spójne kody diagnostyk dla błędnego wiązania. Publiczne nazwy parametrów po wydaniu stają się częścią kontraktu języka.

## F. Jeden styl i bezpieczny formatter

- [x] **05-F.1** Zachować komentarze `//` i `/* ... */` jako trivia powiązane z tokenami/zakresami. Określić przypięcie komentarzy przed deklaracją, po instrukcji, wewnątrz wyrażeń i między named args. Treść i przypięcie komentarzy pozostają zachowane; w wywołaniu zawierającym komentarze utrzymać zapisaną kolejność argumentów, aby nie przestawić komentarzy ani ich znaczenia.
  Dowód: owning leading/trailing token trivia i zakresy AST zachowują dokładną treść, token owner/kind/text, stronę komentarza i głębokość bloku. Komentowane calls zachowują written order, pozostałe nadal używają C.4 CallPlan. Przypadki wielowierszowe, opcjonalne nawiasy/średniki, puste else, powtórzona metadata i granice tokenów są idempotentne i mają te same stripped bytes. Naprawiony abort OOM: explicit close, destructor tylko bezalokacyjne cleanup; wszystkie 786 root fault points single/persistent oraz ASan+UBSan probe przeszły. Strict SFML/Godot build, 24/24 CTest, smoke 502 cases/371410 assertions; niezależny review i czuły przed/po reproducer przeszły.
- [x] **05-F.2** Normalizować słowa zamiast emoji, odstępy, grupowanie wyrażeń i średniki; helpery zachowują istniejącą składnię; drukowanie z `fn` dołącza w 06-F. Nazwane wywołania korzystają z tego samego wiązania i kolejności wykonania co kompilator, z uwzględnieniem wyjątków `poly` i części.
  Dowód: canonical printer używa wspólnego C.4 call plan i wyjątku F.1 dla komentowanych wywołań; dokumentacja wyjaśnia kolejność helper/poly/part oraz grupowanie/średniki. Wszystkie 48 permutacji named/mixed z C.5 w obu trybach optymalizacji są idempotentne i zachowują stripped bytes po formatterze. Test słów/emoji, starej składni helperów, signed i32/U32, -0.0 i najmniejszego normalnego f32 zachowuje dokładne raw bits. Focused 14807 assertions/2 cases oraz pełny smoke i language_documentation przeszły.
- [ ] **05-F.3** Nie dopisywać automatycznie `let` do legacy przypisań ani nie przenosić deklaracji między gałęziami. Formatter zachowuje znaczenie; zalecenia migracji przedstawia osobno lint. Obsługa jawnego `let` i jego typu dołącza w 06-F razem z deklaracjami.
- [ ] **05-F.4** Wprowadzić formatowanie bez binarnego artefaktu, np. `ipc entry.ips --format canonical.ips --format-only`. Zachować dotychczasowe `--format` z kompilacją. Błąd nie nadpisuje docelowego źródła; importy zachowują znaczenie względnych ścieżek.
- [ ] **05-F.5** Używać formattera w edytorze, galerii i podstawowej pętli 05-I; pełny pipeline napraw rozwija 06-I. Testować komentarze także przy zmianie kolejności named args i kanonizacji literałów signed/defaultów.

**Akceptacja:** `format(format(source)) == format(source)`, komentarze zachowane, a program przed/po daje ten sam wykonywalny kod po usunięciu debug source maps. Dotyczy również legacy locals, kolizji nazw, floatów i wyrażeń z RNG.

## G. Jedna dokumentacja i kompaktowy kontrakt dla modelu

- [ ] **05-G.1** Rozszerzyć `ipc --describe-language` o możliwości i ograniczenia: typy inputów, reguły poszerzania, obecne reguły inicjalizacji legacy locals, output-only geometry, named calls, widoczność uniformów, zakaz rekurencji i zagnieżdżonych importów, kolejność efektów oraz niezależne wersje produktu, języka, formatu, C API i linię ABI. Eksport deklaruje tylko faktycznie dostępne możliwości; let/fn/konstruktory dołączają w 06-G.
- [ ] **05-G.2** Opisywać efekty wywołań, m.in. RNG, rysowanie, zapis outputu i możliwość trapu. Dane, których używa typowanie/Planner/binder, eksportować z ich wspólnych rejestrów; reguły opisowe powiązać z wykonywalnymi przypadkami. Nie budować drugiej ręcznej listy sygnatur dla LLM.
- [ ] **05-G.3** Generować `LLM_REFERENCE.md` z wybranych sekcji podręcznika, eksportu kompilatora i zarejestrowanych przykładów. Zawrzeć minimalny asset, helper, część, paletę, output, reguły typów i ograniczenia, a także pary „błędny kod → diagnostyka → poprawka”. Ustalić budżet objętości i możliwość wyboru potrzebnych symboli/sekcji.
- [ ] **05-G.4** Uaktualnić podręcznik, galerię, opisy diagnostyk i dokument migracji. Obecne przykłady `negative-default` i `signed-input` stają się poprawnymi przykładami 0.5; ich oczekiwane błędy pozostają zapisane w historycznej bazie 0.4. Nie zostawiać starych oczekiwań w bieżącym `cases.json`.
- [ ] **05-G.5** Wersjonować i sprawdzać schematy eksportu oraz wszystkich konsumentów: generator dokumentacji, edytor, stronę i paczki. Każde nowe pole ma opis; zmiana niekompatybilna wymaga jawnej migracji schematu.

**Miejsca zmian:** [BuiltinSpec](../language/ips/BuiltinSpec.hpp), [CLI](../tools/ipc.cpp), [generator dokumentacji](../scripts/language_docs.py), [źródła podręcznika](../docs/language/book.json), [manifest przykładów](../examples/language/cases.json), generator strony.

**Akceptacja:** dokument kompaktowy jest generowany, zawiera wersję i commit oraz odwołania do pełnego podręcznika. Każdy przykład jest wykonywalny lub ma sprawdzane oczekiwane odrzucenie/trap. Zmiana sygnatury albo ograniczenia bez aktualizacji opisu/przykładu powoduje błąd kontroli dokumentacji.

## V. Rozszerzalne ABI i wersje biblioteki — P0

- [x] **05-V.1** Zamrozić API 0.2: sześć symboli, layouty i stride struktur, wartości enumów, konwencję wywołania oraz ownership. Zdefiniować addytywne API 0.3 i reguły dalszego rozwoju API 0.4. Rozmiary typów i wspierane ABI/platformy mają być jawne; nie zmieniać starych enumów na inne typy.
  Dowód: `docs/c-api-abi.md` zamraża sześć symboli, pełne offsety/stride, enumy i ownership oraz określa zasady addytywnego API. Nagłówek po usunięciu komentarzy identyczny z v0.4.0; C99 layout probe na macOS arm64 i zachowany klient 0.2 przeszły.

- [x] **05-V.2** Dodać odczyt wersji załadowanej biblioteki, wersji API, linii ABI, obsługiwanych formatów i capabilities. Makra nagłówka opisują kompilację klienta; query opisuje bibliotekę, z którą faktycznie pracuje. Możliwość nieobecna w 0.5, np. anulowanie, nie jest reklamowana.
  Dowód: `iprop_library_query` zwraca oddzielne wersje produktu/API/ABI/formatu oraz wyłącznie dostępne capabilities. C99 shared/static, wszystkie długości prefiksu i canary tail, stare layouty i conformance przeszły; oryginalny klient 0.2 uruchomiony z nową biblioteką (potwierdzony loader).

- [x] **05-V.3** Wprowadzić nowe deskryptory `_ex` z `struct_size`, określonymi defaultami, inicjalizatorem i obsługą flag. Czytać/zapisywać tylko dostępny prefiks; nieznana żądana funkcja nie może zostać cicho zignorowana. Kolekcje mają accessory albo jawny rozmiar/stride elementu. Stare struktury pozostają bez zmian.
  Dowód: oddzielne sized deskryptory, size-taking initializers oraz wspólny reader/writeField z defaultami i walidacją flag. C99 shared/static i wszystkie prefiksy/tail canaries przeszły; dokładnie alokowane krótkie prefiksy sprawdzone ASan+UBSan. Kolekcje nowych widoków mają kontrakt pojedynczych accessorów.

- [x] **05-V.4** Wydzielić konfigurację ABI/SONAME z `PROJECT_VERSION`, zachowując linię 0.4 i stare nazwy potrzebne loaderowi. Uaktualnić CMake, dokumentację kompatybilności, eksporty DLL i install-name; sprawdzić shared/static oraz przeniesiony SDK.
  Dowód: niezależna konfiguracja ABI 0.4 steruje SOVERSION, install-name, runtime query i metadanymi paczki. 15/15 CTest; 15 przypadków rzeczywistego find_package; przeniesiony SDK i konsumenci C99 shared/static przeszli. Oryginalne binarium klienta 0.2 załadowało bibliotekę z przeniesionego SDK (dyld); osobny build produktu 0.5 zachował install-name 0.4 i przeszedł shared/static oraz stary klient.
- [x] **05-V.5** Ustalić kontrakt nowych uchwytów i deskryptorów: UTF-8 z długościami w bajtach, typy o ustalonych szerokościach, nullability, kto zwalnia pamięć, kiedy wskaźnik traci ważność oraz zachowanie po błędzie. Walidować rozmiar deskryptora przed dostępem do pól.
  Dowód: docs/c-api-contracts.md definiuje nullability, fixed-width typy, właścicieli widoków, invalidation, retained program data i transaction rules. Nagłówek dodaje frozen leaf UTF-8 view z byte count; wspólny no-allocation reader odrzuca invalid/truncated UTF-8 i NULL+length, zachowuje embedded NUL. Prefix/tail i UTF-8 testy przeszły shared/static C99 oraz ASan+UBSan.

**Miejsca zmian:** [nagłówek](../backends/capi/iprop.h), [adapter](../backends/capi/iprop.cpp), [CMake C API](../backends/capi/CMakeLists.txt), [polityka ABI](../backends/capi/README.md), manifesty SDK/release.

**Akceptacja:** klient skompilowany ze starym nagłówkiem i stare binarium działają z nową biblioteką; C99 wystarcza do użycia API. Testy krótszych/większych deskryptorów, nieznanych flag i stride nie powodują odczytów poza pamięcią. Wersje produktu, API i formatu nie są zamienne.

## R. Introspekcja programu — P0

- [ ] **05-R.1** Udostępnić count/get/find wejść root: nazwa, binarny typ i dokładny default `color`/`f32`/`u32`. Indeksy obowiązują dla konkretnego załadowanego programu; nie są trwałymi identyfikatorami między kompilacjami.
- [ ] **05-R.2** Udostępnić deklaracje wyjść przed generacją: owner, nazwa, kind, singular/list i kolejność globalnych slotów. Powtarzające się nazwy różnych ownerów są poprawne; brak wartości w nieustawionym slocie nie oznacza braku deklaracji.
- [ ] **05-R.3** Udostępnić tabelę partów: owner/indeks, rozmiar, origin, sygnatury wejść i wyjść. Nazwa jest opcjonalna, jeśli rzeczywiście zachowana w pliku; nie odtwarzać nieobecnych aliasów importu ani ścieżek źródłowych.
- [ ] **05-R.4** Rozszerzyć odczyt metadanych przez nowy accessor o istniejącą emisję (`has_emission`, kolor, offset, radius) i informacje o formacie. Nie dopisywać pól do `iprop_meta`. Nie wymyślać zakresów min/max lub źródłowego typu i32, których format nie przechowuje.
- [ ] **05-R.5** Opisać czas życia widoków metadanych: należą do programu. Dodać przykład C99 budujący panel parametrów ze schematu i przenieść korzystających z tego odczytu konsumentów SDK do jednego kontraktu.

**Akceptacja:** refleksja bez generacji pokrywa root i wszystkie party, dokładne defaulty, emisję oraz identyczne nazwy pod różnymi ownerami. Działa dla starszych formatów, strip/debug i compressed/plain; brak debug nie usuwa wymaganych deklaracji.

## P. Walidowane parametry i rygorystyczne uniformy — P0

- [ ] **05-P.1** Dodać opaque `iprop_parameters` związane z programem; inicjalizacja z defaultów, reset/clone oraz get/set po nazwie albo wcześniej rozwiązanym ID. Dotyczy wejść root; introspekcja wejść partów nie dodaje nowego mechanizmu ich nadpisywania przez hosta. Sprawdzać zgodność parametrów z programem przed wykonaniem. Uchwyt utrzymuje potrzebne metadane i dane programu przy życiu.
- [ ] **05-P.2** Dodać typowane settery `f32`, `u32`, `color`; helper dla signed i32 przekazuje zdefiniowany wzorzec bitów do U32, bez nowego typu binarnego. Wywołujący nie wypełnia ręcznie pól nieaktywnego typu w starym `iprop_uniform`.
- [ ] **05-P.3** Nowe wejście rygorystycznie odrzuca nieznaną nazwę/ID, duplikaty w przekazanej liście override'ów, niezgodny typ, błędny wariant/archetyp i niewłaściwe deskryptory jako błędy argumentów przed VM. Jawne ponowne wywołanie settera aktualizuje wartość; jest czym innym niż duplikat w jednej liście.
- [ ] **05-P.4** Zmiana parametrów jest transakcyjna: nieudany setter/bind nie zostawia połowy override'ów. Podczas generacji parametry są tylko odczytywane; snapshot/clone pozwala przygotować osobne konfiguracje. Nie obiecywać współdzielenia mutowalnego buildera między wątkami.
- [ ] **05-P.5** Dodać `generate_ex` z walidowanymi parametrami i adapterem błędów 05-X. Zachować semantykę starego `iprop_generate` dla uniformów. Rozwiązać nazwę do ID raz przy przygotowaniu parametrów, a następnie przekazywać wartości do wspólnego modelu wejść VM bez ponownego mapowania nazw w każdej warstwie. Jeden walidator obsługuje nowe C wejścia i przyszły context.

**Akceptacja:** literówki, typy, duplikaty i parametry innego programu mają stabilne kody. Granice integerów i f32/defaulty zachowują bity. Stare wejście nadal ignoruje nieznane uniformy, wybiera pierwszy duplikat i zgłasza dawny trap dla niezgodnego typu.

## X. Strukturalne błędy i nowe wejścia — P0

- [x] **05-X.1** Ustalić nowy deskryptor błędu: status, stabilny kod przyczyny, etap load/validate/arguments/execute, opcjonalny offset, function/PC i linia debug. Offset po dekompresji opisuje rozwinięty plik; brak lokalizacji jest jawny. Nie gwarantować nazwy pliku/kolumny, jeśli binary nie ma tych danych.
  Dowód: sized iprop_error_ex + initializer; wspólny typed runtime reason/stage/location i bounded writer, bez message parsing. Container/expanded offset spaces, jawne presence bits, function/PC sprzed dispatch i optional bounded/noalloc debug lookup; legacy offset/status/text zachowane. Pełny strict build (SFML/Godot), 19/19 CTest, C99 exact-prefix ASan/UBSan oraz oryginalny frozen klient API0.2 z real root shared library przeszły. Brak ogłoszonych load_ex/query/generate_ex lub error capability na tym etapie.
- [x] **05-X.2** Tekst trafia do bufora klienta z NUL, długością wymaganą i informacją o obcięciu. Ujednolicić inicjalizację błędu przy sukcesie oraz wiadomości dla E_ARGUMENT/OOM; podstawowa ścieżka błędu nie wymaga nowej alokacji. Błędy pozostają lokalne dla wywołania, bez globalnego last_error.
  Dowód: caller-owned message buffer, NUL, pełna długość bez NUL, truncation i success reset przez noalloc ErrorSink; legacy tekst/status bez zmiany. Usunięto alokację string w catch oraz UB nakładających się buforów. Full build SFML/Godot, 22/22 CTest, root C API/sink ASan/UBSan (exact prefixes, alias, UTF-8 2/3/4-byte) przeszły; 167 faultów/188 real wywołań. Oryginalny klient API0.2 załadował rzeczywistą nową root shared library przez dyld i przeszedł.
- [x] **05-X.3** Rozróżnić błąd argumentów, formatu, rzeczywisty trap VM, OOM i niespodziewany błąd wewnętrzny w nowych wejściach. Limity/anulowanie dostaną osobne przyczyny w 0.6. Stare wartości enumów i klasyfikacja istniejących funkcji pozostają.
  Dowód: wspólny ErrorBoundary klasyfikuje typed stage/type bez analizy tekstu; obsługuje opcjonalny sized error output, ARG/FORMAT/TRAP/OOM/INTERNAL, aktywną fazę i raportowanie bez alokacji. Rzeczywiste load/validate, cztery błędy setup VM, dzielenie przez zero, fault OOM i wszystkie prefiksy przeszły. Pełny strict build SFML/Godot oraz 23/23 CTest i nowy test granicy na źródłach root pod ASan+UBSan przeszły; stare entry pointy zachowują politykę.
- [x] **05-X.4** Dodać load `_ex`, wspólny adapter błędów oraz status-returning query zamiast nowych void-getterów. W tym etapie określić kontrakt `generate_ex`; jego implementacja z parametrami następuje w 05-P.5, żeby nie tworzyć cyklu zależności X/P. Stare funkcje pozostają cienkimi adapterami z własną kompatybilną polityką. Nie duplikować VM ani validatora.
  Dowód: load_ex z uint64 byte_count, sized program_query i library_query_ex używają wspólnego load/validate/VM core oraz osobnych frozen legacy/new status policies. CheckedBoundary odrzuca kolizje error/primary output storage przed zapisem; failed load/query zachowuje poprzedni output, candidate żyje przez diagnostic catch. Kontrakt generate_ex opisany bez symbolu/cap do P.5. Raw odczyt legacy enum usuwa UB, zachowując sprawdzone stare zachowanie -1 i odrzucenie 99. Strict SFML/Godot build, 27/27 CTest, root checked shared/static/error ASan+UBSan i 40 new-load/167 legacy allocation faults przeszły. Oryginalny klient 0.2 z niezmienionym SHA przeszedł z rzeczywistą root dylib (dyld proof); niezależny review final patch bez findingów.
- [ ] **05-X.5** Ustalić zachowanie outputów na błędzie: sukces przekazuje dokładnie jeden nowy uchwyt, błąd nie przekazuje częściowego wyniku i nie nadpisuje istniejącego uchwytu klienta. Sprawdzić mały/zerowy/null err buffer, null query, invalid enum/flag i OOM; żaden wyjątek C++ nie przekracza ABI.

**Akceptacja:** klient może podejmować decyzje po kodach bez parsowania tekstu. Regresje obejmują dotychczasowe ścieżki z komunikatem, brak/stary tekst przy bad args/OOM i poprawne czyszczenie stanu błędu. Różnice starej i nowej polityki są udokumentowane.

## O. Jawny obraz i typowane wyjścia — P1

- [ ] **05-O.1** Dodać nowy sized view obrazu: width, height, byte_count, row_stride, pixel_format i jawnie opisana semantyka alfa/origin. Pierwszy wspierany format nadal RGBA8; nie dodawać nowych rendererów ani formatów pikseli tylko dla rozszerzenia deskryptora.
- [ ] **05-O.2** Dodać wyszukiwanie outputu po `(owner, name)` i odczyt schematu/slotu po ID. Utrzymać globalną kolejność root → party, wszystkie deklarowane sloty, singular count 0/1 i listy puste/niepuste.
- [ ] **05-O.3** Dodać typowane accessory punktu/prostokąta/okręgu z kontrolą rodzaju i indeksu, aby binding nie musiał odtwarzać pakowania 2/4/3 floatów. Stara tablica packed floats i jej stride pozostają identyczne.
- [ ] **05-O.4** Dokumentować długości stringów i czas życia wszystkich widoków. Wynik i jego dane pozostają poprawne po `program_free`; nowa generacja nie unieważnia poprzedniego wyniku. Zaktualizować odczyty SFML/Godot bez ponownej interpretacji ownera lub transformacji.

**Akceptacja:** poprawne typy i zakresy; błędny owner/name/kind/index zwraca status. Testy obejmują root i dwa party z identycznymi nazwami, puste sloty/listy oraz D4 i outputy helperów. Nowe odczyty i stare packed floats dają te same bity.

## M. Kopie wyniku i podstawowe statystyki — P1

- [x] **05-M.1** Zmierzyć bazowo load oraz serię generate dla małych/większych canvases, partów i list outputów. Rejestrować warunki, czas oraz możliwe do wiarygodnego pomiaru alokacje/kopie; benchmark nie jest niestabilnym progiem czasu na każdym PR.
  Dowód: benchmarks/capi/ zawiera cztery fixture’y, statyczny real C API benchmark i surową bazę pre-move z SHA/warunkami. 80 load + 80 generate, 4 seedy, C++ new calls/gross requested bytes; niezależny shared C API potwierdził wszystkie 80 wyników. Kontrola scalar/array/aligned/nothrow: 7 alokacji/558 bajtów; malloc/empty: 0. Zachowano executable i binaria poza repo dla M.5; kopie/peak/malloc niemierzone, brak progu czasowego CI.
- [ ] **05-M.2** Dodać wewnętrzne przekazanie wyniku VM przez przeniesienie buforów, używane jednokrotnie przez C adapter. Obecne kopiujące `VM::result() const` pozostawić dla istniejących konsumentów. Bez zmiany obliczeń, kolejności RNG i kosztów instrukcji.
- [ ] **05-M.3** Ograniczyć potwierdzoną duplikację nazw/danych przy budowie tabeli C. Zachować potrzebne pakowanie i stabilność wskaźników; nie reinterpretować pamięci niezgodnie z layoutem lub aliasingiem. Raportować faktyczny zysk zamiast zakładać zero-copy dla całego wyniku.
- [ ] **05-M.4** Dodać sized query statystyk wyniku: budżet, fuel remaining/used, bytes obrazu i liczba elementów wyjściowych. Używać istniejących liczników VM; wall-clock mierzy narzędzie hosta. Więcej liczników rozszerza 06-N.
- [ ] **05-M.5** Porównać wynik przed/po i zachować benchmark bazowy dla 06-K/M/N/Q. Pełne renderowanie do pamięci hosta pozostaje w 0.6; samo `copy_pixels` nie jest takim trybem.

**Akceptacja:** stare i nowe API zachowują ownership i bity wyniku. Statystyki paliwa odpowiadają VM, a raport identyfikuje usunięte kopie. Nie deklarujemy procentowej poprawy przed pomiarem.

## I. Korpus LLM i pomiar pierwszego etapu

- [ ] **05-I.1** Utrwalić 24 zadania: np. 16 assetów i 8 napraw, z wymaganiami obrazu/outputów, seedami i kryteriami oceny. Wspólny podzbiór nie wymaga składni niedostępnej w 0.4. Zdefiniować model/parametry/liczbę próbek i zapis promptów zanim zmienimy kompilator.
- [ ] **05-I.2** Przygotować prosty runner CLI: specyfikacja + właściwa referencja → źródło → check JSON → jedna naprawa → ponowny check → obraz/outputy. Adapter historycznego 0.4 używa zwykłej kompilacji w katalogu tymczasowym i diagnostyki tekstowej; kryteria sukcesu i budżety są wspólne, a niedostępne wtedy kody diagnostyk mają jawny brak danych. Zapisywać pełne próby i diagnostyki. Kontrolować czas i rozmiary; do 0.6 weryfikować zadeklarowany fuel/rozmiar przed wykonaniem, bez udawania dostępnych jeszcze limitów C API. Pełny adapter gramatyki i wieloetapowa naprawa są w 06-H/I.
- [ ] **05-I.3** Zapisać raport 0.4 → 0.5: first-pass, sukces po naprawie, kody błędów, trapy, zgodność obrazu/outputów, czas i tokeny. Te same zadania, model, parametry, limit napraw i seedy; podać SHA kompilatorów, referencji i promptów oraz ograniczenia zewnętrznego modelu. Kryteria wizualne wymagające człowieka mają zapis przeglądu; cele ilościowe ustalamy po baseline.
- [ ] **05-I.4** W CI odtwarzać zapisane odpowiedzi i naprawy bez żywego LLM. Pomiary z modelem uruchamiać osobno z limitem kosztu. Zachować dane do porównań 0.4/0.5/0.6 w 06-I; brak wykonanego pomiaru ma status brak danych i nie jest sukcesem.

**Artefakty docelowe:** `benchmarks/llm/` z manifestem, runnerem, promptami, surowymi wynikami, raportem i galerią. Ścieżka jest planowanym artefaktem, a nie istniejącą implementacją.

**Akceptacja:** odtworzenie zapisanych prób nie wymaga modelu; raport oddziela kompilację od poprawności wykonania i obrazu. Referencja 0.5 zawiera tylko funkcje 0.5.

## J. CI, zgodność i przygotowanie wydania

- [ ] **05-J.1** Rozdzielić metadane `product_version`, `language_version`, `format_version`, `c_api_version` i `c_abi_soversion` w eksporcie, manifestach i preflight. Wydanie 0.5 sprawdza faktycznie używany zamrożony format 0.4; nie wymaga pozornej nowej specyfikacji binarnej `iprop-v0.5.md`.
- [ ] **05-J.2** Dostosować testy pipeline release, fixture'y eksportu i dokumentacji oraz kolektor paczek do rozdzielonych wersji. Zachować weryfikację dokładnego commita, hashy, kompletności dokumentacji i zgodności z zainstalowanym kompilatorem.
- [ ] **05-J.3** Rozszerzyć testy języka/edytora/CLI oraz C API: old-client/new-library, sized structs, strict params, reflection/errors, ownership i statystyki. Zamrożony klient 0.2 jest budowany osobno i uruchamiany z nową shared library na wspieranych platformach. Dodać adekwatne ASan/UBSan i weryfikację eksportów/layoutów. Zaktualizować `cases.json` oraz `check-optimizations.py` razem, gdy dawny przypadek błędny staje się poprawny; nie usuwać pozostałych oczekiwanych błędów i trapów.
- [ ] **05-J.4** Sprawdzić nowy i stary korpus w trybach optimized/reference, debug/strip i compress/no-compress. Zachować zamrożone obrazy/outputy oraz zgodność native/WASM/C API/SFML/Godot. Istniejąca granica „optimized ≤ reference” nadal dotyczy optymalizacji; wzrost rozmiaru względem 0.4 raportować i uzasadniać oddzielnie.
- [ ] **05-J.5** Pakować pełny podręcznik 0.5, `LLM_REFERENCE.md`, opis C API 0.3, przykłady i manifest we wspólnym układzie SDK/WASM/strony. Gramatyka dołącza w 06-J. Sprawdzić faktycznie rozpakowane archiwa oraz SDK po przeniesieniu do innej ścieżki.
- [ ] **05-J.6** Zaktualizować SFML/Godot i przykład C99 dla nowych metadanych, parametrów i błędów; zachować legacy entry points. Po spełnieniu kryteriów zamknąć changelog, notatki migracji i raport benchmarku; przeprowadzić review finalnego zakresu, pełne CI i przygotować commit wydania 0.5.0. Merge `develop → main`, tag i publikacja stanowią późniejszy krok wydania, wykonywany w ramach zadania release.

**Akceptacja:** wszystkie wymagane joby CI przechodzą dla tego samego SHA; archiwa mają zgodne metadane i kompletne nowe materiały. Programy 0.4 zachowują wynik; nowe konstrukcje działają na istniejącym runtime formatu 0.4. C API 0.3 wynika z dodanych funkcji, nie z numeru języka; format pozostaje 0.4 i utrzymujemy dotychczasową linię ABI.

## Granice modułów

| Odpowiedzialność | Interfejs/moduł |
| --- | --- |
| Kompilacja/check | Jeden pipeline zwracający wynik w pamięci; zapis pliku i CLI są adapterami |
| Diagnostyka źródła | `Diagnostic`/`SourceRange`, kolektor, tekst/JSON i adapter edytora |
| Stałe i argumenty | Typowane stałe oraz wspólny binder/plan wywołania w istniejącym Planner/CallResolver |
| Formatter | Tokeny, zakresy i trivia; rozwiązane sygnatury zachowują kolejność efektów |
| Schemat assetu | Niezmienny model metadanych, wspólne get/find dla CLI/C API/integracji |
| Parametry | Jeden walidator i magazyn wartości powiązany ze schematem programu |
| Granica C | Tłumaczenie typów, własności i błędów; bez logiki języka i drugiego VM |
| Opis języka | Rejestry kompilatora + podręcznik + wykonywalne przypadki; pełna/kompaktowa dokumentacja |

Miejsce składania zależności pozostaje proste. Fabryki stosujemy do rzeczywistego wyboru implementacji, np. renderer diagnostyki lub backend generowania; nie tworzymy fabryki dla każdego AST/opcode. Analiza inicjalizacji i workspace wykonania dołączają jako osobne moduły w 0.6.

## Przykład docelowego zapisu 0.5

To specyfikacja planowanego zachowania, nie potwierdzenie działania obecnego 0.4. `let`, `fn` i konstruktory pojawiają się w przykładzie planu 0.6.

```ips
asset Crystal {
  size 32x32;
  input f32 offset = -0.5;
  input i32 count = -1;
  palette levels = [0, 0.5, 1];
  output point anchor;
  output rect bounds;

  draw {
    x = 8 + offset;
    paint(tint=#9dc, x=x);
    anchor = {x, 4};
    bounds = {x, 4, 8, 20};
  }

  paint(x: f32, tint: color) {
    rect(x=x, y=4, w=8, h=20, color=tint);
  }
}
```

## Zasady pracy i zamknięcia

- Po każdej ukończonej naprawie: review diffu i sąsiednich kontraktów → adekwatna weryfikacja → osobny commit. Większe funkcje dzielimy na kolejno działające kroki; refactor i release mają osobne commity.
- Testy dobieramy do zmiany; pełna macierz native/WASM/C/SFML/Godot, optymalizacji i paczek zamyka kamienie milowe/wydanie. Nie powtarzamy jej bez nowej zmiany lub ryzyka.
- Każdy etap wymaga własnej akceptacji, migracji przykładów i aktualizacji generowanych kontraktów.
- Nie dodajemy klas, generyków, first-class palet/rekordów, rekurencji, zagnieżdżonych importów czy nowych opcodes. Wykluczenia są opisane w referencji. Nie przenosimy całej biblioteki kompilatora do lekkiego runtime.

**Kryteria zakończenia:**

- [ ] Wszystkie zadania z etapów 0/A/B/C/F/G/V/R/P/X/O/M/I/J mają implementację, weryfikację i commity.
- [ ] Check/JSON, named args, poprawki signed/defaultów/palet i formatter działają wspólnie na realnych assetach.
- [ ] C API opisuje asset, waliduje parametry, udostępnia stabilne błędy i typowane odczyty; stary klient zachowuje kontrakt.
- [ ] Przenoszenie wyniku i statystyki są zweryfikowane, pomiary zachowują warunki i bazę dla 0.6.
- [ ] Korpus 0.4 zachowuje obrazy, outputy, trapy i RNG; format nadal 0.4.
- [ ] Dokumentacja, edytor, strona i rozpakowane SDK opisują ten sam commit i właściwe wersje; raport 0.4/0.5 jest gotowy.
- [ ] Finalny zakres ma review i zielone wymagane CI; release/merge/tag/publikacja pozostają osobnym zadaniem.

## Mapa pokrycia całego zakresu

Poniższa mapa jest rejestrem zadań, nie drugą listą do wykonania. **56/56 pierwotnych zadań ma przypisane miejsce**. Wiersz z oboma wydaniami oznacza świadomy podział podstawy i rozszerzenia; żaden fragment nie znika. Numery `05-*` i `06-*` wskazują checkboxy w odpowiednim dokumencie.

| Stare ID | Zachowany zakres | Nowe zadania |
| --- | --- | --- |
| `0.1` | Baza i korpus 0.4 | `05-0.1` |
| `0.2` | Kontrakt języka i migracja | `05-0.2`, `06-0.3`, `06-G.3` |
| `0.3` | Procedura i baseline LLM | `05-0.3`, `05-I.1` |
| `0.4` | Reproduktory niekonsekwencji/kolizji | `05-0.4`, `06-0.2` |
| `A.1` | Priorytety porównań | `05-A.1` |
| `A.2` | Signed defaulty/palety | `05-A.2` |
| `A.3` | Wspólny typ palet | `05-A.3` |
| `A.4` | Spójne input i32 | `05-A.4` |
| `A.5` | Średniki deklaracji | `05-A.5` |
| `B.1` | Pełny check bez zapisu | `05-B.1` |
| `B.2` | Wspólny model i renderery diagnostyk | `05-B.2` |
| `B.3` | Schemat JSON i zakresy Unicode | `05-B.3` |
| `B.4` | Stdout/exit codes/fix-it | `05-B.4` |
| `B.5` | Wiele błędów i recovery | `05-B.5` |
| `B.6` | Diagnostyki/fix-ity edytora | `05-B.6` |
| `C.1` | Jeden binder wszystkich wywołań | `05-C.1`, `06-E.3` |
| `C.2` | Named helper args | `05-C.2` |
| `C.3` | Named part args i at=vec2 | `05-C.3`, `06-E.1` |
| `C.4` | Wspólny plan wywołania | `05-C.4` |
| `C.5` | Kolejność efektów/RNG | `05-C.5` |
| `D.1` | Jawne let i typ | `06-D.1` |
| `D.2` | Helper fn | `06-D.2` |
| `D.3` | Pewność inicjalizacji | `06-D.3` |
| `D.4` | Legacy warnings i lint | `06-D.4` |
| `D.5` | Kolizje let/fn | `06-D.5` |
| `E.1` | Konstruktor vec2 | `06-E.1` |
| `E.2` | shape.circle/shape.rect | `06-E.2` |
| `E.3` | Rejestr i binder konstruktorów | `06-E.3` |
| `E.4` | Kolizje vec2/shape | `06-E.4` |
| `F.1` | Trivia/komentarze | `05-F.1` |
| `F.2` | Styl kanoniczny i fn | `05-F.2`, `06-F.1` |
| `F.3` | Bezpieczne deklaracje/let w formatterze | `05-F.3`, `06-F.2` |
| `F.4` | Format-only | `05-F.4` |
| `F.5` | Integracja formattera z narzędziami i naprawą | `05-F.5`, `06-F.3`, `06-I.1` |
| `G.1` | Eksport możliwości i ograniczeń | `05-G.1`, `06-G.1` |
| `G.2` | Efekty ze wspólnego rejestru | `05-G.2`, `06-G.1` |
| `G.3` | Generowana referencja LLM | `05-G.3`, `06-G.2` |
| `G.4` | Podręcznik/galeria/cases/migracja | `05-G.4`, `06-G.3` |
| `G.5` | Schematy i konsumenci eksportu | `05-G.5`, `06-G.4` |
| `H.1` | Maszynowa gramatyka i EBNF | `06-H.1` |
| `H.2` | Pełny/kanoniczny profil | `06-H.2` |
| `H.3` | Adapter gramatyki | `06-H.3` |
| `H.4` | Korpus zgodności parser/gramatyka | `06-H.4` |
| `H.5` | Manifest/hash i kontrola gramatyki | `06-H.5` |
| `I.1` | Pełny przepływ napraw/obrazu | `05-I.2`, `06-I.1`, `06-I.6` |
| `I.2` | 24 zadania LLM | `05-I.1`, `06-I.2` |
| `I.3` | Metryki i ocena obrazu | `05-I.3`, `06-I.3` |
| `I.4` | Porównanie wersji | `05-I.3`, `06-I.2`, `06-I.4` |
| `I.5` | Ablation gramatyka/naprawa | `06-I.4` |
| `I.6` | Deterministyczne CI i opcjonalny żywy LLM | `05-I.4`, `06-I.5` |
| `J.1` | Niezależne wersje komponentów | `05-J.1`, `05-V.4`, `06-J.1` |
| `J.2` | Release/paczki/fixture eksportu | `05-J.2`, `06-J.2` |
| `J.3` | Testy języka/CLI/edytora | `05-J.3`, `06-J.4` |
| `J.4` | Frozen runtime/backend/optimization matrix | `05-J.4`, `06-J.4` |
| `J.5` | Dokumentacja/gramatyka/przeniesiony SDK | `05-J.5`, `06-H.5`, `06-J.5` |
| `J.6` | Review i przygotowanie wydania | `05-J.6`, `06-J.6` |

Pełna gramatyka i pomiar jej wkładu przechodzą do 0.6 razem z nową składnią. Porównanie 0.4 → 0.5 pozostaje w 0.5; późniejszy raport rozszerza je o 0.6. Pakowanie dokumentów odbywa się w obu wydaniach, a grammar artifact dołącza po implementacji w 0.6.

| Dodana propozycja | Zachowany zakres C API | Nowe zadania |
| --- | --- | --- |
| `API-01` | Rozszerzalne ABI, sized structs, inicjalizatory, stride kolekcji i zgodność klienta | `05-V.1`, `05-V.3`, `05-V.4`, `05-V.5`, `05-J.3`, `06-J.3` |
| `API-02` | Wersja załadowanej biblioteki, capabilities, rozdzielenie produktu/API/formatu/SONAME | `05-V.2`, `05-J.1`, `06-0.3`, `06-J.1` |
| `API-03` | Introspekcja inputów/defaultów, deklaracji outputów, partów i istniejącej emisji | `05-R.1`, `05-R.2`, `05-R.3`, `05-R.4`, `05-R.5` |
| `API-04` | Typowane parametry, IDs, reset/clone, kontrola zgodności programu i i32 bits | `05-P.1`, `05-P.2`, `05-P.4`, `05-P.5` |
| `API-05` | Rygorystyczne uniformy: unknown/duplicate/type/argument przy zachowaniu legacy | `05-P.3`, `05-P.5`, `05-X.4` |
| `API-06` | Strukturalne błędy, lokalizacje, komunikaty dla bad args/OOM i checked query | `05-X.1`, `05-X.2`, `05-X.3`, `05-X.4`, `05-X.5` |
| `API-07` | Limity load/decode oraz fuel/piksele/pamięć/listy i odróżnienie limitu od OOM | `06-L.1`, `06-L.2`, `06-L.3`, `06-L.5` |
| `API-08` | Anulowanie generacji i długich operacji rasteryzacji | `06-L.4`, `06-L.5`, `06-Q.2` |
| `API-09` | Context, reuse buforów/planu, shared immutable program i kontrakt wątków | `06-K.1`, `06-K.2`, `06-K.3`, `06-K.4`, `06-K.5` |
| `API-10` | Usunięcie zbędnych kopii wyniku i metadanych, pomiary przed/po | `05-M.1`, `05-M.2`, `05-M.3`, `05-M.5`, `06-N.1` |
| `API-11` | Jawny image view, byte_count/stride/pixel format/alfa i czas życia | `05-O.1`, `05-O.4`, `05-V.5` |
| `API-12` | Lookup outputu po owner/name i typowane odczyty point/rect/circle | `05-O.2`, `05-O.3`, `05-O.4` |
| `API-13` | Statystyki paliwa/rozmiarów, profilowanie i benchmark wykonania | `05-M.4`, `06-N.1`, `06-N.2` |
| `API-14` | Generacja do bufora hosta, wspólne Canvas storage/view i opcjonalny copy_pixels | `06-M.1`, `06-M.2`, `06-M.3`, `06-M.4` |
| `API-15` | Synchroniczny batch z per-item status/error/ownership i niezależnym RNG | `06-Q.1`, `06-Q.2`, `06-Q.3` |
| `API-16` | Osobne C API kompilatora/check/import resolver/diagnostyki i metadane źródłowe | `06-T.1`, `06-T.2`, `06-T.3`, `06-T.4`, `06-T.5` |
| `API-17` | C++ RAII, jeden pełny binding Rust i przykłady C99/SDK | `05-R.5`, `06-U.1`, `06-U.2`, `06-U.3`, `06-U.4` |
| `API-18` | Integracje SFML/Godot, old/new client, sanitizers i przeniesiony SDK | `05-J.3`, `05-J.5`, `05-J.6`, `06-J.3`, `06-J.5` |

**Kontrola kompletności:** wszystkie powyższe ID mają rzeczywiste checkboxy, punkty etapów sumują się do 110/110, a każda wersja ma język/LLM, C API i osobne kryteria zgodności/wydania.
