# Code review: `develop` → `main`

**Wniosek: zalecam poprawki przed scaleniem.** Potwierdziłem trzy problemy P1 oraz dwa P2. Istniejący zestaw testów przechodzi, ale dodatkowe reprodukcje wykazują awarię procesu, rozbieżność bitów wyników native/WASM, błędy procesu wydania oraz dwa problemy nowych funkcji. P1 oznacza tutaj problem blokujący rekomendację merge; P2 — błąd funkcjonalny lub ograniczenie wymagające naprawy albo jawnej decyzji projektowej.

Review wykonane 2026-10-01 na macOS arm64. Porównane lokalne referencje:

- `main`: `7c1434f93e0fd005805b8a6b9a75e61869de2ca6` — również merge-base.
- `develop`: `367a15878b65124adbe50cde9e51c64fd35aa5c5` — HEAD podczas review.
- Zakres: 9 commitów, 67 zmienionych plików, 6202 dodane i 121 usuniętych linii. Przejrzano cały diff oraz powiązane ścieżki wykonania, walidację, kompilację, specyfikację, testy, instalację i wydawanie pakietu.
- Repozytorium nie ma skonfigurowanego remote; review dotyczy powyższych lokalnych commitów. Stan plików śledzonych był czysty. Ten raport jest jedynym dodanym plikiem w drzewie źródeł; implementacja nie została zmieniona.

## Potwierdzone problemy

### R1 · P1 — plik zaakceptowany przez walidator przerywa proces w `vec2.mul`

**Miejsce nowego błędu:** [runtime/arithmetic.cpp](runtime/arithmetic.cpp), linie 923–942, szczególnie 929 i 941. Powiązane: [runtime/validator.cpp](runtime/validator.cpp), linie 155–170; [backends/capi/iprop.cpp](backends/capi/iprop.cpp), linie 225–233.

`vec2.mul` i `vec2.div` sprawdzają, czy prawy argument jest wektorem. Każdy inny wariant trafia do `std::get<f32>`, bez sprawdzenia typu i bez zamiany błędu na `Trap`. Przy `i32` zostaje rzucony `std::bad_variant_access`. CLI oraz C API nie przechwytują tego wyjątku.

Istotne jest to, że taki stan można uzyskać **po udanej walidacji**. Walidator zapisuje wysokość stosu przy `if`, lecz nie odtwarza i nie scala typów alternatywnych ścieżek. Gałąź analizowana liniowo może zamienić `i32` na `f32`, chociaż wykonanie ją ominie. Ta słabość walidatora istniała na `main`; nowa implementacja operacji wektorowych zamienia ją w potwierdzone nieobsłużone zakończenie procesu.

Minimalne `repro.ipa`:

```text
meta: size=1x1 origin=0,0 variants=1 archetypes=1 fuel=1000000
output: point p
fn draw ( i32, i32 ) -> ( ):
  locals: vec2
  const.f32 1
  const.f32 2
  vec2.make
  const.i32 3
  const.i32 0
  if
    drop
    const.f32 3
  end
  vec2.mul
  local.set 2
  local.get 2
  vec2.x
  local.get 2
  vec2.y
  out.point p
endfn
```

```sh
build/tools/ipa repro.ipa -o repro.iprop
build/tools/ipval repro.iprop
build/tools/ipgen repro.iprop -o repro.png
```

**Zaobserwowane:** assembler i `ipval` akceptują program. `ipgen` kończy się `SIGABRT` (kod 134 w powłoce; `-6` w Python `subprocess`). Konsument skompilowany jako C również ładuje plik, po czym `iprop_generate` przerywa proces. W WASM wywołanie rzuca obiekt niebędący JS `Error`; kolejne poprawne generowanie w tym samym module działało.

**Naprawa:** w obu nowych operacjach obsłużyć jawnie `Vec2`, `f32` i nieprawidłowy typ, kończąc ostatni przypadek zlokalizowanym `Trap`. Walidator powinien przechowywać typowane stany wejścia/wyjścia bloków i scalać gałęzie, uwzględniając również ścieżkę bez `else`, `br` oraz nieosiągalność. Na granicy C API warto zapewnić, że żaden wyjątek C++ nie wydostaje się do hosta; samo dodatkowe `catch` nie naprawia walidacji.

**Regresja do dodania:** opisany plik i odpowiednik z `vec2.div`; niezgodne typy przy jednakowej wysokości stosu w `if/else` i skokach. Walidacja ma odrzucać plik, a bezpośrednio wywołany VM na niepoprawnym programie powinien zgłaszać `Trap`. Konsument C ma otrzymać kod błędu bez zakończenia procesu.

