# Infinite Props 0.9.0 — specyfikacja i plan wykonania

**Status:** etapy 00–13 zakończone. Wszystkie siedem bindingów korzysta ze wspólnego C API 0.6 / ABI 0.5. Etapy 14–15 pozostają otwarte.

**Gałąź prac:** `develop`, publikacja kroków na `origin/develop`.

**Baza przeglądu:** `6bb08d9` (0.8.0); specyfikacja po przeglądzie z 2026-11-02.

**Zasada realizacji:** pełny cel produktu, wspólne mechanizmy wewnętrzne, małe sprawdzalne kroki.

## 1. Cel i granice wydania

0.9.0 ma generować kafelki, kamienne płytki, murki, bruk, spękaną ziemię i zwierzęce plamy jako tekstury, regiony geometryczne oraz niezależne dynamiczne części. Części można przemieszczać, kolorować, teksturować, rysować i zwracać hostowi z zachowaniem geometrii lub rastra oraz transformacji.

Obowiązkowy zakres obejmuje rozszerzenia noise, `noise.mask()`, Voronoi z anizotropią i układem rzędów, kontury, regiony, fugi, bevel, dynamiczne części i pełną politykę fuel. Podział na etapy nie oznacza przeniesienia tych funkcji do kolejnego wydania.

Uzgodnione uproszczenia:

- Jeden współdzielony model geometrii dla diagramów, konturów, regionów i części; publiczne typy pozostają rozróżnialne.
- Jeden mechanizm offsetowania dla `inset` i fug oraz jeden pipeline rasteryzacji nowych regionów i części. Istniejącego renderera nie przepisywać bez potrzeby.
- Rozmieszczenie sites, anizotropia i obliczanie diagramu są oddzielnymi etapami wewnętrznymi. Nie dodawać ogólnego systemu konfiguracji do IPS.
- Przy generowaniu samej tekstury używać bezpośrednio masek. Droga maska → kontury → regiony → maska jest potrzebna dopiero dla geometrii lub osobnych obiektów.
- `.iprop` zapisuje skompilowany program. Pełne dynamiczne części trafiają do wyniku API hosta. **Osobny trwały format zapisu wyników generacji nie należy do 0.9.** Wynik można odtworzyć z programu, kompletu wejść i zgodnego runtime.
- Procentowe rabaty fuel są celami do zweryfikowania, a nie powodem do zmiany obrazu lub podwyższania cen ręcznego rysowania.

Poza zakresem: ogólny `field<f32>`, struktury użytkownika, listy zagnieżdżone i ogólne listy uchwytów, kompilacja/import źródeł podczas wykonania, nowy system materiałów lub oświetlenia, dowolne kąty i skalowanie dynamicznych części, `noise.tile`, `noise.curl`, `noise.gradient` oraz utożsamianie ID Worley z ID oddzielnego diagramu Voronoi.

## 2. Zgodność i wspólna semantyka

### 2.1. Istniejące programy

- Zachować bitowe wyniki istniejących `noise()`, `noise.grad()`, `noise.fbm()`, `noise.cell()`, `noise.ridge()`, `fault()` i `warp()` oraz obraz, RNG, transformacje i wyjścia statycznych importów przy wystarczającym budżecie wykonania.
- Zachować dotychczasowe walidacje domen i błędy obliczeniowe. Zmiana fuel i nowe limity mogą przerwać wykonanie wcześniej; nie obiecywać identycznych trapów zasobowych przy tym samym budżecie.
- Nie zmieniać wartości istniejących ID builtinów, opcode, typów i rodzajów wyjść. Nowe wartości dopisywać; walidator ma odrzucać nieobsługiwane instrukcje i typy przed wykonaniem.
- W obecnym `.iprop` nie ma pola wersji programu: pola zastrzeżone w nagłówku muszą pozostać zerowe. **Taryfa jest własnością runtime 0.9 i obowiązuje także starsze programy.** Nie dodawać wersjonowania fuel ani pozornej gwarancji taryfy 0.8.
- Rozszerzyć kodowanie programu tylko o elementy potrzebne nowym typom, instrukcjom i deklaracjom wyjść. Stary runtime ma jawnie odrzucić program z nieobsługiwanym rozszerzeniem; stare programy mają nadal przechodzić walidację nowego runtime.

### 2.2. Determinizm i argumenty

- Funkcje noise i generatory z pseudolosowością używają jawnego `seed`, bez pobierania z aktywnego RNG. Tworzenie części ma deterministyczny licznik instancji; nie jest operacją czystą ani dopuszczalną w stałych assetu.
- Pozostałe nowe operacje przetwarzają przekazane dane. Nie zmieniają RNG poza efektami ewaluacji argumentów.
- Zachować obecne wiązanie argumentów: pozycyjne/nazwane, nazwy z `=`, ewaluacja raz w kolejności sygnatury. Nie dodawać drugiej składni argumentów ani dowolnych opcjonalnych parametrów.
- Sygnatury w blokach `text` opisują API; przykłady `ips` opisują docelowy język 0.9 i stają się kompilowalnymi przykładami w odpowiednich etapach. Krótsze warianty oznaczają konkretne przeciążenia, nie domyślne parametry helperów.
- Dla nowych algorytmów przed zamknięciem ich etapu przypiąć kolejność operacji, hashe/salty, reguły remisów, tolerancje i wektory wzorcowe w specyfikacji technicznej. Nie uzależniać wyniku od kolejności hash-map, adresów pamięci, czasu ani niejawnego RNG.

### 2.3. Współrzędne, zasoby i limity

Współrzędne są lokalnymi jednostkami aktywnego kontekstu rysowania; X rośnie w prawo, Y w dół. `origin: vec2` to lewy górny róg obszaru, `size: vec2` to dodatnia szerokość i wysokość. Wszystkie wartości geometryczne muszą być skończone i reprezentowalne w granicach runtime. Obszar poza targetem nie oznacza darmowej operacji.

| Zasób | Limit |
| --- | ---: |
| Domyślny fuel | 5 000 000 |
| Wspólna pamięć jednej generacji | 64 MiB |
| Sites / regiony w wyniku operacji | 256 / 256 |
| Wierzchołki pojedynczego obwodu / całego wyniku | 2048 / 65536 |
| Utworzenia dynamicznych instancji w całej generacji | 256 |
| Rekordy eksportowanych części łącznie we wszystkich slotach | 256 |

- Dziury liczą się do limitów geometrii. Istniejące limity canvasa, list i statycznych importów pozostają w mocy.
- Wszystkie nowe bufory, metadane, pojemności kontenerów, pamięć tymczasowa i zasoby zatrzymane przez eksport wchodzą do wspólnego limitera. Współdzielony bufor liczyć raz, osobne widoki i kopie — według zajętej pamięci.
- Licznik utworzeń części nie maleje po porzuceniu uchwytu. Zwolnienie pamięci nie resetuje limitu instancji.
- Sprawdzać argumenty i rozmiary przed alokacją; stosować arytmetykę bez przepełnień. Przekroczenie limitów wywołuje jawny trap, bez obcinania wyniku.
- Nie tworzyć pełnej kopii canvasa dla każdego regionu/części. Niemutowalne zasoby współdzielić referencyjnie i zwalniać po ostatniej referencji; wynik hosta może zatrzymać je po zakończeniu VM.
- Rezerwować fuel/pamięć/pracę przed operacją lub kolejną ograniczoną porcją. Nie publikować niekompletnego uchwytu ani wyniku nieudanej generacji. Nie wymaga to transakcyjnego cofania wewnętrznego canvasa po każdym opcode — nieudana VM nie udostępnia wyniku jako sukcesu.

## 3. Noise i bezpośrednie maski

### 3.1. Worley F2 i ID komórki

```text
noise.cell2(x: f32, y: f32, seed: i32) -> f32
noise.cell2(p: vec2, seed: i32) -> f32
noise.cell_id(x: f32, y: f32, seed: i32) -> i32
noise.cell_id(p: vec2, seed: i32) -> i32
```

- `cell2` zwraca F2, odległość do drugiego najbliższego punktu; `cell_id` zwraca 32-bitowy hash komórki źródłowej najbliższego punktu.
- Użyć tego samego rozkładu punktów, pełnego okna 5×5 i domen/trapów co `noise.cell()`. Dla poprawnych argumentów `F2 >= F1`.
- Remisy odległości rozstrzygać po rosnącym `(cy, cx)`, zgodnie z porządkiem obecnego skanowania. ID oprzeć na zamrożonym `cellHash(seed, cx, cy)` z interpretacją bitów jako `i32`; kolizje są możliwe.
- Wspólny kernel może zwracać F1, F2 i zwycięską komórkę. Obliczać je w jednym przeszukiwaniu, gdy potrzebne są razem, szczególnie dla `noise.mode_cell_edge`. Nie zmieniać kolejności arytmetyki istniejącego F1 ani jego błędów.
- `F2−F1` jest polem małych wartości przy granicach komórek; samo ID nie daje geometrii Voronoi.

### 3.2. Współrzędne po warp

```text
noise.warp_vec(x: f32, y: f32, amount: f32, seed: i32) -> vec2
noise.warp_vec(p: vec2, amount: f32, seed: i32) -> vec2
```

Zastosować istniejące wewnętrzne pole współrzędnych z `warp()` zamiast projektować kolejne:

```text
qx = legacy_fbm(x, y, 4, seed)
qy = legacy_fbm(x + 3.7f, y + 9.1f, 4, seed)
result = {x + amount * qx, y + amount * qy}
```

Zachować kolejność `f32` i istniejące reguły seedów oktaw. `amount=0` po walidacji wejścia zwraca wejściowe bity bez zbędnego próbkowania. Wejście i wynik muszą należeć do bazowej domeny `noise.grad`; po deformacji nie próbkować hipotetycznego zewnętrznego fBm. Odrzucać nieskończone wartości i naruszenia domen faktycznie wykonywanych próbek. Istniejące `warp()` zachowuje swój algorytm i walidację; nie zastępować go nowym wywołaniem, jeśli zmieniłoby to wynik lub kolejność błędów.

### 3.3. Konfigurowalne fBm

```text
noise.fbm(x: f32, y: f32, octaves: i32, lacunarity: f32, gain: f32, seed: i32) -> f32
noise.fbm(p: vec2, octaves: i32, lacunarity: f32, gain: f32, seed: i32) -> f32
```

- `octaves` przycinać do 1–8; `lacunarity` musi być skończone w `(0,8]`, `gain` w `[0,1]`.
- Znormalizowana suma gradient noise: początkowo amplituda i częstotliwość 1; po oktawie mnożniki odpowiednio `gain` i `lacunarity`. Użyć istniejącego schematu seedów oktaw.
- `gain=0` oznacza jedną rzeczywiście próbkowaną oktawę. Cena pozostaje oparta na zadeklarowanej efektywnej liczbie oktaw po przycięciu, bez uzależniania rabatu od wartości próbek.
- Walidować współrzędne po przeskalowaniu każdej wykonywanej oktawy. Przypiąć kolejność sumowania i normalizacji.
- Stare przeciążenia fBm pozostają osobnym zamrożonym kontraktem; współdzielenie kodu jest dopuszczalne tylko po potwierdzeniu zgodności bitowej i błędów.

### 3.4. `noise.mask()`

```text
noise.mask(origin: vec2, size: vec2, kind: i32,
           frequency: vec2, threshold: f32, seed: i32) -> mask
noise.mask(origin: vec2, size: vec2, kind: i32,
           frequency: vec2, threshold: f32, seed: i32,
           octaves: i32, softness: f32, comparison: i32, warp_amount: f32) -> mask
```

| Parametr | Kontrakt |
| --- | --- |
| `kind` | `noise.mode_value`, `mode_grad`, `mode_fbm`, `mode_cell`, `mode_cell_edge`, `mode_ridge` w przestrzeni `noise` |
| `frequency` | Obie składowe skończone i dodatnie |
| `threshold` | Skończony próg w surowych jednostkach wybranego noise |
| `octaves` | 1–8; używane dla fBm/ridge; walidowane także w pozostałych trybach |
| `softness` | Skończone, ≥0; szerokość przejścia w jednostkach wartości noise |
| `comparison` | `noise.above` albo `noise.below` |
| `warp_amount` | Skończone; 0 wyłącza deformację |

Krótkie przeciążenie odpowiada `octaves=4`, `softness=0`, `comparison=noise.above`, `warp_amount=0`.

- Wynik jest standardową maską A8 w skali aktywnego targetu, z przezroczystością poza zadanym prostokątem. Próbkować środki pikseli w lokalnych współrzędnych targetu, mnożonych składowo przez `frequency`; `origin` ogranicza obszar, nie dodaje ukrytego przesunięcia pola.
- Deformację wykonać po przeskalowaniu częstotliwością, przed próbką wybranego noise. `mode_cell_edge` używa F2−F1 z jednego przeszukania.
- Dla `softness=0`: `above` oznacza `value>=threshold`, `below` oznacza `value<=threshold`. Przy dodatnim `softness`: `t=clamp((value-threshold)/softness+0.5,0,1)`, alfa `above=t*t*(3-2*t)`, `below=1-above`; A8 to `floor(alpha*255+0.5)`.
- Używać tych samych kerneli co skalarne noise, z pełnymi guardami domen i bez RNG. Dla trybów fBm/ridge obowiązują istniejące domyślne parametry ich skalarnych wariantów.
- Wynik działa z istniejącymi `surface.paint`, `clip`, algebrą masek, morfologią i blur. Koszt rośnie z powierzchnią, próbkami, oktawami i deformacją.

## 4. Geometria i regiony

### 4.1. Publiczne typy i wspólny rdzeń

| Typ | Znaczenie |
| --- | --- |
| `diagram2d` | Przycięte komórki Voronoi, sites, wspólne krawędzie i sąsiedztwo |
| `contours2d` | Zamknięte obwody, orientacja, zagnieżdżenia i dziury |
| `regions2d` | Osobno adresowalne obszary z dziurami, pochodzeniem i metadanymi |
| `part2d` | Niemutowalny widok dynamicznej instancji: źródło, ID, D4 i wygląd |
| `parts2d` | Niemutowalny zbiór dynamicznych instancji, bez automatycznego renderu |

Uchwyty mogą być lokalnymi oraz argumentami i wynikami helperów. Nie są dozwolone w stałych assetu, ogólnym `list<T>`, samplerach, stepperach, paletach ani host inputs. `output list part` jest specjalizowaną deklaracją wyniku, nie ogólną listą uchwytów.

Diagramy, kontury i regiony współdzielą zapis punktów, obwodów, dziur i pochodzenia krawędzi. Odczyt metadanych nie przebudowuje geometrii. Wspólna krawędź diagramu ma jedną reprezentację, używaną w przeciwnych kierunkach przez obie komórki. Rasteryzacja musi przypisywać graniczne próbki konsekwentnie, bez przypadkowych szczelin i podwójnego krycia.

Publiczne współrzędne są `f32`; obliczenia wewnętrzne mogą być dokładniejsze. Algorytm, reprezentacja i tolerancje muszą zostać przypięte razem z wektorami native/WASM przed zamknięciem etapu. Jeżeli potrzebna jest biblioteka geometryczna, ograniczyć jej zakres do wspólnego rdzenia, przypiąć rewizję i licencję oraz objąć jej alokacje i pracę limiterem. Biblioteka nie jest kontraktem publicznym.

### 4.2. Voronoi