### R2 · P1 — nowe operacje wektorowe nie zachowują bitowej zgodności native/WASM dla NaN

**Miejsce:** [runtime/arithmetic.cpp](runtime/arithmetic.cpp), linie 909–912 (`vec2.add`), 961–966 (`vec2.dot`) i 970–979 (`vec2.rot`). Kontrakt: [spec/iprop-v0.3.md](spec/iprop-v0.3.md), §13, linie 1608–1610.

Operacje polegają na propagacji NaN przez natywne instrukcje zmiennoprzecinkowe. Ten sam plik `.iprop` daje różne znaki NaN w wynikach strukturalnych na native arm64 i WASM. Porównanie obejmowało surowe bity `f32`, nie tekstową reprezentację JSON ani samo `isNaN`.

Minimalne źródło `.ips`:

```text
asset t {
  size 1x1
  output point p
  draw(v: i32, a: i32) {
    n = pow(-2, 0.5)
    p = {n, -n} + {-n, n}
  }
}
```

| Backend | Bity `p.x` | Bity `p.y` |
| --- | --- | --- |
| Native arm64, `ipgen --float-bits` | `0xFFC00000` | `0x7FC00000` |
| WASM, `outputs[0].valueBits` | `0x7FC00000` | `0xFFC00000` |

Dodatkowy test różnicowy używał także bezpośrednich stałych `nan` i `-nan` w assemblerze, więc wynik nie zależy od sposobu otrzymania NaN przez `pow`. Łącznie sprawdzono 2535 przypadków: 15 operacji × 169 kombinacji wejść. Rozbieżności wystąpiły dla `add` — 2, `dot` — 2, `rot` — 16. Pozostałe 12 badanych operacji było zgodnych w tej próbce. Nie jest to dowód poprawności dla wszystkich bitowych wzorców wejść.

**Wpływ:** naruszony jest podstawowy kontrakt bitowo identycznych wyników strukturalnych. Nie stwierdzono tu różnicy obrazu; problem dotyczy bitów outputów i porównywania, zapisywania lub hashowania wyników.

**Naprawa:** doprecyzować reguły NaN dla nowych operacji i realizować je jawnie. §9 opisuje obecnie brak specjalnej obsługi NaN, a §13 wymaga pełnej zgodności bitowej — te wymagania trzeba uzgodnić. Przykładem istniejącej jawnej kanonizacji jest `fold`. Zmianę ograniczyć do uzgodnionej semantyki 0.3, chroniąc historyczne pliki 0.1/0.2.

**Regresja do dodania:** porównania surowych bitów native/WASM dla obu znaków NaN, kombinacji dwóch NaN, nieskończoności i zer ze znakiem, obejmujące wszystkie nowe operacje wektorowe. Samo porównanie pikseli ani `isNaN` tego błędu nie wykryje.

### R3 · P1 — zmiana wersji na 0.3.0 psuje sprawdzanie instalacji i wydania

**Miejsce zmiany:** [CMakeLists.txt](CMakeLists.txt), linia 4. Niezaktualizowani konsumenci: [scripts/install-consumer/CMakeLists.txt](scripts/install-consumer/CMakeLists.txt), linia 3, oraz [.github/workflows/ci.yml](.github/workflows/ci.yml), linia 100. Polityka zgodności: [cmake/Install.cmake](cmake/Install.cmake), linie 55–57.

Produkt ma teraz wersję `0.3.0`, ale test zewnętrznego konsumenta nadal wymaga `find_package(iprop 0.2 CONFIG REQUIRED)`. Pakiet używa `SameMinorVersion`, więc poprawnie odrzuca takie żądanie. Krok instalacji i konsumpcji jest wykonywany w macierzy CI Linux/macOS/Windows. Oddzielnie workflow backendów wywołuje `release.py` z wersją `0.2.0`, która nie zgadza się z kodem i narzędziami `0.3.0`.

**Reprodukcje wykonane lokalnie:**

```sh
cmake --install build --prefix /tmp/iprop-review-install
cmake -DPREFIX=/tmp/iprop-review-install -DWORK=/tmp/iprop-review-consumer -P scripts/install_smoke.cmake
python3 scripts/release.py 0.2.0 build --check
```

Instalacja się udaje. Test konsumenta nie znajduje zgodnego pakietu: żąda `0.2`, znajduje `0.3.0`. Skrypt wydania kończy się komunikatem `Requested, source and built product versions must match`. Nie jest to wynik uruchomienia zdalnego GitHub Actions; lokalnie odtworzono polecenia używane przez workflow.

W tymczasowej kopii konsumenta poza repozytorium zmiana samego wymagania na `0.3` pozwoliła zbudować i uruchomić wariant współdzielony oraz statyczny, z poprawnym wynikiem golden. To potwierdza problem wymaganej wersji, bez dowodu na regresję ABI.

**Naprawa:** zsynchronizować wymaganie konsumenta, wersję w CI i instrukcje wydania w README oraz przykład CMake backendu C API. Docelowo przekazywać oczekiwaną wersję produktu z jednego źródła. Zachować rozróżnienie: numer produktu/formatu `0.3` i istniejący numer API C `0.2` opisują różne kontrakty; nie ma podstaw do automatycznej zmiany numeru API C.

**Regresja do sprawdzenia:** oficjalny `install_smoke.cmake`, oba sposoby linkowania i `release.py 0.3.0 ... --check` na zbudowanych wymaganych backendach. Nie usuwać polityki `SameMinorVersion` tylko po to, by ukryć błędne wymaganie.

### R4 · P2 — obrót części przy różnej skali X/Y rozdziela obraz i geometrię outputów

**Miejsce:** [runtime/vm.cpp](runtime/vm.cpp), linie 187–200 i 262–272; [runtime/outputs.cpp](runtime/outputs.cpp), linie 24–33 i 52–54. Powiązana decyzja projektowa: [spec/iprop-v0.3.md](spec/iprop-v0.3.md), §8.1 oraz §10.5.

Subcanvas powstaje najpierw w rozmiarze `sizeX × scaleX`, `sizeY × scaleY`; transformacja D4 permutuje następnie już przeskalowane piksele. Outputy są transformowane w jednostkach assetu. Przy zamianie osi, tj. rodzajach D4 1, 3, 6 i 7, oraz `scaleX != scaleY`, te kolejności przestają opisywać tę samą geometrię.

Reprodukcja: część `tile` ma rozmiar `4×2`, rysuje pełny czerwony prostokąt i zwraca `box={0,0,4,2}`. Asset nadrzędny `8×8` wykonuje `tile.draw(seed,v,a,at={0,0},rot=90)` i przekazuje `tile.outputs.box` do własnego outputu.

| Render root | Skala X/Y | Faktyczny bounding box czerwonych pikseli | Output w jednostkach assetu | Output po przeliczeniu skalą root |
| --- | --- | --- | --- | --- |
| `8×8` | `1 / 1` | `2×4` px | `2×4` | `2×4` px |
| `16×8` | `2 / 1` | `2×8` px | `2×4` | `4×4` px |

**Wpływ:** geometria kolizji i kotwice przeliczane według skali root mogą nie pokrywać się z obrazem. Przypadek ma zerowy origin i zerowe przesunięcie, więc rozbieżności nie tłumaczy osobna konwencja położenia części.

**Ważne rozróżnienie:** implementacja realizuje literalną tabelę permutacji rastra z §10.5. Problem obejmuje sam projekt kontraktu: transformację rastra po skalowaniu zestawiono z transformacją outputów przed skalowaniem. Nie jest to wyłącznie odstępstwo kodu od tej tabeli.

**Naprawa:** uzgodnić wspólną kolejność operacji i układ współrzędnych. Jeżeli geometria ma pozostać w jednostkach assetu, wariantem jest odpowiednie zamienianie skal podczas przygotowania subcanvas dla D4 zamieniającego osie. Alternatywnie trzeba jawnie ograniczyć obsługiwane kombinacje skali/transformacji. Decyzja wymaga aktualizacji specyfikacji 0.3 i testów przed zamrożeniem formatu.

**Regresja do dodania:** zgodność pikseli z punktami i prostokątami dla ośmiu D4, niesymetrycznego rozmiaru, różnej skali X/Y, niezerowego origin i `at`, helperów oraz zagnieżdżonych części. Istniejące testy native/WASM obejmują także niesymetryczne skalowanie, ale zgodność obu backendów nie wykrywa wspólnego błędu relacji obraz–output.

### R5 · P2 — `scalar * vec2` zużywa nowy lokalny slot przy każdym wystąpieniu

**Miejsce:** [compiler/EmitExpr.cpp](compiler/EmitExpr.cpp), linie 195–207, szczególnie 204; [compiler/Emit.cpp](compiler/Emit.cpp), linie 46–49. Limit: [runtime/code.cpp](runtime/code.cpp), linie 68–69.

Emiter zachowuje poprawną kolejność ewaluacji, zapisując lewy skalar przed wyliczeniem prawego wektora. Każde wystąpienie dodaje jednak permanentny lokalny slot `f32`, mimo że po `local.get` jego czas życia się kończy. Liczba slotów rośnie z całkowitą liczbą takich wyrażeń, zamiast z maksymalną liczbą jednocześnie potrzebnych wartości.