```text
voronoi(origin: vec2, size: vec2, count: i32, seed: i32) -> diagram2d
voronoi(origin: vec2, size: vec2, count: i32, seed: i32,
        method: i32, jitter: f32, relax: i32) -> diagram2d
voronoi(origin: vec2, size: vec2, count: i32, seed: i32,
        method: i32, jitter: f32, relax: i32,
        anisotropy: f32, direction: vec2) -> diagram2d
voronoi(origin: vec2, size: vec2, count: i32, seed: i32,
        method: i32, jitter: f32, relax: i32,
        anisotropy: f32, direction: vec2,
        row_bias: f32, row_jitter: f32, row_offset: f32) -> diagram2d
voronoi(origin: vec2, size: vec2, sites: list<vec2>) -> diagram2d
voronoi(origin: vec2, size: vec2, sites: list<vec2>, relax: i32) -> diagram2d

diagram.count(d: diagram2d) -> i32
diagram.site(d: diagram2d, index: i32) -> vec2
diagram.neighbors(d: diagram2d, index: i32) -> list<i32>
```

| Parametr | Zakres / domyślna wartość krótszego przeciążenia |
| --- | --- |
| `count` / długość `sites` | 1–256 |
| `method` | `voronoi.random` albo `voronoi.grid`; domyślnie random |
| `jitter` | Skończone 0–1; domyślnie 1; dla random walidowane, bez wpływu na bazowy rozkład |
| `relax` | 0–4; domyślnie 0 |
| `anisotropy` | Skończone 0.25–4; domyślnie 1 |
| `direction` | Skończony niezerowy wektor; normalizowany; domyślnie `{1,0}` |
| `row_bias` / `row_jitter` | Skończone 0–1; domyślnie 0 / 0 |
| `row_offset` | Skończone 0–1; domyślnie 0.5 |

- Komórki najbliższego punktu pokrywają prostokąt bez luk, są zamknięte i przycięte do jego granicy. Przy `anisotropy=1` obowiązuje metryka euklidesowa.
- Własne sites muszą być skończone, unikatowe i wewnątrz prostokąta z jego brzegiem. Duplikaty i punkty spoza obszaru powodują błąd, bez cichego usuwania. Punkty generowane nie mogą po kwantyzacji tworzyć duplikatów bez obsługi ograniczonej liczbą prób.
- `random` tworzy rozkład jednostajny; `grid` generuje uporządkowaną siatkę dokładnie `count` punktów z jitterem. Kolejność sites i sposób budowy niepełnego ostatniego rzędu należy zamrozić w etapie 05.
- Rzędy modyfikują rozmieszczenie punktów, anizotropia — metrykę. Linie rzędów biegną wzdłuż `direction`, aby `{1,0}` oznaczało poziomy mur. `row_bias` miesza bazowy rozkład z układem rzędów, `row_jitter` zaburza pozycje rzędów, `row_offset` przesuwa co drugi rząd.
- Dla jednostkowego `u=direction`, prostopadłego `v` i `a=anisotropy`, odległość porównawcza ma kwadrat `(dot(delta,u)/a)^2 + dot(delta,v)^2`. `a>1` wydłuża komórki wzdłuż kierunku. Przy transformacji przestrzeni przycinać do przekształconego prostokąta, nie jego AABB.
- Lloyd zastępuje sites centroidami przyciętych komórek i przelicza diagram, zachowując pierwotne indeksy. `relax=0` zachowuje sites. `row_bias=0` i `anisotropy=1` mają dawać wynik podstawowego przeciążenia.
- Komórki są uporządkowane po indeksie site. Sąsiedztwo wynika ze wspólnego odcinka dodatniej długości; stykanie w punkcie nie wystarcza. Zwracać unikatowe indeksy rosnąco; niepoprawny indeks powoduje trap.
- Sites i ID tego diagramu są niezależne od punktów i ID Worley, nawet dla identycznego seeda.

### 4.3. Kontury

```text
contour(source: mask, threshold: f32) -> contours2d
contour(source: mask, threshold: f32,
        simplify: f32, min_area: f32, connectivity: i32) -> contours2d
```

- Użyć deterministycznego Marching Squares z interpolacją próbek alfa, domknięciem na granicy rastra i jawnymi dziurami. Uwzględniać zapisane położenie, skalę i D4 maski.
- `threshold` jest skończone w `[0,1]`. Dla zera wnętrzem jest `alpha>0`; dla wartości dodatnich `alpha>=threshold`. Zerowe tło poza maską pozostaje zewnętrzem, także dla progu 0.
- `connectivity` to `contour.four` albo `contour.eight`; domyślnie four. Przypiąć tabelę przypadków siodłowych, trafienia dokładnie w próg i domknięcia brzegu; same nazwy trybów nie definiują interpolacji degeneracji.
- `simplify` i `min_area` są skończone i ≥0, domyślnie 0. Uproszczenie działa po ustaleniu topologii; nie może tworzyć samoprzecięć, odwracać orientacji ani usuwać dziur. Dopuszczalne jest pozostawienie większej liczby punktów, gdy żądana tolerancja naruszyłaby topologię.
- `min_area` filtruje całe składowe według pola z odjęciem dziur, nie usuwa samodzielnie małych dziur. Wyspy wewnątrz dziur są osobnymi składowymi.
- Obwody zewnętrzne i dziury mają przeciwne orientacje. Składowe porządkować malejąco po polu, potem po `(min_y,min_x,max_y,max_x)` bboxu i stabilnym indeksie wykrycia w skanie wierszowym. Przypiąć też kanoniczny punkt początkowy obwodu.
- Pusta maska daje pusty zbiór. Limity dotyczą również pracy i pamięci konturów później usuwanych przez filtr; `min_area` nie służy do ukrywania nieograniczonej pracy.

### 4.4. Regiony, metadane i maski

```text
regions(source: diagram2d) -> regions2d
regions(source: diagram2d, inset: f32, min_area: f32) -> regions2d
regions(source: contours2d) -> regions2d
regions(source: contours2d, inset: f32, min_area: f32) -> regions2d
regions.count(rs: regions2d) -> i32
regions.id(rs: regions2d, index: i32) -> i32
regions.area(rs: regions2d, index: i32) -> f32
regions.center(rs: regions2d, index: i32) -> vec2
regions.neighbors(rs: regions2d, index: i32) -> list<i32>
regions.points(rs: regions2d, index: i32) -> list<vec2>
regions.mask(rs: regions2d, index: i32) -> mask
regions.mask(rs: regions2d) -> mask
```

- Diagram daje po jednym regionie na komórkę. Kontury składają się w składowe z dziurami; geometria zerowego pola nie staje się regionem.
- Krótkie przeciążenia oznaczają `inset=0`, `min_area=0`. Nie dodawać osobnego publicznego `regions.inset()` ani dowolnych kombinacji częściowo pominiętych argumentów.
- `inset` musi być skończone: dodatnie pomniejsza wypełniony obszar, ujemne powiększa. Stosować round joins z przypiętą tolerancją w jednostkach świata, również na zewnętrznej granicy obszaru i wokół dziur. Offset nie modyfikuje diagramu źródłowego.
- Offset może usuwać regiony lub dzielić je na fragmenty. Zachować wszystkie niezerowe składowe, a `min_area>=0` zastosować po offsetowaniu przed sprawdzeniem limitu wynikowych 256 regionów. Powiększanie nie scala różnych regionów automatycznie.
- ID to pochodzenie: indeks site dla diagramu, indeks kanonicznie uporządkowanej składowej wejściowej dla konturów. Fragmenty po podziale zachowują ID źródła; indeks wyniku rozróżnia fragmenty. Nie obiecywać unikalności `regions.id` po podziale.
- Zachować kolejność źródeł; fragmenty jednego źródła porządkować jak składowe konturów. Pole i centroid liczyć z odjęciem dziur.
- `neighbors` oznacza pochodzenie ze wspólnej krawędzi diagramu, a nie kontakt po wykonaniu fugi. Zwraca aktualne indeksy zachowanych fragmentów, których pochodzenie krawędzi wskazuje sąsiednie komórki; usuwa brakujące i własny indeks, deduplikuje i sortuje. Dla źródła konturowego lista jest pusta.
- `points` działa tylko dla jednego obwodu bez dziur, z 3–256 punktami. Inne kształty wywołują trap, bez obcinania ani wypełnienia dziur.
- `mask(rs,index)` rasteruje pojedynczy region, a `mask(rs)` geometryczną sumę wszystkich regionów, bez wielokrotnego nakładania alfy. Uwzględnia dziury i aktywną skalę targetu; tworzy zasób, nie rysuje i nie wypala aktywnego clippingu do źródła. Pusty zbiór daje przezroczystą maskę.

### 4.5. Fugi i bevel

```text
regions.gap(source: regions2d, width: f32) -> regions2d
regions.gap(source: regions2d, width: f32, variation: f32, seed: i32) -> regions2d
regions.bevel(source: regions2d, width: f32, light: vec2) -> layer
regions.bevel(source: regions2d, width: f32, light: vec2,
              highlight: color, shadow: color, strength: f32) -> layer
```

- Fuga wymaga skończonego `width>=0`, `variation` w `[0,1]`. Krótka wersja ma variation=0 i nie wymaga seeda. Dla variation=0 odsuwa każdą granicę o width/2 tym samym kernelem co inset; zewnętrzny brzeg również się cofa. Width=0 zachowuje źródło.
- Stała szerokość jest docelowym odstępem na prostych wspólnych krawędziach. Round joins, narożniki i zanikające fragmenty nie gwarantują identycznej odległości w każdym punkcie.
- Przy variation>0 obie strony wspólnej krawędzi używają jednego deterministycznego profilu szerokości i wspólnej parametryzacji, niezależnej od orientacji obwodu. Zachować pochodzenie krawędzi; dla źródeł bez wspólnej topologii profil dotyczy poszczególnych obwodów i nie obiecuje dopasowania niezależnych wysp.
- Fugi stosują tę samą politykę rozpadu, ID i kolejności co offset. Kolejne wywołania działają na aktualnej geometrii i kumulują efekt.
- Bevel wymaga skończonego width>0, niezerowego skończonego light i strength w `[0,1]`; domyślnie highlight biały, shadow czarny, strength=0.5. Jawne kolory zachowują własną alfę, mnożoną przez siłę i krycie efektu.
- Bevel tworzy RGBA8 z cieniowaniem wewnętrznego pasa przy krawędziach, uwzględniając normalne i dziury. `light` wskazuje kierunek ku światłu; normalne zwrócone ku niemu otrzymują highlight. Regułę narożników i zaniku efektu przypiąć w etapie 08. Nie zmieniać regionów i nie renderować automatycznie do rodzica.
- Nie implementować osobnych silników dla inset, gap i bevel. Bevel korzysta z tego samego odczytu krawędzi i rasteryzacji, a wynik nakłada się przez `surface.paint(layer)`.

## 5. Dynamiczne części i wynik hosta

### 5.1. Tworzenie i tożsamość

```text
part.create(source: regions2d, index: i32) -> part2d
part.create(source: mask) -> part2d
part.create(source: layer) -> part2d
parts.from_regions(source: regions2d) -> parts2d
parts.count(source: parts2d) -> i32
parts.get(source: parts2d, index: i32) -> part2d
part.id(p: part2d) -> i32
part.source_id(p: part2d) -> i32
part.center(p: part2d) -> vec2
part.bounds_origin(p: part2d) -> vec2
part.bounds_size(p: part2d) -> vec2
```

- Region przechwytywany jest z dziurami i źródłowym ID. Maska zachowuje A8 z miękkimi brzegami, warstwa zachowuje RGBA8. Nie wymuszać konturowania źródeł rastrowych.
- `parts.from_regions` tworzy po jednej instancji na region w kolejności zbioru; pusty zbiór daje zero części. `parts.get` i kopiowanie uchwytu nie tworzą instancji ani nie kopiują geometrii. Indeks poza zakresem powoduje trap.
- Instancje otrzymują kolejne ID od 0, wspólne dla całej generacji wraz ze statycznie wywoływanymi komponentami. Dwa `part.create` tego samego regionu tworzą różne ID; `place/fill/texture` zachowują ID. ID nie jest trwałe między seedami ani zmianami programu.
- `source_id` zwraca ID regionu albo -1 dla źródła maskowego/warstwowego. Nie jest to ID Worley ani indeks eksportu.
- Maska/warstwa bez alfy większej od zera powoduje trap `empty dynamic part`. Region bez wyglądu może być maskowany lub eksportowany jako geometria; jego rysowanie wymaga uprzedniego `fill` albo `texture`.
- Dynamiczne części nie wywołują kodu statycznych importów, nie kopiują ich deklaracji, helperów ani output slots. `regions()` nie tworzy części automatycznie.

### 5.2. Wygląd, umiejscowienie i render

```text
part.place(p: part2d, at: vec2, rot: i32, mirror_x: i32, mirror_y: i32) -> part2d
part.fill(p: part2d, tint: color) -> part2d
part.texture(p: part2d, image: layer) -> part2d
part.draw(p: part2d) -> void
part.mask(p: part2d) -> mask
```

- Operacje zwracają niemutowalne widoki; wcześniejsze widoki zachowują swój wygląd i położenie. Źródła geometrii i rastra są współdzielone.
- `fill` i `texture` zastępują źródło wyglądu, ostatnie wywołanie wygrywa. `texture` jest próbkowana w oryginalnym układzie źródła i mnożona przez krycie kształtu; poza jej zasięgiem jest przezroczysta. `part.create(layer)` domyślnie zachowuje przechwycone RGBA, z alfą używaną **raz**, bez ponownego mnożenia jej przez samą siebie.
- `part.mask` opisuje kształt, niezależnie od tintu i tekstury. `fill` mnoży alfę koloru przez krycie kształtu. Dla źródła warstwowego zmiana wyglądu zachowuje pierwotną alfę jako kształt.
- Kotwica regionu to centroid z odjęciem dziur, dla rastra — środek bboxu pikseli o alfa>0. Bez `place` część pozostaje w położeniu źródłowym.
- `place` ustawia absolutną pozycję kotwicy i D4 względem oryginału, bez kumulowania poprzedniego `place`. `rot` przyjmuje 0/90/180/270, flagi odbić 0/1; są to wartości runtime. Kolejność: obrót, następnie odbicia, zgodnie z istniejącym D4.
- Używać istniejących rozróżnień mapowania krawędzi geometrycznych i indeksów pikseli. Po wyznaczeniu D4 obliczyć translację kotwicy w pikselach targetu i zaokrąglić każdą składową przez `floor(t+0.5)` także dla ujemnych wartości. Nie zaokrąglać niezależnie każdego wierzchołka.
- Raster jest przestawiany całymi pikselami, bez interpolacji i rozmywania. Zgodność skali źródła/targetu podlega tym samym zasadom co istniejące maski/warstwy; nie dodawać niejawnego skalowania. Metadane i eksport opisują efektywną transformację po zaokrągleniu, a nie nieosiągniętą pozycję żądaną.
- Geometria nie ma wrodzonej skali pikseli: dla D4 rasteryzować ją we współdzielonym pipeline w odwrotnie przekształconej skali targetu, także przy nierównych skalach X/Y. Nie kopiować ani osobno zaokrąglać wierzchołków. Dopasowanie siatki pikseli nadal obowiązuje rastry i tekstury. Jawne `place` utrwala siatkę zaokrąglonej translacji; nieumieszczona geometria dobiera ją przy użyciu.
- `center` zwraca efektywną pozycję kotwicy; bounds to osiowy bbox kształtu po transformacji, bez wpływu aktywnego clipu i wyglądu. Odczyty nie powodują ponownego skanowania pełnego źródła.
- `draw` renderuje w bieżącej kolejności, trybie legacy/source-over i aktualnym clipie. Powtórne rysowanie nie tworzy instancji. Brak wyglądu powoduje trap `dynamic part has no appearance`.
- `mask` materializuje przekształcony kształt jako A8 w układzie targetu; nie rysuje i nie utrwala aktualnego clipu. Clipping działa podczas późniejszego użycia maski. Zachować dziury i miękką alfę.