**Reprodukcja:** funkcja `draw(v:i32,a:i32)` deklaruje `q={1,2}`, wykonuje kolejno 1024 przypisania `q=1.0*q`, a na końcu zapisuje `p=q`. Kompilator odrzuca ją komunikatem `compiled program failed validation: too many locals`. Ten sam program z 1024 przypisaniami `q=q*1.0` kompiluje się. To nie jest 1024 zmiennych użytkownika ani 1024 zagnieżdżenia.

**Naprawa:** typowana pula lokalnych zmiennych tymczasowych z zakresem życia. Slot skalaru powinien być zwalniany po wyemitowaniu odpowiadającego mu `local.get`; zagnieżdżone mnożenia nadal potrzebują osobnych, jednocześnie aktywnych slotów. Wspólny pojedynczy scratch bez śledzenia czasu życia przywróciłby problem nadpisywania wartości.

**Regresja do dodania:** długa sekwencja mnożeń w obu kolejnościach, mnożenia zagnieżdżone oraz prawy operand zawierający builtin/helper korzystający ze scratch i RNG. Zachować istniejącą kolejność efektów ubocznych.

## Ocena funkcjonalności i pokrycia zmian

Nowy toolkit jest wdrożony przez wszystkie główne warstwy: format, assembler/disassembler, walidator, VM, język `.ips`, kompilator oraz testy native/WASM. Rozdzielenie historycznych formatów i bramkowanie nowych instrukcji wersją są dobrym kierunkiem. W wielu miejscach zadbano o NaN, wartości brzegowe, stałą kolejność obliczeń, koszt fuel oraz zachowanie kolejności RNG.

| Obszar | Ocena po review |
| --- | --- |
| `vec2`, operatory, odczyt komponentów | Spójne rozszerzenie typu i outputów; naprawić R1, R2 i R5. Dostępność części operacji w `.ips` wymaga osobnej decyzji poniżej. |
| SDF i krzywe Béziera | Formuły i kolejność operacji opisane w specyfikacji, przypadki zdegenerowane uwzględnione w implementacji/testach. Główna szansa to wydzielenie geometrii i przygotowanie danych stałych przed pętlą pikseli. |
| Gradient noise, FBM, ridge, Worley, warp, jitter | Czytelnie zachowana separacja od zwykłej arytmetyki i kontrola parametrów. Istniejące testy deterministyczne oraz porównania backendów przechodzą. |
| Gain, quintic, ramp | Wdrożona semantyka brzegów; przy ramp z jednym elementem poprawnie zachowano ewaluację `t` dokładnie raz. Ramp z dużą paletą ma istotny koszt liniowy. |
| Operacje koloru i HSV | Powiązane reguły typów, emisji i runtime są pokryte testami. Nie znalazłem dodatkowego odtwarzalnego błędu w tym obszarze. |
| Dither, fold, polar, clamp, remap, smoothstep | Widać dbałość o jawne reguły matematyczne i porównania bitów, szczególnie w `fold` i clamp. Warto rozszerzyć ten standard na cały `vec2`. |
| Transformacje części | Wszystkie D4 i kombinacje transformacji mają implementację/testy. Relacja transformacji do skali wymaga rozwiązania R4; trzy reprezentacje tej samej reguły zwiększają ryzyko rozjazdu. |
| Zgodność i dystrybucja | Golden i konsumenci backendów przechodzą; sprawdzanie instalacji/wydania blokuje R3. Binaria przykładów `plant.iprop` i `portrait.iprop` zmieniają tylko bajt wersji minor na offsetcie 6, z 2 na 3. |

Nie traktuję odłożonego proof-of-concept motyla ani showcase'ów jako regresji: ich odroczenie jest jawnie zapisane w `PHASES.md`. Przejście istniejących testów nie oznacza zamknięcia przypadków R1–R5.

## Architektura: co podzielić i jakie granice wprowadzić

Podział parser → semantyka → kompilator → runtime → backendy jest sensowny. Backendom nie trzeba dodawać oddzielnych implementacji matematyki. Problem rośnie wewnątrz warstw: wiedza o jednej operacji jest powtarzana w enumie, nazwach mnemoników, assemblerze, walidatorze, typerze, emiterze i wykonaniu VM.

Sygnałem wzrostu odpowiedzialności są poniższe rozmiary. Liczba linii sama nie przesądza o błędzie; tutaj towarzyszy jej mieszanie domen i powielanie decyzji. `CONTRIBUTING.md` również zaleca małe pliki skupione na jednej odpowiedzialności.