### 5.3. Eksport i własność danych

```text
output list part name
```

W `draw` przypisanie `name = p` dopina snapshot `part2d` w kolejności wykonania. Slot nie jest czytany jako wyrażenie, analogicznie do obecnych `output list point/rect/circle`. Nie dodawać singular `output part` ani ogólnego `list<part2d>`.

- Eksport i render są niezależne: `draw` nie eksportuje, przypisanie do slotu nie rysuje. Eksport bez wyglądu jest dozwolony i oznacza jawną geometrię/maskę bez appearance.
- Snapshot zawiera ID instancji, ID źródła, rodzaj kształtu i odwołanie do geometrii/A8/RGBA8, wygląd lub jego brak, kotwicę, efektywny D4/translację, skalę, bounds i indeks wpisu w slocie. Geometria zawiera obwody oraz przypisanie dziur, raster — format, rozmiar, stride i mapping.
- Indeks wpisu oznacza **kolejność eksportu**, nie historię renderingu. Część może być narysowana zero lub wiele razy i wyeksportowana w innym miejscu programu. Nie wprowadzać niejawnego rejestrowania wszystkich draw calls.
- Kilka snapshotów może mieć to samo `part.id` i inne transformacje/wygląd. Para slot+indeks rozróżnia rekordy; zasoby nie są duplikowane przy każdym wpisie.
- Wynik generacji jest właścicielem zatrzymanych zasobów; pozostaje ważny po zniszczeniu VM/programu i po kolejnych generacjach. Dane pożyczone przez C API żyją do zwolnienia wyniku; jawne kopie należą do hosta.
- Rozszerzać C++ result oraz checked C API o jawne rekordy i zapytania dla części oraz zasobów, według istniejących zasad rozmiarów deskryptorów, walidacji pojemności i niezmieniania wyjścia przy błędzie. Nie zmieniać layoutu starych publicznych struktur ani pakować części w tablice float dla point/rect/circle.
- Stare ścieżki API, które nie potrafią reprezentować nowego rodzaju wyjścia, mają zwracać jawny błąd nieobsługiwanej funkcji przy użyciu takiego programu/slotu, zamiast gubić części lub zwracać fałszywy pusty wynik. Działanie starszych programów pozostaje zgodne.
- Udostępnić równoważne dane we wszystkich utrzymywanych hostach: C, HTML5/WASM, Apple, Android, Unity, Godot, Python i SFML; współdzielić istniejącą infrastrukturę result queries. Nie sprowadzać eksportu do samego spłaszczonego RGBA.
- `.iprop` przechowuje instrukcje/deklaracje potrzebne generacji, **nie** jej gotowe snapshoty. Test round-trip programu to compile/load/generate; test eksportu to porównanie pełnego wyniku i własności danych przez host API. Nie dodawać osobnej serializacji wyniku.

## 6. Fuel i limity wykonania

### 6.1. Zasady

Fuel promuje deklaratywne operacje i ponowne użycie zasobów, ale nie jest dokładnym licznikiem CPU. Zachować domyślne 5 mln oraz niezależne ograniczenia pamięci, wymiarów i geometrii. Nowe operacje przed końcową kalibracją stosują konserwatywne opłaty; żaden etap nie dodaje bezlimitowej pracy natywnej.

- Każda wykonana operacja kosztuje co najmniej 1 fuel, a przetwarzanie danych ma dodatnią opłatę zależną od rozmiaru. Stosować całkowitoliczbowe zaokrąglenie w górę i zabezpieczenia overflow.
- Rezerwować koszt przed pracą lub przed następną ograniczoną porcją. Nie naliczać dwukrotnie tych samych próbek/skanów wewnątrz jednego builtinu; instrukcje programu i aktywny clipping nadal mają własny koszt.
- Nie zwiększać kosztu `pixel()`, helperów, zwykłych pętli ani lokalnych odczytów, żeby sztucznie powiększyć przewagę nowych funkcji.
- Użycie pustego/przezroczystego zasobu lub wyniku poza ekranem nie jest automatycznie darmowe. Rozliczać rzeczywiście wykonywane skany; udokumentowane szybkie ścieżki muszą pozostać ograniczone.
- Zmiana stawek nie może zmieniać obrazu, RNG, obliczeń ani wyników metadanych. Wpływ na dostępność wyniku przy wyczerpaniu zasobów jest jawnie dopuszczony przez §2.1.

### 6.2. Cele dla istniejących operacji

Punktem odniesienia jest **całkowity koszt operacji w 0.8**, bez instrukcji argumentów/programu wywołującego. Dla rabatu mnożnikowego: `newFuel=max(1,ceil(legacyFuel*multiplier))`. Nie wymuszać procentu, gdy minimum 1 lub pomiary bezpieczeństwa na to nie pozwalają.

| Operacja | Cel względem kosztu 0.8 |
| --- | ---: |
| `noise()`, `noise.grad()`, `fault()` | 20% |
| `noise.fbm()`, `noise.ridge()` | 30% |
| `noise.cell()` | 40% |
| `warp()` | 35% |
| `mask.union/intersect/subtract/invert` | 40% |
| `mask.expand/shrink` | 30–50%, po optymalizacji lub dowiedzionym ograniczeniu pracy |
| `layer.blur`, `mask.blur` | 60% |
| `surface.paint(layer/mask,...)` | 60% |
| Przechwycenie `layer {...}` | 70% samego narzutu bufora; rysowanie w bloku bez dodatkowego rabatu |
| `poly`, `curve`, `oval` | 70–80% |
| `sampler<T>.pick(n)`, `stepper<T>.pick(n)` | 50–70% narzutu zbiorczego; dodatni koszt każdego elementu |
| `color.mix/shade/hsv`, `ramp` | Do 50% rabatu, tylko przy koszcie bazowym >1 |

**Wynik pomiarów etapu 01:** istniejące skalarne noise/fault/warp oraz `color.mix/shade/hsv` kosztują w 0.8 po 1 fuel za samą operację, także przy 8 oktawach. Nie da się obniżyć ich ceny przy zachowaniu minimum 1. Nie zwiększać cen bazowych ani kosztu argumentów/pętli dla uzyskania procentu z tabeli; etap 13 ma odnotować to ograniczenie i oprzeć bezpieczeństwo na niezależnym limicie pracy. `ramp` jest obecnie loweringiem kompilatora, więc jego pełny koszt trzeba mierzyć wraz z tym rozwinięciem. Wzorzec i metodologia: [pomiary 0.8](benchmarks/baselines/v0.8.0/README.md).

- fBm/ridge mają cenę monotoniczną względem liczby oktaw; blur względem powierzchni i jądra; morfologia względem powierzchni i promienia.
- `cell2` i `cell_id` wyceniać jak pełny skan Worley; `warp_vec` według rzeczywiście wymaganych wewnętrznych fBm, a nie jak pojedynczy gradient noise.
- `mask.expand/shrink` muszą zachować dokładny wynik A8 i granice. W 0.8 taryfa zależy od logicznych próbek dysku, więc samo przemnożenie starej ceny nie zastępuje optymalizacji. Jeżeli brak bezpiecznej optymalizacji, pozostawić konserwatywną stawkę i opisać odchylenie od celu.
- `pick(n)` zachowuje liczbę i kolejność losowań oraz semantykę steppera dla n=0/1/16/256. Zbiorczy rabat nie oznacza darmowego n.

### 6.3. Nowe generatory i części

| Operacja | Co uwzględnia koszt |
| --- | --- |
| `noise.mask` | Bufor A8, odwiedzone piksele, oktawy, Worley F1/F2, warp i kwantyzacja |
| `voronoi` | Sites, generacja rzędów, metryka, budowa diagramu, wszystkie iteracje relaksacji i przypadki pesymistyczne |
| `contour` | Cały skan maski, wyprodukowane segmenty i topologia, także dane później odfiltrowane |
| `regions`, `gap` | Wierzchołki, składowe, offsetowanie, profil zmiennej fugi i pamięć tymczasowa |
| `regions.mask`, `bevel` | Przetworzone krawędzie, odwiedzone piksele, tworzenie buforów |
| Odczyty geometrii | Niski koszt stały oraz koszt kopiowania zwracanych list; bez przebudowy źródła |
| `part.create`, `parts.from_regions` | Uchwyt, ustalenie bboxu/alfy, dodatnia opłata za instancję; batch tańszy od równoważnych pojedynczych tworzeń |
| `place/fill/texture`, metadane części | Widok i walidacja, bez ponownej opłaty za współdzieloną geometrię |
| `part.draw`, `part.mask` | Faktyczne przetwarzanie geometrii/pikseli i materializacja potrzebnego rastra |
| Eksport | Rekordy, zatrzymane zasoby, ewentualne materializacje i kopie; bez powtórnego renderu |

Docelowa relacja dla porównywalnego obrazu: natywne generatory 10–20, gotowe maski/warstwy 20–30, figury 30–45, ręczna pętla `pixel()` 100. To wskaźnik projektowy, nie gwarantowany mnożnik każdego wywołania. Porównania muszą dotyczyć tej samej pracy lub jawnie opisanej zbliżonej jakości.

### 6.4. Niezależna kontrola pracy

- Dodać wspólny deterministyczny licznik pracy natywnej, obejmujący także operacje z rabatem: piksele/próbki, kroki geometrii i duże kopiowania. Musi mieć skończony domyślny limit niezależny od fuel, bez możliwości obejścia przez zagnieżdżone wywołania lub wiele małych builtinów.
- Udostępnić opcjonalny deadline hosta oraz bezpieczne anulowanie, sprawdzane również wewnątrz długich skanów i algorytmów. Pomiar czasu nie wpływa na losowania, wynik algorytmu ani deterministyczne naliczanie fuel.
- Przed obniżkami w etapie 13 ustalić i zapisać liczbowe limity domyślne oraz maksymalny odstęp pomiędzy punktami przerwania, na podstawie benchmarków. Sam timeout procesu lub Promise w JS nie wystarcza do przerwania synchronicznego WASM.
- Brak obsługi zegara w danym hoście nie wyłącza limitu pracy. Jeśli biblioteka nie pozwala ograniczyć pojedynczego wywołania, ograniczyć wejście lub zastosować inne wykonanie przed dopuszczeniem rabatu.
- Nowe błędy fuel/pamięci/pracy/anulowania pozostawiają hostowi niezmieniony parametr wyniku i nie publikują częściowego obrazu, eksportu ani zasobu.

## 7. Sprawdzane przykłady

Źródła są kompilowane, wykonywane i sprawdzane przez formatter razem z handbookiem:
[gepard](examples/language/cheetah-spots.ips), [żyrafa](examples/language/giraffe-spots.ips),
[mur](examples/language/stone-wall.ips), [bruk](examples/language/paving.ips),
[sucha ziemia](examples/language/dry-earth.ips) oraz
[dynamiczne kafelki z eksportem](examples/language/part-export.ips).
Tekstury używają bezpośrednio `noise.mask`; geometrię i części tworzymy wtedy,
gdy są potrzebne osobne regiony, transformacje lub eksport.

## 8. Zasady realizacji, odhaczania i publikacji

- Kroki realizować na `develop` w poniższej kolejności. Podpunkty są jednostkami pracy; większy etap może mieć kilka spójnych commitów. Etap 13, zgodnie z dyspozycją użytkownika, otrzymuje jeden commit po ukończeniu całego etapu.
- Przed zmianami sprawdzić branch, stan drzewa, instrukcje repo i rozbieżność z `origin/develop`. Nie dołączać cudzych zmian, nie nadpisywać zdalnej historii i nie używać force push.
- Checkbox `[x]` potwierdza wykonaną pracę i wskazaną weryfikację, nigdy sam plan ani stub. Po każdym podpunkcie/kroku gotowym do przekazania aktualizować checklistę i wynik weryfikacji w tym samym commicie co zmiany.
- Po sprawdzeniu staged diff wykonać commit i push na `origin/develop`. Potwierdzić równość lokalnego i zdalnego SHA. Dopiero wtedy raportować krok jako dostarczony; nieudany push jest otwartą przeszkodą, nie sukcesem publikacji.
- Wynik sprawdzeń i ewentualną blokadę publikacji zapisać przy etapie lub w jego dzienniku. SHA commitu podawać w raporcie po pushu; nie próbować wpisywać do commitu jego własnego hasha. Kolejny commit może dopisać odnośnik do poprzedniego.
- Etap 00 obejmuje wyłącznie ten dokument. Żaden podpunkt implementacyjny nie jest zakończony przez samo przepisanie specyfikacji.
- Nie uruchamiać publikacji release, merge do `main`, tagowania ani wdrożenia strony w ramach tej checklisty. Aktualizacja dokumentacji/strony w kodzie nie oznacza deploymentu.
- Każdy etap z publicznym builtin/type/output obejmuje parser i semantykę, planowanie/lowering, emitter, walidator, runtime, formatter i eksport opisu języka w zakresie wymaganym zmianą. Nie uznawać samego zielonego builda za weryfikację semantyki.
- Przed zamknięciem etapu algorytmicznego zapisać techniczny kontrakt i wektory wynikowe, a przed wydaniem przenieść ustalenia normatywne do zamrożonej specyfikacji 0.9. Ten plik pozostaje checklistą realizacji i śladem weryfikacji.

## 9. Checklista wykonania

### Etap 00 — przepisanie i sprawdzenie specyfikacji

**Zależności:** brak. Pierwszy commit/push zawiera tylko ten plik.

- [x] 00.1 Przepisać zakres i wspólne kontrakty zgodnie z uzgodnionymi uproszczeniami, bez usuwania obowiązkowych funkcji.
- [x] 00.2 Rozdzielić format programu, wynik hosta i ABI; opisać niewersjonowaną taryfę runtime oraz pełną własność eksportowanych zasobów.
- [x] 00.3 Dodać etapy 00–15 z podpunktami, zależnościami, kryteriami odbioru oraz procedurą commit/push.
- [x] 00.4 Sprawdzić kompletność API względem poprzedniej propozycji, spójność przykładów i odsyłaczy, checklistę i whitespace; przejrzeć staged scope.
- [x] **Etap 00 zakończony — dokument gotowy do implementacji.**

**Odbiór:** pełny uzgodniony zakres i jawne uproszczenia, wyłącznie dokumentacyjna zmiana, brak odhaczonych funkcji 0.9.

**Weryfikacja (2026-11-02):** zachowano 52/52 wcześniejsze sygnatury i dodano jawne `regions.mask(rs)`; parser Markdown poprawnie rozpoznaje strukturę, tabele i 3 przykłady IPS; 10 lokalnych odnośników prowadzi do istniejących plików; 16 etapów i 76 numerowanych podpunktów ma poprawną, unikatową kolejność. `git diff --cached --check` przechodzi; staged scope obejmuje tylko ten dokument. Przykłady opisują przyszłe API — nie twierdzimy, że kompiluje je runtime 0.8. Testy runtime/build pominięto, ponieważ kod nie jest zmieniany. Potwierdzenie commit/push i SHA należy do raportu dostarczenia, zgodnie z §8.