| Plik | `main` | `develop` | Główne odpowiedzialności |
| --- | ---: | ---: | --- |
| `runtime/arithmetic.cpp` | 628 | 1194 | Matematyka skalarna, wektory, SDF, Bézier, krzywe, kolor i adapter instrukcji VM |
| `runtime/validator.cpp` | 584 | 798 | Przepływ sterowania, stos typów, wersje, operandy, outputy i sygnatury instrukcji |
| `language/ips/Typing.cpp` | 550 | 712 | Typowanie AST, tabela builtinów, części, palety i szczególne reguły wywołań |
| `compiler/EmitExpr.cpp` | 364 | 558 | Emisja wyrażeń, promocje typów, powtórne typowanie, palety, ramp i zmienne tymczasowe |

### A1. Wspólny opis instrukcji oraz osobny opis builtinów

Wprowadzić statyczny `OpcodeSpec`: opcode, mnemonic, minimalna wersja formatu, rodzaj operandu/aux oraz dozwolone sygnatury stosu. Korzystać z niego w wyszukiwaniu mnemoników, assemblerze i powtarzalnej części walidatora. Kontrolę przepływu i zależności od pliku pozostawić w dedykowanych funkcjach walidacji.

Oddzielny `BuiltinSpec` powinien mapować nazwy `.ips`, przeciążenia, typ wyniku, promocje i sposób emisji. Część builtinów rozwija się do wielu instrukcji, więc nie należy wymuszać relacji jeden builtin = jeden opcode. Przy takim podziale nowa prosta operacja wymaga jednego opisu i jednej implementacji, a test spójności może wykrywać brak mnemonika, bramki wersji lub adaptera.

Miejsca: `runtime/iprop/Constants.hpp`, `runtime/opcode_name.cpp`, `tools/ipa.cpp`, `runtime/validator.cpp`, początek `language/ips/Typing.cpp`, `compiler/EmitCall.cpp`. Koszt: średni; korzyść: mniejsze ryzyko pominięcia jednej warstwy przy następnym rozszerzeniu formatu.

### A2. Resolver/fabryka planów wywołań przed emiterem

Obecnie `Emitter::emitExpr` ponownie wywołuje `typeOf`, `emitBinary` typuje oba poddrzewa, a `EmitCall` jeszcze rozpoznaje nazwy i szczególne warianty. Część warunków transformacji sprawdzana jest dopiero podczas emisji. Emiter musi wiedzieć za dużo o semantyce języka.

Proponowana granica: `CallResolver::resolve(...)` tworzy `ResolvedCall` jako wariant `BuiltinCall`, `UserCall`, `PartCall`, `RampCall`. Plan zawiera rozstrzygnięte przeciążenie, typy/promocje, argumenty z zachowaną kolejnością ewaluacji, lokalizację źródłową i zweryfikowane stałe. Dla części zawiera gotowy `D4Transform`. Emiter dostaje typowane węzły/plany i odpowiada za układ instrukcji oraz slotów.

Fabryka jest tutaj uzasadniona, ponieważ konstruuje jeden z kilku planów z odmiennymi wymaganiami. Wystarczy funkcja lub mała klasa zwracająca `std::variant`; nie jest potrzebna hierarchia klas z metodami wirtualnymi dla każdej funkcji języka.

Miejsca: `compiler/Plan.cpp`, `compiler/EmitExpr.cpp:11–74`, `compiler/EmitCall.cpp`, `language/ips/Typing.cpp`. Koszt: średni/duży; wykonywać etapami, zaczynając od wywołań i transformacji części.

### A3. `D4Transform` i fabryka `PartRenderPlan`

Tabela złożenia transformacji jest w `compiler/EmitCall.cpp:316`, mapa pikseli w `runtime/vm.cpp:262`, a mapa współrzędnych w `runtime/outputs.cpp:24`. Używany jest surowy numer transformacji. Wydzielić mały, walidowany typ wartościowy z operacjami:

```text
D4Transform::fromParameters(rot, mirrorX, mirrorY)
swapsAxes()
mapPixel(pixel, rasterExtent)
mapPoint(point, assetExtent)
mapRect(rect, assetExtent)
compose(other)
```

`mapPixel` i `mapPoint` muszą pozostać różnymi operacjami: indeksy pikseli i ciągłe współrzędne krawędzi mają inne granice. Wspólny model ma utrzymywać ich relację, bez mechanicznego zrównania wzorów `w-1-x` i `sizeX-x`.

`PartRenderPlan::create(part, callerScale, placement, transform)` powinno obliczać rozmiar subcanvas, skalę, mapowanie origin, rozmiar po transformacji i koszt blitu, a także odrzucać nieprawidłowe rozmiary. `Execution` wykonuje już gotowy plan. Najpierw trzeba rozstrzygnąć R4 — samo przeniesienie obecnych wzorów do klasy nie poprawi kontraktu.

Koszt: średni; korzyść: jedna odpowiedzialność za geometrię części, łatwiejsze testy kompozycji i przygotowanie do kolejnych transformacji.

### A4. Czyste moduły matematyczne i małe adaptery VM

Podzielić `arithmetic.cpp` według domen na funkcje skalarne/wektorowe, SDF/Bézier, krzywe i kolor. Funkcje matematyczne powinny przyjmować wartości i zwracać wartości, bez znajomości całego `Execution`. Adapter instrukcji odpowiada za pobranie typowanych argumentów, ewentualny `Trap`, zapis wyniku i zmianę `ip`.

Wspólne reguły zaokrąglania, NaN i kolejności działań powinny mieć jedno miejsce tam, gdzie kontrakty rzeczywiście są identyczne. Przykład: wielomian `fade5` w `runtime/seeded.cpp:98` i quintic w `arithmetic.cpp:274` można współdzielić, pozostawiając odmienną obsługę zakresu na właściwej granicy API. Unikać ogólnego pliku `utils` skupiającego ponownie wszystkie domeny.

Dla rysowania krzywych przydatna jest fabryka `BezierKernel::quadratic(...)` / `cubic(...)`: raz przygotowuje dane krzywej, następnie `distanceTo(point)` działa dla każdego piksela. Każde wyniesienie obliczeń musi zachować dokładną kolejność i zaokrąglenia wymaganego algorytmu. Koszt podziału: mały/średni; optymalizację kernela traktować jako osobny krok.

### A5. Typowane zmienne tymczasowe i wspólne przygotowanie palet

`TemporaryLocals::acquire(type)` powinno zwracać uchwyt o kontrolowanym czasie życia; jego zwolnienie oddaje slot do puli danego typu. To rozwiązuje R5 i zastępuje rosnącą mieszankę specjalnych scratch slots oraz permanentnych tempów. Uchwyt powinien obejmować emisję instrukcji, dla których wartość musi pozostać dostępna.

`emitIndex` i `emitRamp` powtarzają wyszukanie palety, sprawdzanie jej elementów i emisję wyboru. Rozdzielić to na `ResolvedPalette` przygotowane w semantyce i `PaletteLowerer` odpowiedzialny za bytecode. Taka granica daje jedno miejsce do optymalizacji stałych i strategii wyboru elementów. Koszt: mały/średni.

### A6. Typowane ramki kontrolne walidatora

Zamiast samego `labels_: vector<size_t>` wprowadzić `ControlFrame` przechowujący rodzaj bloku, stan typów przy wejściu, oczekiwany stan wyjścia, stan gałęzi `then` oraz osiągalność. To jest granica odpowiedzialności potrzebna do R1, a nie tylko porządkowanie pliku.

Walidację pojedynczych zwykłych instrukcji można potem oprzeć na `OpcodeSpec`. Skoki, zakończenia bloków, wywołania i dynamiczne zależności od metadanych pozostają jawne i przeglądalne. Koszt: średni; wymaga mocniejszych testów kontroli przepływu przed przepisywaniem pozostałej części walidatora.

### A7. Wspólna lista źródeł runtime dla native i WASM

`runtime/CMakeLists.txt` i `backends/html5/CMakeLists.txt` ręcznie wyliczają te same źródła. Wyodrębnić deklarację źródeł lub funkcję tworzącą target runtime, używaną przez oba niezależne buildy. Po podziale `arithmetic.cpp` ryzyko pominięcia pliku w WASM będzie większe. Koszt: mały; weryfikacja: świeży build obu konfiguracji.

## Złożoność i wydajność

### Ramp: koszt liniowy mimo stałego parametru

W [compiler/EmitExpr.cpp](compiler/EmitExpr.cpp), od linii 537, wybór przedziału jest liniową sekwencją porównań. Dla palety `N` elementów rozmiar emisji wynosi `O(N)` na miejsce wywołania; dla `P` wywołań runtime koszt w najgorszym przypadku wynosi `O(P·N)`. Dotyczy to również stałego `t`, które mogłoby wskazać przedział podczas kompilacji.

Pomiar: obraz `64×64`, w każdej iteracji `pixel(x,y,ramp(colors,0.99))`, zmieniana tylko liczba kolorów. Budżet emitowany przez kompilator to 1 000 000 (`compiler/Assemble.cpp:77`). Są to pomiary rozmiaru pliku i deterministycznego fuel, bez twierdzenia o czasie CPU.

| Liczba kolorów | Rozmiar `.iprop` | Zużyte fuel / wynik |
| ---: | ---: | --- |
| 1 | 646 B | 79 501 |
| 8 | 1465 B | 300 685 |
| 32 | 3769 B | 792 205 |
| 64 | 6841 B | `fuel exhausted` |
| 128 | 12981 B | `fuel exhausted` |