### Etap 01 — zamrożenie zgodności i pomiary bazowe

**Zależności:** 00.

- [x] 01.1 Utrwalić wektory bitowe noise/fault/warp 0.8 oraz domeny, remisy Worley i dotychczasowe błędy; objąć warianty scalar/vec2 i stałe składane przez kompilator.
- [x] 01.2 Utrwalić RNG, D4, clipping, legacy/source-over i point/rect/circle outputs, także dla zagnieżdżonych statycznych importów.
- [x] 01.3 Zapisać odtwarzalny benchmark CPU/fuel/pamięci operacji z §6, z rewizją, toolchainem i parametrami; oddzielić pomiary od testów bitowych.
- [x] 01.4 Potwierdzić istniejące fixtures ID/opcode/formatu i wynik native/WASM; nie regenerować wzorców jedynie po to, by dopasować je do nowego wyniku.
- [x] **Etap 01 zakończony.**

**Odbiór:** możliwe jest wykrycie zmiany choćby jednego bitu starszych funkcji oraz porównanie całkowitych kosztów operacji.

**Weryfikacja (2026-11-02):** [wzorzec i raport](benchmarks/baselines/v0.8.0/README.md), [wyniki kontroli](benchmarks/baselines/v0.8.0/verification.json), [metodologia i polecenia](benchmarks/operations/README.md). Zamrożono 78 wektorów, 3 remisy Worley, 2 binaria 0.8, 193 opcode i 126 kontraktów przeciążeń; sprawdzono 8 konfiguracji kompilatora i statyczne importy w 8 D4, przy 3 seedach i 2 skalach. Rozszerzony gate przechodzi natywnie i w świeżym WASM: 480 generacji, 64 błędy runtime i 16 diagnostyk stałych; kontrolne zmiany jednego bitu wyjścia oraz RGBA są wykrywane. Pełny CTest: 72/72; Node: 10/10; pełny native/WASM conformance wraz z rozszerzonym gate przechodzi. Benchmark: 141 scenariuszy, 15 próbek po 16 generacji i 3 próbki rozgrzewkowe; drugi przebieg odtwarza fuel całkowity/samej operacji, alokacje, szczyt pamięci i hashe. Pamięć oznacza żądane bajty C++, nie RSS ani logiczny limit VM. Dotychczasowe golden/fixtures niezmienione; kod runtime i kompilatora niezmieniony. Potwierdzenie commit/push i SHA w raporcie dostarczenia, zgodnie z §8.

### Etap 02 — rozszerzenia noise

**Zależności:** 01.

- [x] 02.1 Wprowadzić wspólny skan F1/F2/winner, `cell2` i `cell_id` z zamrożonym remisem i hashem, bez regresji F1.
- [x] 02.2 Dodać `warp_vec` oparte na istniejącym polu warp oraz konfigurowalne fBm; zachować stare przeciążenia i ich domeny.
- [x] 02.3 Uzupełnić katalog, przeciążenia vec2, const folding tam, gdzie dopuszczalny, lowering i walidację bytecode; nowe ID tylko dopisywać.
- [x] 02.4 Sprawdzić F2≥F1, granice i remisy, amount=0, octaves poniżej/na/powyżej 1–8, gain=0/1, lacunarity przy granicach, NaN/Inf i przekroczenia domen po skalowaniu/warp.
- [x] **Etap 02 zakończony.**

**Odbiór:** wektory starego noise bez zmian, nowe wyniki native/WASM zgodne, RNG niezużywany, konserwatywne koszty naliczane przed pracą.

**Weryfikacja (2026-11-02):** [kontrakt techniczny](spec/noise-v0.9.md), [wektory bitowe](tests/fixtures/noise-v0.9.json), [raport](spec/verification/noise-v0.9.json), [przykład IPS](examples/language/noise-extensions.ips). Dodano opcode `0xDF`–`0xE2`, bez zmiany istniejących ID i wzorców 0.8; stare przeciążenia fBm zachowują pozycje w katalogu. Natywny test skupiony: 4661 asercji w 5 przypadkach. Nowy gate native/WASM: 63 wektory z niezależnego modelu binary32, 143 generacje (6048 obserwacji wzorcowych), 87 trapów, 14 diagnostyk stałych i 36 odrzuceń bytecode; obejmuje 8 konfiguracji kompilatora, scalar/vec2, stałe i IPA/IPDIS. Zachowany gate 0.8: 480 generacji, 64 trapy i 16 diagnostyk bez zmian. Pełny CTest: 73/73; Node: 10/10; pełny native/WASM conformance przechodzi. Zachowane narzędzia 0.8 odrzucają 8 programów z nowymi instrukcjami (osiągalnymi i nieosiągalnymi). Generowanie handbooka: 85 builtinów, 150 przykładów. Koszt rezerwowany przed pracą: `cell2/cell_id` 25, `warp_vec` 8 (1 przy zerze), konfigurowalne fBm 1–8 także przy gain=0; integracja pełnej taryfy i limitu pracy pozostaje w etapie 13. Potwierdzenie commit/push i SHA w raporcie dostarczenia, zgodnie z §8.

### Etap 03 — `noise.mask()`

**Zależności:** 02; korzysta z istniejących limiterów masek.

- [x] 03.1 Dodać oba przeciążenia, stałe trybów i walidację; współdzielić kernele skalarne i pojedynczy skan F1/F2.
- [x] 03.2 Zaimplementować próbkowanie środków, zakres origin/size, frequency, warp, progowanie, softness i kwantyzację A8 według §3.4.
- [x] 03.3 Zintegrować z paint/clip/algebrą masek oraz zachować mapowanie i skalę; nie tworzyć typu pola ani pętli IPS wewnątrz lowering.
- [x] 03.4 Porównać A8 z ręczną referencją dla każdego trybu, progów, miękkich brzegów, różnych skal i origin; sprawdzić RNG oraz przerwanie fuel/pamięcią.
- [x] **Etap 03 zakończony.**

**Odbiór:** tekstury plam i szczelin działają bez geometrii, każda próbka i bufor są rozliczane, trap nie publikuje maski.

**Weryfikacja (2026-11-02):** [kontrakt techniczny](spec/noise-mask-v0.9.md), [wektory A8](tests/fixtures/noise-mask-v0.9.json), [raport](spec/verification/noise-mask-v0.9.json), [przykład plam i szczelin](examples/language/noise-mask.ips). Świeży build Release poza iCloud: 74/74 CTest, 6 testów maski / 15615 asercji, Node 10/10; pełny native/WASM PASS. Nowa bramka sprawdza 52 niezależne obrazy A8, 388 generacji, 15546 obserwacji pikseli, 162 trapy, 20 odrzuceń bytecode i 3 błędy kompilacji. Ręczna referencja obejmuje sześć trybów, twarde i miękkie progi, oba porównania, warp, częstotliwości anizotropowe, różne skale, origin ujemny/ułamkowy i przeniesienie pokrycia spoza targetu. Sprawdzone paint/clip/algebra, dodatni blur i morfologia, osiem D4, współdzielenie eksportu, RNG, granice fuel i wspólnego limitu pamięci oraz brak wyniku po trapie. Oba nowe opcode są odrzucane przez zachowane runtime 0.8 i etap 02, także w martwym kodzie. Stare wektory i golden pozostają bez zmian. Handbook: 86 builtinów / 151 przykładów; eksport języka i jego addytywne pole maski mają sprawdzony schemat (181 mutacji, 151 rzeczywistych przypadków CLI). Koszt maski jest konserwatywny i rezerwowany przed alokacją; pełna kalibracja taryfy i niezależny limiter pracy pozostają w etapie 13.

### Etap 04 — wspólna geometria, typy VM i zasoby

**Zależności:** 01; wykonywany po 03.

- [x] 04.1 Dodać pięć typów uchwytów z obsługą lokalnych i helperów oraz odrzuceniem niedozwolonych kontenerów, inputs i stałych.
- [x] 04.2 Wprowadzić współdzielone punkty/krawędzie/obwody/dziury i pochodzenie; cache metadanych bez osobnych kopii geometrii dla każdego publicznego typu.
- [x] 04.3 Objąć limiterem pamięć zasobów, kontenerów i pracy tymczasowej, także zależności zewnętrznych; przypiąć wykorzystane źródła/licencje i nie dodawać niepotrzebnych bibliotek.
- [x] 04.4 Zintegrować typowanie i walidator, brak przypisania, scope i zwalnianie; sprawdzić konflikty namespace/importów bez zbędnego rezerwowania zwykłych nazw lokalnych.
- [x] 04.5 Przetestować przekroczenia limitów, wycofanie nieudanego tworzenia, zwalnianie po ostatniej referencji i brak wycieków po trapach; utrwalić techniczny kontrakt geometrii.
- [x] **Etap 04 zakończony.**

**Odbiór:** ograniczony pamięciowo wspólny rdzeń, działające uchwyty VM, bez ogólnego systemu struktur/list i bez naruszenia starszego kodowania.

**Weryfikacja (2026-11-02):** [kontrakt rdzenia](spec/geometry-core-v0.9.md), [wektory topologii i metadanych](tests/fixtures/geometry-v0.9.json), [raport](spec/verification/geometry-core-v0.9.json), [przykład typowanych helperów](examples/language/geometry-handles.ips). Świeże buildy Release poza iCloud: 76/76 CTest, 11 testów geometrii / 3011 asercji, Node 10/10; pełny native/WASM PASS. Nowa bramka sprawdza pięć różnych uchwytów, trzy niezależne topologie, bity metadanych, dokładne progi pamięci/fuel, 60 generacji, 35 odrzuceń bytecode, pięć trapów i 25 błędów kompilacji. Sprawdzone maksima 256 sites/regionów, 2048/65536 wierzchołków, rzeczywiste zapełnienie wspólnego limitu 64 MiB, 244 błędy alokacji w czterech ścieżkach, pamięć tymczasowa i pojemności, ostatnia referencja, zachowanie po zniszczeniu wykonania, współdzielenie rastra oraz płaskie pochodzenie kolejnych operacji. Odczyt niezainicjalizowanego uchwytu pozostaje trapem również po optymalizacji nieużywanego przypisania. Nowe nazwy typów są kontekstowe; istniejące nazwy lokalne, helpery i importy pozostają dostępne. Utrzymano stare ID/kodowanie i 42 wcześniejsze wzorce; runtime 0.8 i etap 03 odrzucają wszystkie 15 nowych programów testowych, również martwe i skompresowane. Eksport opisu języka przechodzi na schemat 5 z udokumentowaną migracją zamkniętego katalogu typów (193 mutacje / 152 przypadki CLI); handbook: 86 builtinów / 152 przykłady. Pakietowy runtime działa bez wewnętrznych programów testowych. Algorytmy i publiczne konstruktory pozostają w etapach 05–09, eksport hosta w 10, pełna taryfa w 13. Potwierdzenie commit/push i SHA w raporcie dostarczenia zgodnie z §8.

### Etap 05 — Voronoi i rozmieszczenie sites

**Zależności:** 04.

- [x] 05.1 Zaimplementować bazowy diagram i własne sites, kanoniczne wspólne krawędzie, odczyty site/count/neighbors i przycinanie do obszaru.
- [x] 05.2 Dodać deterministyczne random/grid/jitter i Lloyd 0–4; zamrozić rozkład, kolejność, niepełne rzędy i ograniczoną obsługę kolizji generowanych sites.
- [x] 05.3 Dodać anizotropową metrykę i kierunek oraz row_bias/row_jitter/row_offset jako osobną fazę generowania sites.
- [x] 05.4 Sprawdzić count=1/256, błędne count, duplikaty, sites na brzegu i poza obszarem, przypadki współliniowe, jitter=0/1, relax=4, kierunki ukośne i neutralność rozszerzeń.
- [x] 05.5 Zweryfikować sumę pól w przypiętej tolerancji, brak luk i powielonych granic, symetrię sąsiedztwa oraz golden native/WASM.
- [x] **Etap 05 zakończony.**

**Odbiór:** wszystkie przeciążenia Voronoi działają, dane są stabilne i ograniczone, każda iteracja i pamięć tymczasowa mają koszt.

**Weryfikacja (2026-11-03):** [kontrakt Voronoi](spec/voronoi-v0.9.md), [wektory referencyjne](tests/fixtures/voronoi-v0.9.json), [raport](spec/verification/voronoi-v0.9.json), [przykład sites i sąsiedztwa](examples/language/voronoi.ips). Wszystkie sześć przeciążeń i trzy odczyty działają przez parser, typowanie, lowering, emitter, walidator, VM, formatter, IPA/IPDIS i opis języka. Świeże buildy Release poza iCloud: 78/78 CTest; siedem testów Voronoi / 2 278 223 asercje; pełny native/WASM PASS. Bramka obejmuje 19 zestawów, 306 generacji, bity całej topologii/metadanych, fuel i pamięć, 18 trapów, trzy odrzucenia bytecode oraz pięć błędów kompilacji. Niezależny oracle przekształca cały prostokąt do metryki euklidesowej; nie używa wyników runtime jako wzorca geometrii. Zachowano osobno analityczne poligony i utrwalone hashe referencyjnie sprawdzonych rekordów. Sprawdzone limity 1/256 także dla jawnej listy sites, współliniowość i współokręgowość, duplikaty/brzeg, niepełne rzędy, ukośne kierunki, neutralność i Lloyd 0–4. Krawędzie wewnętrzne mają dwa przeciwne użycia, zewnętrzne pokrywają każdy bok prostokąta dokładnie raz; zaokrąglone obwody nie mogą się samoprzecinać. Suma pól mieści się w tolerancji względnej 2e-6. Wspólna arena obejmuje scratch i wszystkie iteracje; 952 wymuszone błędy alokacji nie pozostawiają wycieków, count/site nie alokują. Metadane liczą momenty względem pierwszego wierzchołka, co stabilizuje duże współrzędne i zachowuje wcześniejsze wzorce. Count=256 z czterema iteracjami wymaga w sprawdzonym przypadku około 7,4M fuel; jawne zwiększenie meta `fuel` w IPA działa, domyślne 5M zachowuje trap. Nie dodano biblioteki geometrycznej. Runtime 0.8, etap 03 i etap 04 odrzucają po 21 nowych programów, także martwe opcode. Wcześniejsze wzorce pozostają niezmienione. Schema języka pozostaje 5 z opcjonalnym opisem Voronoi (200 mutacji / 153 przykłady CLI); handbook: 90 builtinów / 153 przykłady. Wariant pakietowy przechodzi bez wewnętrznych programów testowych. Kontury, regiony i dynamiczne części pozostają w kolejnych etapach.

**Publikacja (2026-11-03):** implementacja i testy w commicie [55731b4](https://github.com/jCzachor6/infinite-props/commit/55731b45b04f22ef0fe00bdb6336e8c06f46b48a). `git push origin develop` zakończył się powodzeniem; `git ls-remote origin refs/heads/develop` potwierdził pełny SHA `55731b45b04f22ef0fe00bdb6336e8c06f46b48a`, zgodny z lokalnym. Wcześniejszy błąd DNS ustąpił; publikacja przebiegła bez eskalacji. Ten wpis i zamknięcie checkboxa zapisuje osobny commit dokumentacyjny, zgodnie z §8.

### Etap 06 — kontury, dziury i upraszczanie

**Zależności:** 04; wykonywany po 05.

- [x] 06.1 Przypiąć tabelę Marching Squares, interpolację, reguły progów/degeneracji i domknięcie rastra dla connectivity four/eight.
- [x] 06.2 Zaimplementować ekstrakcję, orientację, zagnieżdżenia, dziury i stabilne porządkowanie w układzie źródłowej maski.
- [x] 06.3 Dodać simplify z ochroną topologii oraz min_area dla całych składowych po odjęciu dziur, z ograniczeniem kosztu także odrzuconych danych.
- [x] 06.4 Sprawdzić pustą/pełną maskę, pierścień, wyspę w dziurze, składowe rozłączne, diagonalne styki, cienkie mosty, brzeg maski i threshold=0/0.5/1.
- [x] **Etap 06 zakończony.**

**Odbiór:** zamknięta stabilna geometria bez zgubionych dziur, samoprzecięć i nieograniczonych danych pośrednich; zgodność native/WASM.

**06.1 (2026-11-03):** [kontrakt konturów](spec/contours-v0.9.md) przypina 16 przypadków, oba siodła, interpolację alfa i regułę degeneracji, domknięcie dokładnie na prostokącie maski, mapowanie skali/D4, kolejność oraz ochronę topologii. [Zestaw analityczny](tests/fixtures/contours-v0.9.json) zawiera niezależną tabelę i oczekiwane składowe/dziury. Dokumentacyjny commit nie zamykał implementacji; jej potwierdzenie znajduje się poniżej. Kontrakt opublikowany jako `e16e400863b3e463fbe53656b9e388f5d04bf1de`; zdalny SHA potwierdzony.

**06.2–06.3 (2026-11-03):** Dwa przeciążenia `contour` działają przez parser, semantykę, lowering, emitter, VM, walidator i formatowanie; schema 5 ma opcjonalny opis konturów. Ekstrakcja zachowuje położenie/skale/D4 źródła, zagnieżdżenia, dziury, kanoniczne początki, sortowanie i pochodzenie. Uproszczenie mierzy błąd wobec całych pierwotnych łuków i sprawdza przecięcia, orientację oraz zawieranie innych obwodów. Filtr porównuje pole netto całej składowej i niezależnie zachowuje wyspy. 88 zestawów sprawdzono niezależnym oracle geometrii i flood-fill rastra; pełne rekordy native/WASM (łącznie z momentami, fuel i pamięcią) są identyczne. Testy rdzenia: 117 187 asercji / dziewięć przypadków, w tym wszystkie 512 masek 3×3 przy obu connectivity i trzech progach. 246 wymuszonych błędów alokacji bez wycieków; limity 256/2048/65536 obejmują dane sprzed simplify/min_area. Pełna wcześniejsza bramka native/WASM przechodzi. Wyniki końcowe 06.4 podano poniżej.

**06.4 (2026-11-03):** [Raport końcowy](spec/verification/contours-v0.9.json) i [80/80 CTest](spec/verification/logs-v0.9.tar.gz) (`contours-v0.9-ctest.txt`). Dziewięć testów konturów / 117 201 asercji, 3072 ekstrakcje wszystkich masek 3×3, 88 utrwalonych zestawów i osiem dodatkowych wariantów zagnieżdżenia/simplify/D4. Cała geometria i metadane, fuel oraz pamięć są bitowo identyczne w native/WASM. Publiczna bramka obejmuje 706 generacji, dziewięć trapów, sześć odrzuceń bytecode i sześć błędów kompilacji. Sprawdzone dokładne granice fuel i wspólnej areny oraz 246 błędów alokacji; brak wycieków i częściowych wyników. Wszystkie cztery starsze runtime (0.8, etapy 03/04/05) odrzucają po sześć programów z nowymi instrukcjami; 54 wcześniejsze wzorce pozostają bez zmian. Czytniki/protokoły: 205 mutacji / 154 przykłady CLI; handbook: 91 builtinów / 154 przykłady. Osobny wariant pakietowy bez programów testowych przechodzi te same 706 generacji i dziewięć trapów. Nie dodano zależności geometrycznej. Końcowa kalibracja taryfy i niezależnego limitu pracy pozostaje w etapie 13.

**Publikacja 06.2–06.3 (2026-11-03):** [9659c78](https://github.com/jCzachor6/infinite-props/commit/9659c7880bbf09630efda80d34f1a5ee595fca8c). Push na `origin/develop` oraz zdalny SHA `9659c7880bbf09630efda80d34f1a5ee595fca8c` potwierdzone. Kolejny commit zapisuje końcowe próby 06.4, raport i zamknięcie checklisty, zgodnie z §8.

### Etap 07 — regiony, offsetowanie i maski

**Zależności:** 05, 06.

- [x] 07.1 Zbudować regiony z diagramów i konturów oraz count/id/area/center/neighbors według ustalonej semantyki pochodzenia i fragmentów.
- [x] 07.2 Zaimplementować jeden offset z round joins, dodatnim/ujemnym inset, rozpadem składowych, min_area i stabilną kolejnością.
- [x] 07.3 Dodać maskę pojedynczego regionu i sumy regionów z dziurami; współdzielić rasteryzator i ustalić regułę przypisania wspólnych granic.
- [x] 07.4 Dodać ograniczone `regions.points`; przetestować odrzucenie dziur i >256 wierzchołków bez utraty danych w innych API.
- [x] 07.5 Sprawdzić zanik/podział regionu, powiększanie i nakładanie, usuniętych sąsiadów, puste zbiory i maski, skalę/origin, clip i paint.
- [x] **Etap 07 zakończony.**

**Odbiór:** poprawne ID i indeksy po filtracji/podziale, dokładne dziury i spójne granice, brak przebudowy całej geometrii przy odczytach.

**07.1 (2026-11-03):** Dwa krótkie przeciążenia `regions` współdzielą gotowy store bez alokacji geometrii. Count/id/area/center korzystają z cache; neighbors zwraca zwykłą listę. Sprawdzone zachowanie dziur, ID, filtrowanie pola netto, ponowne przypisanie wyspy po usunięciu rodzica i indeksy zachowanych sąsiadów. Trzy testy / 2483 asercje, round-trip IPS/IPA i named calls, niezainicjalizowane uchwyty oraz błędne indeksy także z optymalizacją; własny helper `regions` zachowuje pierwszeństwo. Przechodzi wcześniejsza pełna bramka native/WASM (w tym 88 zestawów konturów) i cztery ponownie wykonane bramki smoke/eksport/dokumentacja/CI. [Kontrakt](spec/regions-v0.9.md) przypina semantykę pochodzenia i porządek fragmentów; ich tworzenie, maski i points pozostają w kolejnych podpunktach etapu 07. Nie dodano opcode konfiguracji przed ich implementacją.

**Publikacja 07.1 (2026-11-03):** [23ea669](https://github.com/jCzachor6/infinite-props/commit/23ea669e4e614d68c4d1ce1e3fc8d1df19b8b38b). Push na `origin/develop` i zgodny zdalny SHA potwierdzone.

**07.2–07.4 (2026-11-03):** Jeden ograniczony kernel offsetu z dodatnim/ujemnym inset i round joins (tolerancja łuku 1/64 jednostki przed zaokrągleniem f32), dodatnim winding i walidacją przecięć. Działa zanik, podział, pole netto po odjęciu dziur, min_area przed limitem 256 wynikowych regionów, stabilne ID/kolejność oraz indeksy sąsiadów z pochodzenia krawędzi. Jedna rasteryzacja scanline z siatką 4×4 obsługuje pojedynczy region i sumę przed kwantyzacją A8, bez wypalania clip i bez ograniczenia geometrii do targetu. `regions.points` zachowuje pełne dane w pozostałych API i jawnie odrzuca dziury oraz >256 punktów. Odczyty metadanych korzystają z cache. Nie dodano zależności geometrycznej.

**Weryfikacja 07.2–07.4:** 82/82 CTest, 10 testów regionów / 43 040 asercji, 1536 offsetów wszystkich masek binarnych 3×3 i 510 wymuszonych błędów alokacji bez wycieków. 55 zestawów geometrii, metadanych, fuel i pamięci jest bitowo identycznych w native/WASM; niezależna referencja sprawdza pola/momenty, przecięcia, odległości offsetu i każdą próbkę maski. Publiczna bramka: 564 generacje, 41 trapów, 36 odrzuceń bytecode, siedem błędów kompilacji; te same 564 generacje przechodzą w wariancie pakietowym bez programów testowych. Pełna wcześniejsza regresja native/WASM przechodzi. Eksport języka: schema 5 z opcjonalnym opisem regionów, 210 mutacji / 155 przykładów CLI; handbook: 99 builtinów / 155 przykładów. Końcowy raport i zamknięcie 07.5 zostaną zapisane w kolejnym commicie, zgodnie z §8.

**07.5 (2026-11-03):** [Raport końcowy](spec/verification/regions-v0.9.json) i [82/82 CTest](spec/verification/logs-v0.9.tar.gz) (`regions-v0.9-ctest.txt`). Pełna macierz native/WASM przechodzi; 55 zestawów porównuje wszystkie punkty/sites, krawędzie/pochodzenie, obwody/dziury, metadane oraz fuel/pamięć. Publiczne API: 564 generacje, 41 trapów, 36 odrzuceń bytecode i siedem błędów kompilacji. Wariant pakietowy bez wewnętrznych programów testowych przechodzi te same 564 generacje i 41 trapów. Potwierdzone granice fuel/areny oraz 510 wymuszonych błędów alokacji bez wycieków. Limit wynikowych 256 regionów działa po min_area (257 fragmentów może zostać odfiltrowanych); limity obwodu i danych pośrednich obejmują także odrzucane wyniki. Skala niejednorodna, origin ujemny/ułamkowy, geometria poza targetem, clip, oba tryby paint, pusty zbiór i maska, zanik/podział/nakładanie oraz usunięci sąsiedzi mają testy. Pięć starszych runtime (0.8, etapy 03–06) odrzuca po 20 nowych programów; 57 wcześniejszych wzorców/referencji pozostaje niezmienionych. Przykład IPS skompilowany, wygenerowany i obejrzany; nie wykonywano release ani wdrożenia.

**Publikacja 07.2–07.4 (2026-11-03):** [cf0ab55](https://github.com/jCzachor6/infinite-props/commit/cf0ab55ac8cfb4f379a7724803b2c1fa50da42d6). Push na `origin/develop` i zgodny zdalny SHA potwierdzone. Bieżący commit zapisuje raport, log i zamknięcie etapu; jego SHA podaje raport dostarczenia, zgodnie z §8.

### Etap 08 — fugi i bevel

**Zależności:** 07.

- [x] 08.1 Oprzeć stałą fugę width/2 na wspólnym offsetowaniu; uwzględnić brzeg zewnętrzny, dziury i powtarzane zastosowanie.
- [x] 08.2 Przypiąć i wdrożyć deterministyczny profil zmiennej fugi ze współdzielonym odczytem po obu stronach krawędzi oraz ograniczoną liczbą segmentów.
- [x] 08.3 Przypiąć profil bevel, normalne, narożniki, remisy krawędzi i alfa; wygenerować warstwę bez zmiany geometrii oraz bez nowego systemu oświetlenia.
- [x] 08.4 Sprawdzić width=0 dla gap, zanik cienkich regionów, variation=0/1, deterministyczność seedów, zewnętrzne/wewnętrzne krawędzie i nakładanie warstwy source-over/legacy.
- [x] **Etap 08 zakończony.**

**Odbiór:** stały gap odpowiada temu samemu offsetowi co inset, zmienne granice mają wspólny profil, bevel daje powtarzalny efekt w scenach muru/płytek.

**Weryfikacja 08.1:** krótki `regions.gap` (opcode `FC`, commit `5f138f7`) używa bezpośrednio offsetu width/2; pełny frontend, walidator, runtime, formatter, IPA/IPDIS i opis języka. 2 testy / 2827 asercji: współdzielenie width=0, zgodność punktów/krawędzi/metadanych i fuel, dziury, zanik, powtarzane zastosowanie, błędy i optymalizacja. Dodatkowo 40 generacji publicznego API native/WASM porównuje gap z inset, PNG, metadane i RNG. 82 kontrole CTest zaliczone po uzupełnieniu dokumentacji i przykładu; pełny dotychczasowy gate native/WASM zaliczony.

**Weryfikacja 08.2:** profil 9 węzłów oparty na seedzie i pierwotnych indeksach krawędzi/wierzchołków (commit `d36b072`); kanoniczna parametryzacja niezależna od orientacji, pochodzenie zachowane przy powtarzaniu. Wspólne przecięcia, positive winding, podział i publikacja z offsetu, z istniejącymi limitami pośrednimi. Kontrakt, hashe/salty, round joins i taryfa pośrednia przypięte w [specyfikacji technicznej](spec/gap-bevel-v0.9.md). 4 testy / 37062 asercje, w tym 1536 zmiennych gapów na wszystkich binarnych maskach 3×3 i kontrola obu stron wspólnej krawędzi. 49 pełnych zapisów geometrii i 120 generacji publicznego API zgodnych native/WASM; variation=0 identyczne ze stałym gapem. 82 kontrole CTest zaliczone po aktualizacji generatora testów efektów i dawniej zastrzeżonego opcode.

**Weryfikacja 08.3:** bevel (commit `6c1292d`) używa wspólnego scanline i siatki 4×4 regionów. Przypięto dystans, normalne ku zewnętrzu wypełnienia, remisy, narożniki, liniowy zanik, wybór nakładających się regionów i kwantyzację alfa/koloru. Osobna RGBA8, bez mutacji geometrii, rysowania do rodzica ani nowych zasad oświetlenia. Odczyt krawędzi ograniczony pasem i bboxami, bufory właścicieli/próbkowania tylko na jeden wiersz. 5 testów / 37286 asercji; 48 pełnych zapisów warstw native/WASM zgodnych z niezależnym naiwnym rendererem per próbka, dodatkowo 48 generacji publicznego API (domyślne/jawne kolory, strength=0/0.5/1, pusty clip przy tworzeniu, source-over/legacy). 82 kontrole CTest zaliczone po przeniesieniu testów nieznanego opcode z zajętych `FE/FF` na zastrzeżone bajty sterujące. Przykład płytek 128×96 obejrzany, 3269529/5000000 fuel. Końcową weryfikację dokumentuje 08.4 poniżej.

**Weryfikacja 08.4:** [47 pełnych wektorów](tests/fixtures/gap-bevel-v0.9.json), 2 przypięte i obejrzane sceny płytek/muru, 8 testów / 37465 asercji, 1046 wymuszonych awarii alokacji bez wycieków (commit `7fa4ab4`). Gate: 485 generacji native/WASM, 51 trapów, 87 odrzuceń bytecode, 7 błędów kompilatora, dokładne granice fuel/quota w obu rdzeniach. Kontroluje również profile z niezależnym hashem BigInt i próbkowaniem łuków, maski i bevel z naiwną referencją per próbka, wszystkie 8 konfiguracji kompilatora, IPA/IPDIS, formatter, kolejność argumentów i RNG, dziury, rozpad i powtarzanie. Końcowe **84/84 CTest** zaliczone; cały gate dotychczasowych etapów native/WASM zaliczony. Paczka bez sond diagnostycznych: te same 485 generacji publicznego API; 6 starszych runtime'ów odrzuca 12 nowych programów. [Raport zamknięcia](spec/verification/gap-bevel-v0.9.json), [log CTest](spec/verification/logs-v0.9.tar.gz) (`gap-bevel-v0.9-ctest.txt`). Etap 09 pozostaje otwarty.

### Etap 09 — dynamiczne części i D4

**Zależności:** 07, 08.

- [x] 09.1 Dodać tworzenie z regionu/A8/RGBA i zbiorcze tworzenie, count/get, globalny licznik ID i limit łącznych utworzeń bez resetowania po zwolnieniu.
- [x] 09.2 Dodać niemutowalne fill/texture/place i metadane; współdzielić bufory, istniejący D4 i źródła geometrii.
- [x] 09.3 Zaimplementować zaokrąglenie położenia, bounds, center, draw i mask z clippingiem i aktywnym compositingiem; zachować alfę źródła warstwowego dokładnie raz.
- [x] 09.4 Sprawdzić wszystkie osiem symetrii D4, pozycje ułamkowe/ujemne, różne kotwice, dziury, miękką alfę, puste źródło i brak wyglądu.
- [x] 09.5 Potwierdzić stabilne stare widoki, brak nowych ID przy get/place/fill/texture, dwa utworzenia tego samego źródła i limit 256 po porzucaniu uchwytów.
- [x] **Etap 09 zakończony.**

**Odbiór:** niezależne części są instancjami runtime; brak kopiowania całego canvasa na każdy obiekt oraz regresji statycznych importów.

**09.1 (2026-11-03):** [Kontrakt instancji](spec/dynamic-parts-v0.9.md), [przykład tworzenia](examples/language/dynamic-parts.ips). Osiem nowych instrukcji, sześć builtinów; pełna ścieżka kompilatora, walidatora, VM, formattera, IPA/IPDIS i opisu języka. Sprawdzone współdzielenie regionów i rastrów, dziury/source ID, puste rastry, indeksy, helpery, RNG, zwalnianie widoków i limit 256 także po ich porzucaniu. Świeży native Release: **84/84 CTest**, ukierunkowane testy tworzenia/builtinów/efektów: 26 przypadków / 11938 asercji. Publiczny smoke native/WASM zwraca identyczne ID i pusty obraz bez automatycznego renderu. Wygląd i render części pozostają otwarte w 09.2–09.5.

**09.2 (2026-11-03):** 09.1 opublikowane jako `c2a718a`. Dodane `fill/texture/place`, center i bounds; źródła i ID są współdzielone, ostatni wygląd zastępuje poprzedni, a miejsce jest absolutne wobec oryginału. Cache oryginalnej kotwicy w pikselach zachowuje dokładne połówki przy skali 3; brak ponownych skanów przy odczycie metadanych. Kontrakt przypina binary64, zaokrąglenie i składanie istniejącego D4. Sprawdzone 16 zestawów parametrów symetrii, 64 pary D4 źródła/części, pozycje ujemne/ułamkowe, powtórne place, niedozwolone parametry, wymagana zamiana nierównych skal, trwałość dawnych widoków i brak nowych ID. **84/84 CTest**, testy ukierunkowane: 29 przypadków / 12453 asercje; publiczny przykład native/WASM ma identyczne ID i metadane. Handbook: 113 builtinów / 158 przykładów. Render/mask pozostają w 09.3.

**09.3 (2026-11-03):** 09.2 opublikowane jako `d00b8ba`. Render i mask używają wspólnego rasteryzatora regionów, D4, paintSurface i kompozytora. Domyślne RGBA nakłada alfę raz; fill/texture mnożą wygląd przez pierwotne krycie. Brak wyglądu daje jawny trap przy draw, ale mask jest poprawna. Maski nie przejmują clipu ani wyglądu; istniejące A8 są współdzielone, alfa RGBA jest materializowana tylko w dodatnim bboxie. Poprawiona kontekstowa składnia `part.mask` oraz rozpoznawanie statycznych wywołań `pot.draw`/`part.draw(seed,at)` obok dynamicznego `part.draw(p)`. **84/84 CTest**, testy ukierunkowane: 32 przypadki / 12804 asercje, pełne RGBA i metadane przykładu native/WASM identyczne. Schema 5 z opcjonalną polityką części; 115 builtinów / 158 przykładów. Macierz referencyjna, granice zasobów i audyt zamknięcia pozostają w 09.4–09.5.

**09.4 (2026-11-03):** 09.3 opublikowane jako `9f0c3cb`. [275 wektorów](tests/fixtures/dynamic-parts-v0.9.json) i [niezależny oracle](tests/reference/dynamic-parts-reference.js) sprawdzają pełne RGBA/A8, mapping, bity metadanych oraz oryginalnej ramki; koszty fuel/pamięci są jawnie utrwalonymi pinami implementacji. Wszystkie 64 pary D4 dla każdego źródła rastrowego, analityczna dziura i trójkąt, ujemne/ułamkowe położenia, różne kotwice, skale 2×3 i 0.75×1.25, oba kompozytory, miękkie/puste clipy i wszystkie wyglądy. Gate: 81 wariantów publicznych, **1056 generacji native/WASM**, 33 trapy, 56 odrzuceń nieużywanego bytecode, 9 diagnostyk; wszystkie 8 konfiguracji kompilatora, IPA/IPDIS i formatter. **85/85 CTest**, cały wcześniejszy gate native/WASM przechodzi. [Przykład dynamicznych kafli](examples/language/dynamic-tiles.ips) obejrzany, oba przykłady mają przypięte RGBA/fuel i zgodność WASM; 115 builtinów / 159 przykładów. Granice zasobów i końcowy audyt to 09.5.

**09.5 (2026-11-03):** 09.4 opublikowane jako `cb9c96b`. Doprecyzowany i wykonany podział skal: geometria dobiera odwrotną siatkę D4 targetu bez kopiowania wierzchołków; rastry i tekstury nie są resamplowane. 42 dodatkowe przypadki geometrii i 6 odrzuceń niedopasowanych rastrów, bez zmiany wcześniejszych 275 wyników. Końcowy gate: **317 wektorów, 634 pełne rekordy rdzeni, 1063 generacje native/WASM, 37 trapów, 56 odrzuceń bytecode i 10 diagnostyk**. **86/86 CTest**, 13 testów części / 1687 asercji; 132 wymuszone awarie alokacji w 12 ścieżkach bez wycieków, atomowe ID przy błędzie bulk. Potwierdzone stare widoki i bufory po zamknięciu wykonania, ID 0–255 także po porzucaniu uchwytów, 512 powtórzeń operacji bez nowych ID oraz 24 efektowne argumenty w kolejności sygnatury. Granice fuel i pojedynczego bajtu areny są zgodne native/WASM. Paczka bez sond przechodzi 1063 generacje publiczne; checked C API/Python: 4 generacje z dokładnym RGBA, punktami i fuel. Siedem starszych runtime odrzuca po 48 nowych programów; 67 dawnych wzorców bez zmian. Kontrakty eksportu: 221 mutacji / 159 przypadków CLI; 115 builtinów / 159 przykładów. Zbiorczy raport i audyt dostarczenia zostaną utrwalone w commicie zamknięcia etapu.

**Zamknięcie etapu 09:** implementacja 09.5 opublikowana jako `5576a2a`. [Raport weryfikacji](spec/verification/dynamic-parts-v0.9.json) i [pełny log 86/86 CTest](spec/verification/logs-v0.9.tar.gz) (`dynamic-parts-v0.9-ctest.txt`) utrwalają zgodność native/WASM, granice zasobów, paczkę bez sond, checked C API/Python, stare runtime i hashe źródeł/binarek. Dokumentacja sprawdzona względem źródeł na czystym `5576a2a` (115 builtinów / 159 przykładów); audyt dostarczenia powtarza build/verify na czystym commicie zamknięcia i potwierdza SHA po pushu. Opcjonalny silnik Python `jsonschema` nie był dostępny; walidatory kontraktów oraz 221 mutacji / 159 rzeczywistych przypadków CLI przeszły. Eksport typowanych części i rozszerzenia bindingów pozostają nieodznaczonymi etapami 10–11.

### Etap 10 — eksport części i API hosta

**Zależności:** 09.

- [x] 10.1 Dodać deklarację `output list part`, kodowanie programu, append snapshotu, walidację i globalny limit 256 eksportowanych rekordów.
- [x] 10.2 Rozszerzyć wynik C++ i checked C API o typowane części oraz dostęp do geometrii, dziur, A8/RGBA i współdzielonych zasobów, bez zmiany layoutu starego ABI.
- [x] 10.3 Określić i przetestować ownership, pożyczone widoki, kopie, puste sloty, błędne indeksy/pojemności oraz jawne odrzucanie nieobsługiwanych nowych wyjść przez starsze ścieżki.
- [x] 10.4 Sprawdzić render-only/export-only, brak wyglądu, wiele snapshotów jednego ID, niezależną kolejność eksportu, sloty statycznych importów i zachowanie wyniku po zwolnieniu VM/programu.
- [x] 10.5 Sprawdzić compile/load/generate rozszerzonego `.iprop`, zgodność starych fixtures i bezpieczne odrzucanie nowych rozszerzeń przez stary czytnik; nie dodawać serializacji wyniku.
- [x] **Etap 10 zakończony.**

**Odbiór:** host otrzymuje rzeczywiste niezależne części, bez utraty dziur/alfy/transformacji, bez ukrytej kopii zasobów i bez zależności od życia VM.

**10.1 (2026-11-03):** [Kontrakt](spec/part-exports-v0.9.md): kind 3 i opcode `0x69`, bez zmiany starych ID ani nagłówka. Wspólne ownership z istniejącą sekcją 10, append w root coordinates, współdzielone tabele/zasoby i limit 256 niezależny od ID instancji. Parser/typowanie/emitter/formatter/IPA/IPDIS/opis języka objęte zmianą. 26 testów części/eksportów/zgodności, **544350 asercji**; kontrakty **221 mutacji / 159 przypadków CLI**. Nowy program przechodzi compile/load/validate, runtime z etapu 09 odrzuca nowy kind; legacy JSON odrzuca go przed zapisem obrazu. C API i binding WASM do czasu ich rozszerzenia jawnie odrzucają ten wynik. 10.2–10.5 pozostają otwarte.

**10.2 (2026-11-04):** 10.1 opublikowane jako `95726f5`. Wynik C++ współdzieli snapshoty; checked C API 0.5 udostępnia nowe sized descriptors dla slotów, części, zasobów, regionów i obwodów, jawne dziury oraz dokładne A8/RGBA. Wspólne bufory mają jeden result-local ID, query nie alokuje ani nie materializuje kształtu. Stare layouty/enum i ABI 0.5 zachowane. **11/11 sprawdzeń CTest**: klient C99 shared/static z pełnym eksportem, miękką alfą, kopiami, pustym slotem i lifetime; dawne wyjścia, statystyki, błędy i liczenie alokacji bez regresji. Kontrakt hosta: [C API parts](docs/c-api-part-outputs.md). 10.3–10.5 pozostają otwarte.

**10.3 (2026-11-04):** 10.2 opublikowane jako `c4cd41a`. **1848 sprawdzeń** kompletnych/prefiksowych/nieosiowanych deskryptorów, canary/tail, błędnych slotów/indeksów/rodzajów/pojemności i aliasowania; query z wymuszoną odmową każdej alokacji: **zero alokacji**. 256 rekordów przechowuje 512 odrębnych zasobów mimo identycznej zawartości; współdzielone RGBA nadal ma jeden ID, puste sloty są obecne. Bezpośrednia równość pointera rastra, weak refs po zwolnieniu ostatniego resultu i host copies potwierdzają ownership. **82 wymuszone awarie** jednorazowe/trwałe w append/grow/transfer/pack/generate oraz **4 dokładne granice areny**: brak wycieków, zachowane wcześniejsze snapshoty i niezmieniony uchwyt wyjściowy przy błędzie. Nowe testy C API **4/4**; 10.4–10.5 otwarte.

**10.4 (2026-11-04):** 10.3 opublikowane jako `9d0b1ce`. **7 testów / 288132 asercji**, w tym **4096 generacji** ze wszystkimi D4 dynamicznej części i dwóch poziomów importów, czterema skalami oraz opt/no-opt. Niezależne wzory na krawędzie i signed basis sprawdzają dokładne bity centroidu/bounds, source grid, D4 i całkowitą translację; także ujemne położenia, części poza targetem i zaokrąglone rozmiary canvasa importu. Render-only/export-only, powtórne draw bez dopisywania, kolejność snapshotów, pusty clip, brak wyglądu, kolejne wywołania bez zapisu oraz limit współdzielony z importem potwierdzone. Sonda testowa porównuje pełne C++/checked C API, a native/WASM mają identyczne rekordy, geometrię z dziurami, bufory i RGBA w scenach podstawowej, nested i order. Dla nested właściciele slotów `[0,2,3,3,4]` uwzględniają nieodwołany import bez wyjść; handoff wyniku nie zależy od życia programu. 10.5 otwarte.

**10.5 (2026-11-04):** 10.4 opublikowane jako `22c5f2b`. **91/91 CTest**, w głównym procesie 814 testów / 4515488 asercji. Pełne conformance: **604 pary native/WASM / 1208 pełnych obserwacji**, checked C API porównuje każde pole, źródła i ownership; **7 trapów, 30 mutacji bytecode, 8 diagnostyk, 28 IPA/IPDIS, 7 formatter round-trip**. Siedem przypiętych scen obejmuje niezerowy mapping źródła/tekstury, A8/RGBA, dziury, indeksy regionów 0–2 przy jednym zasobie i maksymalne 256 rekordów / 512 zasobów. **8 starszych runtime odrzuca po 17 nowych programów**, 72 wcześniejsze wzorce bez zmian. Cztery C99 clients z przeniesionego SDK przechodzą shared/static, także ze starym nagłówkiem API 0.4. Nowe checked generate wymaga jawnego `IPROP_GENERATE_PART_OUTPUTS`; bez flagi zwraca `E_UNSUPPORTED` przed VM/alokacją wyniku, również dla pustych i niewywołanych slotów. Prefixy 4/7 odrzucone, 8-bajtowy prefix z flagą obsługiwany. Zapobiega to fałszywemu pustemu obrazowi w starym checked kliencie. Aktualny gate ownership ma 1851 przypadków i zero alokacji query; 82 awarie i cztery granice areny przechodzą. Paczka bez sond weryfikuje compile/load i 57 jawnych odrzuceń legacy JS/JSON, a pełne typed obserwacje oznacza jako niewykonane. Dokumentacja 115 builtinów / 159 przykładów i kontrakty 231 mutacji / 159 CLI przechodzą; opcjonalny Python `jsonschema` niedostępny. Nie dodano serializacji wyniku. Zbiorczy raport i audyt czystego commitu zostaną utrwalone w commicie zamknięcia.

**Weryfikacja końcowa (2026-11-04):** kroki 10.1–10.5 opublikowano na `origin/develop`; checkpoint implementacji `1b0775bd7c0ae74fba12c0f1969326d9162add93` ma potwierdzone zdalne SHA i rozbieżność `0 0`. [Raport](spec/verification/part-exports-v0.9.json) oraz [log CTest](spec/verification/logs-v0.9.tar.gz) (`part-exports-v0.9-ctest.txt`) utrwalają wyniki, zakres i ograniczenia. Dokumentacja zbudowana i zweryfikowana na czystym checkpoincie: 115 builtinów / 159 przykładów. Commit zamknięcia obejmuje wyłącznie ten plik, raport i log; dokumentację należy ponownie sprawdzić na jego czystym SHA w audycie dostarczenia. Pełny eksport działa w C++/checked C API; publiczne bindingi są nadal etapem 11. Nie jest to wydanie ani publikacja strony.

### Etap 11 — bindingi i hosty

**Zależności:** 10.

- [x] 11.1 Udostępnić pełny wynik części w HTML5/WASM, Apple, Android, Unity, Godot, Python i SFML przez wspólny kontrakt z C API.
- [x] 11.2 Uzupełnić mosty typów, zarządzanie pamięcią i potrzebne wersje API/ABI bez łamania istniejących layoutów; nie dodawać osobnego formatu eksportu dla każdej platformy.
- [x] 11.3 Uruchomić conformance obejmujące rekordy, dziury, bufory, D4 i lifetime; porównać te same wyniki z referencją C++/C.
- [x] 11.4 Zapisać macierz platform: wykonane lokalnie/CI, sprawdzone tylko statycznie i niedostępne. Nie oznaczać brakującego testu platformy jako zaliczonego.
- [x] **Etap 11 zakończony.**

**Odbiór:** wszystkie utrzymywane bindingi implementują kontrakt; wymagane sprawdzenia platform przechodzą, a ograniczenia środowiska są jawne.

**Kroki integracji 11.1:**

- [x] 11.1a Python: pełne snapshoty, jedna kopia zasobu na ID, obwody/dziury, dokładne A8/RGBA, automatyczna negocjacja typed generation i niezależny lifetime.
- [x] 11.1b HTML5/WASM: publiczny binding korzysta ze wspólnej implementacji checked C API; pełne snapshoty/zasoby, stare valueBits, owner/index/count, błędy i niezależny lifetime.
- [x] 11.1c Apple: pełne własne rekordy i zasoby w Swift; zbudowany XCFramework macOS/iOS/symulator, rzeczywiste testy Swift/CGImage na macOS.
- [x] 11.1d Unity/C#: pełne rekordy i wspólna tabela zasobów w produkcyjnym wrapperze C#; runtime .NET z rzeczywistym C API na macOS.
- [x] 11.1e Android/JNI/Kotlin: pełny most typów/zasobów i lifetime; rzeczywiste wywołania JNI z JVM i CheckJNI. Test urządzenia/emulatora pozostaje osobną pozycją macierzy 11.4.
- [x] 11.1f Godot: pełne słowniki snapshotów i jedna tabela zasobów, niezależne Image/arrays, zachowanie wyników po generacji, przeładowaniu i zwolnieniu assetu.
- [x] 11.1g SFML: opcjonalny pełny owned `copyResult`, snapshoty i zasoby przez wspólne query C API; viewer może nadal czytać tylko obraz.

**11.1a (2026-11-04):** Python porównuje każde pole z 23 wspólnymi obserwacjami C++/checked C API. Siedem wcześniejszych semantic pins sprawdzono przed utrwaleniem referencji; dodatkowe sceny obejmują wszystkie dynamiczne i statyczne D4 oraz różne siatki targetu. Pełne dane, kolejność, ID, dziury, bufory i bity metadanych są zgodne także po kolejnej generacji i zwolnieniu programu. Siedem testów Python: sześć przechodzi, jeden opcjonalny test Pillow/CLI obrazka pominięty z powodu braku Pillow. CLI zachowuje dawny JSON point/rect/circle i jawnie odrzuca deklaracje part przed zapisem obrazu; nie dodano serializacji wyniku. Podpunkty 11.1–11.4 pozostają otwarte do integracji i weryfikacji wszystkich hostów.

**11.1b (2026-11-04):** 11.1a opublikowano jako `2e74eef`. Publiczny WASM przechodzi 23 wspólne sceny oraz **604 kompletne obserwacje** zgodne z C++/checked C API i sondą WASM (1812 pełnych obserwacji łącznie, 604 pary native/WASM). Każdy wynik sprawdzono ponownie po kolejnej generacji; eksport zachowuje pełną geometrię, dziury, A8/RGBA, mappingi i bity metadanych. Przechodzi 512 przypadków macierzy, 28 IPA/IPDIS, 7 formatter round-trip, 7 trapów i 30 mutacji bytecode. Legacy CLI JSON nadal odrzuca 57 nowych programów; publiczny binding obsługuje je, a 37 błędnych programów/generacji odrzuca z checked statusem. Siedem wcześniejszych pinów bez zmian. Nie dodano formatu zapisu wyniku.

**11.1c–e (2026-11-04):** 11.1b opublikowano jako `b6be993`. Swift, produkcyjny C# oraz Kotlin/JNI porównują wszystkie pola w 23 wspólnych scenach C++/C, także po kolejnej generacji i zwolnieniu programu. Każdy z tych hostów odtworzył **21 829** przypadków wspólnego korpusu (`e0736a354fe602e04097721b6131922819cece0e86033053e038c1b2dc099919`) bez różnicy RGBA/wyjść. Swift: siedem testów, sześć przechodzi, opcjonalny Metal pominięty z powodu braku urządzenia; CGImage przechodzi. XCFramework zbudowany dla macOS arm64/x86_64, iOS arm64 i symulatora arm64/x86_64. C# sprawdzony z .NET 8, JNI z Kotlin 2.2.10/JVM i CheckJNI; nie są to testy Unity Editor ani urządzenia Android. Python z Pillow przechodzi wszystkie siedem testów oraz ten sam korpus z kontrolami negatywnymi.

**11.2 — obsługa błędów WASM:** pełny wcześniejszy gate ujawnił uszkodzenie granicy po wielu błędnych generacjach. C++ zwraca teraz błąd przez prywatny most i kończy sprzątanie przed rzuceniem JS Error; publiczne `Module.generate` i `Iprop.generate` nadal rzucają wyjątki. Sprawdzono 3000 kolejnych błędów i callback inicjalizacji; stały gate obejmuje 2048 błędów i poprawny wynik po nich. Cały gate native/WASM przechodzi, w tym 604 pełne publiczne porównania części. Wspólny korpus uwzględnia kind/count części z zerem liści float; pełne dane części weryfikuje osobny wspólny gate 23 scen, bez nowego formatu produktu.

**11.1f–g (2026-11-04):** 11.1c–e opublikowano jako `4e93503`. Rzeczywisty GDExtension Godot 4.5.2 i produkcyjny adapter SFML przechodzą wszystkie 23 wspólne pełne obserwacje C++/C, łącznie z ownership, dziurami, dokładnymi buforami i D4. Godot zachowuje poprzednie kontenery przez zastąpienie ich przy reloadzie, a błędy C++ zatrzymuje na granicy silnika. SDK otrzymuje kompletne źródła helperów, referencji i adaptera SFML. Skorygowano ścieżki wyjściowe testów Godot, aby działały w wskazanym katalogu build, oraz zapis kilku instrukcji odrzucanych przez GCC `-Werror=misleading-indentation`, bez zmiany ich działania.

**11.2–11.3 (2026-11-04):** każdy host przechodzi 23 pełne wspólne obserwacje i zachowuje własne dane po zwolnieniu wyniku/programu oraz kolejnej generacji. Nagłówek C API ma ten sam kontrakt tokenów co etap 10; zmieniły się wyłącznie komentarze. Nie zmieniono layoutów ani nie dodano formatu zapisu wyniku. Przeniesione SDK przechodzi siedem testów Python z Pillow i 23 scenami oraz dwóch klientów C++ shared/static korzystających z zainstalowanego adaptera SFML. Publiczna przeniesiona paczka WASM bez sond przechodzi 604 pełne obserwacje, siedem pinów i 2048 kolejnych błędów zakończonych poprawną generacją. Pełny wcześniejszy gate zawiera 604 pary native/WASM i 1812 obserwacji z publicznym JS. Wszystkie hosty odtwarzają wspólny korpus 21 829 przypadków; kind/count części jest w korpusie, a pełne dane części w osobnym gate 23 scen.

**11.4 — macierz wykonania (2026-11-04):**

| Host | Wykonano lokalnie | Kompilacja bez wykonania / niedostępne |
| --- | --- | --- |
| HTML5/WASM | Publiczny JS w Node, pełne dane, lifetime, błędy, 21 829 przypadków | Przeglądarka i jej GPU niesprawdzane w tym etapie |
| Python | Produkcyjny ctypes/Pillow, 7/7 testów, 23 pełne sceny, 21 829 przypadków, przeniesione SDK | Brak pominięć w tym zakresie |
| Apple | Swift i CGImage na macOS arm64: 6 testów zaliczonych, 1 pominięty; 23 sceny i 21 829 przypadków | Metal niedostępny; XCFramework macOS/iOS/symulator zbudowany, Swift iOS device/simulator typechecked; iOS i x86_64 runtime niewykonane |
| Unity/C# | Produkcyjne źródła wrappera pod .NET 8, rzeczywiste C API, 23 sceny i 21 829 przypadków | Unity Editor i tekstury silnika niewykonane |
| Android | Produkcyjny Kotlin/JNI pod JVM z CheckJNI, 23 sceny i 21 829 przypadków | NDK niekompletny, brak uruchomienia Android ABI/urządzenia/emulatora; test instrumentacyjny podłączony do wspólnych źródeł |
| Godot | Rzeczywisty GDExtension, 23 pełne sceny na macOS i Linuksie; Image/tekstury: 21 829 przypadków pod Linux/Xvfb/Mesa | macOS windowed GL niedostępny; Linux używa software OpenGL |
| SFML | Produkcyjny adapter, 23 pełne sceny; upload/readback/BlendNone render: 21 829 przypadków pod Linux/Xvfb/Mesa | macOS desktop GL niedostępny; Linux używa software OpenGL |

**Weryfikacja końcowa:** **97/97 CTest na Linuksie**, bez wyłączeń, oraz **95/95 na macOS**, z dwoma testami GL sprawdzonymi na Linuksie. Dla Windows i CI tego checkpointu nie utrwalono wyników wykonania; nie są zaliczone jako testy platform. Linux to lokalny izolowany Docker Compose, nie job GitHub Actions. Wzorzec źródeł potwierdza zgodność wszystkich plików runtime/bindingów/testów z checkpointem `f845598c563042fdfe1b4f3ac2644f0299e90377`; różnice kopii dotyczą jedynie trzech plików dokumentacji. Checkpoint jest opublikowany na `origin/develop`, a dokumentację zbudowano i zweryfikowano na jego czystym SHA: 115 builtinów / 159 przykładów.

[Raport etapu 11](spec/verification/part-bindings-v0.9.json), [logi hostów i SDK](spec/verification/logs-v0.9.tar.gz) (`part-bindings-v0.9-hosts.txt`), [CTest Linux](spec/verification/logs-v0.9.tar.gz) (`part-bindings-v0.9-linux-ctest.txt`), [CTest macOS](spec/verification/logs-v0.9.tar.gz) (`part-bindings-v0.9-macos-ctest.txt`) i [pełne conformance](spec/verification/logs-v0.9.tar.gz) (`part-bindings-v0.9-conformance.txt`) zapisują zakres, SHA korpusu i ograniczenia. Commit zamknięcia obejmuje ten plik oraz raport i logi; jego czyste SHA wymaga ponownego build/verify dokumentacji i porównania z rzeczywistym zdalnym SHA w audycie dostarczenia. Nie jest to wydanie 0.9 ani publikacja strony.

**Audyt odbioru (2026-11-04):** publiczny test WASM uruchamia teraz bezpośrednio te same 23 zamrożone sceny C++/C co pozostałe bindingi, porównując pełny wynik oraz jego zachowanie po kolejnej generacji. Przygotowanie korzysta z istniejącego wspólnego helpera Python; nie powstał nowy zestaw referencji ani format produktu. Przeniesiona publiczna paczka bez sond przechodzi ten przebieg razem z dotychczasowymi 604 obserwacjami i 2048 błędami. Ponownie uruchomiony pełny gate używa świeżych narzędzi native etapu 11 i przechodzi 604 pary native/WASM, 1812 pełnych obserwacji oraz 23 wspólne sceny. Korpus pikseli pozostaje wcześniejszym przypiętym zestawem 21 829 przypadków; runtime i bindingi nie zmieniły się podczas tego audytu.

### Etap 12 — niezależne limity pracy i przerywanie

**Zależności:** 11; przed jakimikolwiek szerokimi obniżkami taryfy.

- [x] 12.1 Wprowadzić wspólny deterministyczny limit pracy natywnej i objąć nim także dotychczasowe operacje przewidziane do rabatowania, z zachowaniem domyślnego fuel.
- [x] 12.2a Zaimplementować deadline, atomowe anulowanie i punkty przerwania w VM, C API, CLI i publicznym WASM.
- [x] 12.2b Udostępnić wspólne sterowanie w Python, Swift, C#/Unity, Kotlin/JNI, SFML i Godot oraz sprawdzić rzeczywiste bindingi.
- [x] 12.2b.1 Podłączyć i sprawdzić Python, Swift, C#/Unity i Kotlin/JNI: pełne wyniki, dokładny work, deadline, anulowanie podczas skanu i własność tokenów.
- [x] 12.2b.2 Podłączyć i sprawdzić SFML oraz Godot przez ten sam C API.
- [x] 12.2 Udostępnić deadline/anulowanie w VM i hostach oraz punkty przerwania wewnątrz długich skanów, geometrii i kopiowania.
- [x] 12.3 Na podstawie pomiarów ustalić skończony domyślny limit pracy, sposób liczenia jednostek i maksymalną porcję nieprzerywalnej pracy; zapisać wartości i ich uzasadnienie.
- [x] 12.4 Sprawdzić kosztowne dane przy niskim fuel za builtin, skumulowaną pracę wielu małych wywołań, granice pamięci i przerwanie w każdym pipeline.
- [x] 12.5 Potwierdzić, że fuel/work są deterministyczne, deadline nie wpływa na RNG i udane obrazy, a błędy nie publikują częściowych wyników ani nie zostawiają zasobów.
- [x] **Etap 12 zakończony.**

**Odbiór:** host ma egzekwowalne ograniczenie kosztu niezależne od promocyjnej taryfy; brak zegara w danym środowisku nie wyłącza licznika pracy.

**12.1 / 12.2a — weryfikacja rdzenia (2026-11-04):** [kontrakt techniczny](spec/execution-limits-v0.9.md). Osobny licznik obejmuje setup, instrukcje, skany, geometrię, alokowane bufory i kopie; 5 mln fuel i dotychczasowe stawki pozostają bez zmian. C API 0.6 dodaje osobne deskryptory i token, zachowując ABI 0.5 i stare layouty. Wykonano 93/93 natywnych CTest (build bez SFML/Godot), dziewięć skupionych przypadków / 516 asercji oraz pełne conformance native/WASM. Nowa bramka porównuje dokładne liczniki work/fuel i obrazy 21 pipeline’ów, 42 granice, 21 odzyskań i 2048 anulowań. Rzeczywisty drugi worker przerywa synchroniczne `noise.mask` wewnątrz instrukcji. Przebieg zachowuje poprzednie wzorce obrazów/RNG i pełne 23 sceny części. Ten checkpoint nie zamykał pozostałych bindingów, kalibracji ani końcowego audytu 12.3–12.5; ich odbiór opisano poniżej.

**12.2b.1 — bindingi zarządzane (2026-11-04):** Python: 10/10 testów; Swift: 8 zaliczonych / jeden Metal pominięty z braku urządzenia; rzeczywisty C#/native oraz Kotlin/JNI z CheckJNI: PASS. Każdy przebieg obejmuje 23 pełne sceny części przy dokładnym budżecie i o jednostkę za małym, deadline oraz anulowanie aktywnego skanu z innego wątku. Kotlin sprawdza też zamknięcie tokena w trakcie generacji. Wszystkie cztery odtwarzają 21 829 zamrożonych obrazów bez zmiany wzorców; test pikseli jawnie przyznaje większy work dla dawnych scen o zwiększonym fuel. Domyślny budżet produktu pozostaje bez zmian. Zbudowano świeży XCFramework z trzema slice’ami / pięcioma architekturami; wykonanie Swift dotyczy macOS arm64. Nie zaliczono iOS runtime, Unity Editor ani urządzenia/emulatora Android.

[Raport bindingów zarządzanych](spec/verification/execution-limits-v0.9-managed.json) oraz [logi](spec/verification/logs-v0.9.tar.gz) (`execution-limits-v0.9-managed.txt`) zapisują zakres lokalnej weryfikacji. Kontrola typów Swift przechodzi również dla iOS arm64 i symulatora arm64; nie jest to test wykonania na tych platformach. Trzy testy pakowania/dokumentacji przechodzą po dołączeniu wspólnego źródła testu anulowania.

**12.2b.2 / 12.3 — silniki i pomiary (2026-11-04):** SFML i Godot udostępniają opcjonalne limity przez wspólny C API, utrzymują token podczas wywołania i sprawdzają 23 pełne sceny przy dokładnym work, budżecie o jednostkę za małym, deadline oraz anulowaniu aktywnego skanu. Godot zachowuje poprzednie zasoby/statystyki po błędzie. Natywnie: 100/100 CTest na Linuksie oraz 98/98 na macOS (dwa testy GL wykonane na Linuksie). Oba silniki odtwarzają tam 21 829 obrazów razem z odczytem tekstur GPU; cztery bindingi zarządzane również zachowują ten zamrożony korpus. Pełne conformance native/WASM pozostaje zgodne. Przeniesiony SDK przechodzi 10 testów Python i osobne konsumenty C++ shared/static z limitami i anulowaniem. [Logi hostów](spec/verification/logs-v0.9.tar.gz) (`execution-limits-v0.9-hosts.txt`).

[Kalibracja](spec/verification/execution-limits-v0.9-calibration.json) obejmuje 188 przypadków na native i WASM. Najwyższy work 141 dawnych operacji to 4 612 363, a 42 pipeline’ów 64²/256² — 6 217 470. Ustalono **100 mln** domyślnego work, **1024** logiczne kroki między punktami sprawdzenia oraz **32 KiB** w pojedynczej porcji bulk copy/init; największy stały workspace geometrii ma ograniczenie kompilacyjne 32 KiB. Bufor sterowania jest przenoszony z punktami przerwania, a jednostki obejmują także jego setup i przekazanie wyników helpera. Odtwarzane liczniki work/fuel, obrazy i powody błędów zgadzają się między native i WASM. Zaobserwowane maksymalne odstępy wynoszą 4,245 ms native / 0,967 ms WASM; nie są przenośną gwarancją czasu — zegar hosta, allocator, planista i sprzątanie mają osobny opis zakresu. Końcowy audyt 12.4–12.5 i sanitizery opisuje raport zamknięcia poniżej.

**12.4 / 12.5 — audyt zamknięcia (2026-11-04):** [raport](spec/verification/execution-limits-v0.9.json), [Linux 100/100](spec/verification/logs-v0.9.tar.gz) (`execution-limits-v0.9-linux-ctest.txt`), [macOS 98/98](spec/verification/logs-v0.9.tar.gz) (`execution-limits-v0.9-macos-ctest.txt`), [pełne native/WASM](spec/verification/logs-v0.9.tar.gz) (`execution-limits-v0.9-conformance.txt`) oraz [sanitizery](spec/verification/logs-v0.9.tar.gz) (`execution-limits-v0.9-sanitizers.txt`). Weryfikacja obejmuje 21 pipeline’ów, 42 dokładne granice, 21 odzyskań, 2048 błędów getterów WASM i po 2048 anulowań C API shared/static, granice pamięci i błędy alokacji. Dziewięć skupionych przypadków / 516 asercji sprawdza także porcje dużych rekordów i przenoszenie bufora sterowania. Błędy nie publikują częściowych wyników; zachowane wyniki przeżywają kolejne nieudane generacje. Udane przebiegi zachowują fuel, RNG i obrazy.

ASan/UBSan/LSan pokrywa 93/93 przypadki w dwóch przebiegach: 92 zaliczone w pełnym uruchomieniu oraz poprawiony test Python w ponowieniu. Pierwotny raport CPython 3.12 zachowano w archiwum; ten sam szum alokacji odtworzono bez biblioteki Infinite Props. Harness uruchamia teraz po 10 testów z `malloc` dla granic buforów i z `pymalloc` dla wycieków; w tym drugim trybie nadal musi wykrywać celowe błędy C++ po nazwie funkcji. Poprawka dotyczy testów, nie runtime. Kod silników i kalibracji to `2aa2700`; checkpoint z poprawką harnessu to `0c4a86e`. Porównano 974 pliki kodu/fixture’ów/CMake z użytym snapshotem Linuksa. Na czystym `0c4a86e` zbudowano i zweryfikowano dokumentację native i przeniesionego SDK: 115 builtinów / 159 przykładów. Końcowy commit zawiera tylko specyfikację, status kontraktu i artefakty weryfikacji; jego czyste SHA oraz zdalny stan są sprawdzane po publikacji. Zakres lokalny nie zalicza Windows, iOS runtime, Unity Editor/IL2CPP, urządzenia/emulatora Android ani Metal na macOS.

### Etap 13 — pełna taryfa, optymalizacje i benchmarki

**Zależności:** 01–12.

- [x] 13.1 Zmierzyć stare i nowe całkowite koszty wszystkich grup z §6 oraz wprowadzić wspólną normatywną tabelę/formuły stawek, bez podwójnego liczenia pracy.
- [x] 13.2 Obniżyć noise/fault/warp oraz skalibrować nowe generatory i części; zweryfikować monotoniczność ceny względem oktaw, powierzchni, sites i relaksacji.
- [x] 13.3 Zoptymalizować morfologię z dokładną zgodnością A8, gdzie to bezpieczne; skalibrować maski, blur, paint i narzut capture oraz figury.
- [x] 13.4 Wdrożyć rabaty operacji zbiorczych i kolorów, zachowując RNG/stepper i dodatni koszt elementów; nie podnosić ceny pixel/helperów/pętli.
- [x] 13.5 Porównać ręczny noise+pixel, noise.mask, contour+regions, figury i części dla 64×64/128×128/256×256 oraz ≥20 seedów na scenariusz; zapisać CPU, fuel, szczyt pamięci i porównanie obrazu/jakości.
- [x] 13.6 Osobno zbadać morfologię radius=0/1/4/16/64, kilka sigma blur, paint masek/warstw, przezroczystość i obszar poza targetem; pick(n) dla 0/1/16/256 oraz części dla 36/128/256 regionów.
- [x] 13.7 Powtórzyć testy odporności po rabatach; zapisać osiągnięte stawki i każde uzasadnione odstępstwo od celów procentowych. Zmniejszyć rabat przy ryzyku, zamiast podnosić fuel.
- [x] **Etap 13 zakończony.**

**Odbiór:** tabela jest liczbowa i zweryfikowana, deklaratywne odpowiedniki mają wyraźną przewagę fuel bez regresji wyników, RNG, pamięci i limitów wykonania.

**Weryfikacja:** [taryfa normatywna](spec/fuel-v0.9.md), [raport i zakres](spec/verification/fuel-v0.9.json), reprodukcja w `benchmarks/operations/README.md`. Pomiary obejmują 141 przypadków porównanych z 0.8, 10 nowych skalarów i 402 scenariusze, w tym pięć ścieżek × trzy rozmiary × 20 seedów. Sprawdzono 118 serii monotonicznych oraz wszystkie sites 1–256 dla 20 seedów (5120 generacji). Surowe CPU/fuel/pamięć/obrazy i źródła są skompresowane, zamiast powielać rozbudowane raporty.

Stawki: algebra i morfologia 40%, blur/paint 60%, bufor capture 70%, poly/curve/oval 75%; nowe kernele geometrii i masek 20%, skan wierzchołków pojedynczej części 50%, paczki 25% plus dodatnia opłata za każdą instancję. Pełny przepływ paczki z `parts.get` jest tańszy dla 36/128/256 regionów. Noise/fault/warp i kolory pozostają przy minimalnym 1; `ramp` pozostaje loweringiem 25 w bazowym pomiarze no-opt. Nie zwiększono cen pixel/helperów/pętli ani budżetów domyślnych.

Bezpośredni noise.mask daje identyczne RGBA przy 5,3–5,7% fuel ręcznej pętli. Kontury/części mają jawnie zmierzoną różnicę wynikającą z uproszczenia i AA; proxy figur z kół traci kształt/dziury i nie jest traktowane jako równoważna jakość. Morfologia zachowuje dokładne A8/D4, używa ograniczonej kolejki i przy radius=64 przyspiesza w pomiarze około 30×; małe erozje mogą kosztować kilka mikrosekund więcej.

Na końcowej taryfie zaliczono macOS 98/98, Linux 100/100, pełny native/WASM, 21 identycznych liczników pracy i rzeczywiste anulowanie w workerze. Zamrożone 21 829 przypadków obrazu przeszło przez Python/Pillow, SFML/GPU i Godot/GPU. ASan/UBSan/LSan: pełne 93/93 przed ostatnim dostrojeniem rabatu paczek, następnie 26/26 dotkniętych ścieżek na końcowej stawce (w tym wszystkie 827 przypadków C++, części, kontrole C API i błędy alokacji). Zestawienie zachowuje ten zakres i nie oznacza ponowienia całego wydania na wszystkich platformach.

### Etap 14 — dokumentacja, przykłady i integracja produktu

**Zależności:** 13.

- [x] 14.1 Zaktualizować handbook, eksport opisu języka, LLM reference, podpowiedzi edytora i dokumentację hostów z katalogu i ostatecznych kontraktów 0.9.
- [x] 14.2 Przenieść przykłady z §7 do sprawdzanych źródeł; dodać sceny bruku, muru, suchej ziemi, plam żyrafy i geparda oraz przykład eksportu części.
- [x] 14.3 Dodać migrację 0.8→0.9 z opisem nowej taryfy, namespace, ograniczeń uchwytów, D4 i granicy programu/wyniku, bez obiecywania historycznego fuel.
- [x] 14.4 Uzgodnić wersje produktu, języka i potrzebnych kontraktów API/ABI z istniejącą polityką repo; dopisać changelog i zamrożoną specyfikację techniczną 0.9.
- [x] 14.5 Sprawdzić kompilowanie przykładów, formatter round-trip, generowanie dokumentacji i wspólne ścieżki wizualne strony/edytora; aktualizacja źródeł nie jest publikacją strony.
- [x] **Etap 14 zakończony.**

**Odbiór:** publiczne opisy i przykłady odpowiadają rzeczywistemu API oraz taryfie, bez pozostałości pseudokodu lub niezdefiniowanych wywołań.

**Weryfikacja:** [raport](spec/verification/documentation-v0.9.json) i [skompresowane logi/obrazy](spec/verification/documentation-v0.9.tar.gz). Handbook: 115 builtinów / 165 wykonywanych przykładów; sześć nowych scen ma stabilny formatter i zgodne obrazy/wyjścia w 96 generacjach. Referencja LLM pozostaje poniżej 128 KiB. Schemat 6 opisuje końcową taryfę i pułapki wykonania; 233 mutacje / 165 przypadków CLI przechodzą także formalny JSON Schema. Produkt/język 0.9.0, API 0.6 / ABI 0.5; zamrożony kontrakt składa się z zachowanej bazy i jawnych rozszerzeń, bez kopiowania tysięcy linii.

macOS: 98/98 CTest oraz końcowe 5/5 dotkniętych bramek; pełne native/WASM, 51 testów edytora, sześć testów renderowania dokumentacji i smoke przeniesionego SDK: PASS. Paczka strony zachowuje komplet plików manifestu, 42 obrazy native/WASM i 51 scen motyli. Chromium sprawdza handbook desktop/mobile, menu i rzeczywisty HTML podglądu z obrazem z natywnego runtime; most VS Code jest symulowany, osobny GUI extension-host nie był uruchamiany. Sześć scen obejrzano, a Python potwierdza 36 snapshotów / jeden współdzielony zasób eksportu po zamknięciu programu. Etap 15, wydanie i publikacja strony pozostają osobne.

### Etap 15 — końcowa weryfikacja 0.9

**Zależności:** 01–14; gotowość techniczna, bez publikowania release.

- [ ] 15.1 Uruchomić pełny wymagany zestaw testów i buildów, walidację formatu, sanitizery tam, gdzie wspierane, oraz macierz hostów i native/WASM na finalnej rewizji.
- [ ] 15.2 Potwierdzić golden 0.8, wyniki nowych algorytmów, stabilność RNG/ID, dziury, regiony i D4 oraz komplet eksportowanych danych i lifetime.
- [ ] 15.3 Zweryfikować limity pamięci, geometrii, instancji, eksportu, fuel, pracy i anulowania; uwzględnić błędy alokacji i brak częściowego wyniku.
- [ ] 15.4 Obejrzeć sceny wizualne i porównać macierz 20 seedów w trzech rozdzielczościach; potwierdzić jakość i przewagę fuel z raportu benchmarków.
- [ ] 15.5 Sprawdzić dokumentację, wersje, przykłady, końcowy diff i checklistę; przygotować raport zakresu, wyników i ewentualnych niewykonanych sprawdzeń.
- [ ] 15.6 Sprawdzić staged scope i przygotować finalny checkpoint bez niezamierzonych zmian; po jego commit/push potwierdzić zgodność SHA w raporcie dostarczenia, zgodnie z §8.
- [ ] **Etap 15 zakończony — zakres 0.9 zweryfikowany.**

**Odbiór:** pełne funkcje, jawne wyniki testów i pomiarów oraz brak otwartych kryteriów obowiązkowych. Wydanie/tag/main/deployment wymagają odrębnej czynności poza tym planem.

**Weryfikacja:** oczekuje na wykonanie.

## 10. Źródła bazowe w repozytorium

- [Zamrożony kontrakt 0.8](spec/iprop-v0.8.md) oraz [opis języka](docs/language-description.md).
- [Noise](docs/language/noise.md), [warstwy i maski](docs/language/surfaces.md), [argumenty i helpery](docs/language/functions.md), [inputs i outputs](docs/language/assets.md).
- [Implementacja noise](runtime/seeded.cpp), [walidacja nagłówka i sekcji](runtime/format.cpp), [model wyniku VM](runtime/iprop/VM.hpp), [checked C API](backends/capi/iprop.h).

Te odnośniki opisują bazę 0.8 podczas przeglądu; nie stanowią dowodu, że funkcje 0.9 są już zaimplementowane.