To szansa optymalizacji i ograniczenie projektu, nie osobne naruszenie specyfikacji: obecny §12.11 opisuje taki sposób rozwijania ramp. Pierwszy krok to obliczanie stałego, czystego `t` podczas kompilacji przy zachowaniu bitów interpolacji. Dla dynamicznego `t` rozważyć drzewo decyzji po indeksie `i32`; nową instrukcję lookup dopiero przy uzasadnionej potrzebie rozwoju formatu. Zmiana emisji zmienia również zużycie fuel i powinna być świadomie objęta specyfikacją/testami. Nie wolno usuwać ewaluacji `t`, jeśli może zmienić RNG, narysować coś lub zakończyć się trapem.

### Wielokrotne typowanie AST

`typeOf` rekursywnie odwiedza poddrzewa, a emiter ponawia te zapytania na kolejnych poziomach. Dla `E` węzłów i głębokości `D` daje to potencjalnie pracę rzędu `O(E·D)`, czyli kwadratową dla długiego łańcucha wyrażeń. To ocena struktury wywołań, nie pomiar czasu kompilacji. Typowany plan/IR z A2 pozwala rozstrzygać typ i przeciążenie raz, a następnie emitować kod w jednym przejściu.

### Bézier: ograniczony algorytm, znaczny stały koszt na piksel

`runtime/canvas.cpp:78` wywołuje `distBezier` dla każdego piksela rozpatrywanego obszaru. Algorytm w `runtime/arithmetic.cpp:133` ma pięć punktów startowych i do pięciu ocen odległości dla każdego, czyli do 25 ocen plus sprawdzenie końców. Asymptotycznie koszt jest liniowy w powierzchni odwiedzanego obszaru, z dużym stałym mnożnikiem. Dane niezależne od badanego piksela są ponownie przygotowywane w funkcjach odległości.

Fabryka przygotowanego kernela z A4 jest sensownym miejscem optymalizacji. Faktyczny zysk trzeba zmierzyć, ponieważ część obliczeń może już wynosić optymalizator. Nie zalecam bez dowodu zamiany algorytmu na aproksymację o innych wynikach ani zmiany normatywnego kosztu fuel wraz z prostym refaktorem.

## Dalsze usprawnienia i decyzje API

1. **Diagnostyka transformacji.** `rot=45` przechodzi sprawdzenie typu w `Typing.cpp:456`, a dopiero emiter odrzuca zakres/stałość (`EmitCall.cpp:333`). CLI wypisuje `ipc: the rot transform must be 0, 90, 180 or 270` bez lokalizacji źródłowej. Parser diagnostyki w rozszerzeniu edytora oczekuje formatu z numerem linii/kolumny. Przenieść pełne rozstrzygnięcie transformacji do semantyki/planu oraz ujednolicić formatowanie `SourceError`. Ogólny problem formatowania istniał wcześniej; nowe reguły go dziedziczą.
2. **Dostępność operacji wektorowych w `.ips`.** VM/assembler mają `vec2.len`, `vec2.dot`, `vec2.rot`, ale nie ma odpowiadających im wpisów builtinów i emisji. `vec2.len({3,4})` nie parsuje się (`vec2` jest słowem kluczowym nieuwzględnionym w tej pozycji przez `ParserDetail`), a `len(...)` jest nieznane. Ustalić publiczne nazwy i spójne API. To luka użyteczności, nie dowód naruszenia zadeklarowanego zakresu wszystkich bindingów `.ips`.
3. **Konwencja dodatniego obrotu.** §9 dla `vec2.rot` podaje klasyczny wzór `{x*cos-y*sin, x*sin+y*cos}` i opisuje go jako przeciwny do ruchu wskazówek przy osi Y w dół. W takim ekranowym układzie ten wzór obraca wektor skierowany w prawo w dół, a `part.draw(rot=90)` według §10.5 obraca go w górę. Ujednolicić opis/kierunek dodatnich kątów przed utrwaleniem API; sama zgodność z oboma osobnymi wzorami nie daje spójnej konwencji użytkownikowi.
4. **Niezależne testy relacji, nie tylko zgodności backendów.** Dodać własności transformacji: identyczność, złożenie z odwrotnością, mapowanie granic i relację obraz–geometria. Porównanie dwóch kompilacji tego samego runtime potwierdza przenośność, ale nie rozstrzyga, czy wspólny algorytm odpowiada kontraktowi użytkownika.
5. **Zestaw wejść granicznych współdzielony przez operacje.** Obok fixture'ów obrazkowych utrzymywać małe programy z surowymi bitami float, błędnymi typami przy rozgałęzieniach i granicami limitów zasobów. Każdy test powinien sprawdzać konkretny kontrakt, np. brak wyjątku za C ABI lub zużycie slotów zależne od zagnieżdżenia.

## Wykonana weryfikacja

| Sprawdzenie | Wynik i zakres |
| --- | --- |
| `git diff --check main...develop` | PASS |
| `cmake --build build --parallel 6` | PASS; konfiguracja zawiera C API, SFML i Godot |
| `ctest --test-dir build --output-on-failure` | PASS, 6/6: C API, SFML rendering/errors/conformance, Godot conformance, smoke |
| `build/tests/iprop_tests --reporter compact` | PASS, 356 przypadków / 299 595 asercji |
| Świeży Debug z ASan, UBSan i `float-cast-overflow` poza repo | PASS, build i 2/2 cele CTest: C API conformance + smoke; SFML/Godot wyłączone w tej konfiguracji |
| Build Emscripten 6.0.10 w `build/wasm` i przygotowanie assetów HTML5 | PASS |
| Node harness backendu HTML5 | PASS, 9/9 |
| Konformancja native/WASM | PASS dla 7 pozycji golden, 116 porównań obrazów wraz z outputami i 19 powodów trapów |
| Dodatkowe 2535 przypadków różnicowych nowych operacji | FAIL: 20 rozbieżności bitów NaN w `add`, `dot`, `rot` — R2 |
| CLI smoke | PASS, sześć narzędzi |
| Instalacja pakietu | PASS |
| Oficjalny test zewnętrznego konsumenta | FAIL, wymaganie wersji 0.2 — R3 |
| Kopia konsumenta z wymaganiem 0.3, shared i static | PASS w obu wariantach |
| Dokładne polecenie release check z obecnego CI | FAIL, wersja 0.2.0 — R3 |
| Plik z R1 w CLI i konsumencie C | Potwierdzony `SIGABRT` po udanym ładowaniu/walidacji |
| D4 z R4 | Potwierdzona rozbieżność bounding boxu pikseli i przeskalowanego outputu |
| Sekwencja mnożeń z R5 | Potwierdzone `too many locals` tylko dla wariantu scalar-left |

Sanitizery nie zgłosiły błędów w istniejącym zestawie testów. Nie oznacza to, że dodatkowa reprodukcja z R1 jest poprawna — ujawnia ścieżkę nieobjętą tym zestawem. Nie uruchamiano zdalnego CI, natywnych buildów Linux/Windows ani interaktywnego UI edytora i przeglądarki. Wnioski o błędach poleceń CI wynikają z ich lokalnego odtworzenia i konfiguracji wersji.

Pliki reprodukcji, wyniki JSON, obrazy i build sanitizowany pozostawiono tymczasowo w `/tmp/infinite-props-review.7ErxQs`. Przydatne pliki to `branch-type.ipa`, `nan-add.ips`, `differential.js`, `differential-results.json`, `tile.ips`, `rotated.ips`, `rotated-16x8.png`, `temps-left.ips`, `temps-right.ips`, `complexity-results.json` oraz `sanitized/Testing/Temporary/LastTest.log`. Katalog `/tmp` nie jest trwałym archiwum; istotne wejścia, wyniki i warunki są opisane powyżej.

## Proponowana kolejność pracy

1. Naprawić R1 z testem kontraktu walidator → VM → C API oraz R3 z przejściem rzeczywistych skryptów instalacji i wydania.
2. Uzgodnić i przypiąć semantykę NaN dla 0.3, naprawić R2; zachować historyczne golden 0.1/0.2.
3. Rozstrzygnąć układ współrzędnych/skalowanie D4, naprawić R4 i wyodrębnić `D4Transform`/`PartRenderPlan`.
4. Naprawić R5 przez zakresowe sloty tymczasowe. To mała zmiana z mierzalnym efektem, niezależna od pełnego przebudowania kompilatora.
5. Osobnymi zmianami wyodrębnić moduły matematyki i wspólną listę źródeł CMake; następnie wdrażać `OpcodeSpec`, resolver wywołań i typowany plan.
6. Optymalizować ramp i przygotowanie krzywych na podstawie pomiarów, z porównaniem bitów, efektów ubocznych i uzgodnionego kosztu fuel.

Nie łączyłbym wszystkich refaktorów z naprawami w jeden duży commit. Każdy etap powinien zachować publiczne wyniki albo jawnie dokumentować uzgodnioną korektę kontraktu 0.3. Największą wartość dadzą małe typy wartościowe, czyste funkcje oraz kilka fabryk planów z dobrze określonymi warunkami konstrukcji.
