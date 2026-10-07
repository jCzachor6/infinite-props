# Infinite Props 0.6 — jawny język, generowanie LLM i wykonanie przez C API

**Status:** plan wdrożenia; wszystkie zadania są do wykonania.
**Data:** 2026-10-05.
**Baza planowania:** wydane `v0.4.0`, SHA `66d34c2831585afb231022da9296528a912e962e`. Baza implementacji: ukończone/wydane 0.5, którego rzeczywisty SHA utrwalamy w 06-0.1.
**Cel:** dokończyć jawny, sprawdzalny zapis języka oraz dać hostom wielokrotną generację z kontrolą zasobów, współbieżnością i wygodnym SDK.

Ten dokument jest drugą połową [planu 0.5](iprop-v0.5-plan.md). Nie zastępuje zadań 0.5 ani nie powtarza ich implementacji. Oba plany zawierają zmiany języka/narzędzi i C API. Mapa na końcu planu 0.5 rozlicza wszystkie pierwotne zadania i wszystkie dodane propozycje C API.

## Bilans i zależności

Wstępny nakład: **105 punktów dla 0.5 i 97 dla 0.6**. Punkty są względnym szacunkiem implementacji, migracji i weryfikacji, a nie czasem ani zmierzoną produktywnością. Po reproduktorach/benchmarkach przeliczamy obie strony razem. W 0.5 zostają podstawowe kontrakty i naprawy, a w 0.6 funkcje budowane na tych kontraktach.

| Etap | Zakres | Priorytet | Punkty | Zależności |
| --- | --- | --- | ---: | --- |
| 0 | Baza 0.5, projekty i reproduktory | P0 | 5 | ukończone 0.5 |
| D | `let`/`fn` i analiza inicjalizacji | P1 | 13 | 05-B/C, 0 |
| E | Konstruktory geometrii | P1 | 5 | 05-C, D |
| F | Formatter i lint nowego zapisu | P1 | 3 | 05-F, D/E |
| G | Aktualizacja kontraktu i referencji | P1 | 3 | 05-G, D/E/F |
| H | Gramatyka i jeden adapter | P1 | 13 | D/E/F/G |
| K | Konteksty i współbieżność | P0 | 8 | 05-V/R/P/X/M, 0 |
| L | Limity hosta i anulowanie | P0 | 8 | K, 05-X |
| M | Generacja do bufora hosta | P1 | 8 | K/L, 05-O/M |
| N | Pomiary i rozszerzenie statystyk | P1 | 2 | 05-M, K/L/M/Q |
| Q | Synchroniczna generacja batch | P1 | 3 | K/L/M |
| T | Osobna biblioteka C API kompilatora | P1 | 8 | 05-B/G/X, D/E/F/G |
| U | C++ RAII i binding Rust | P1 | 5 | K/L/M/Q/T |
| J | Integracje, CI i wydanie | P0 | 13 | wszystkie powyższe |
| **Razem** | | | **97** | |

Przykładowa kolejność commitów: `0 → D → E → F → G → K → L → M → Q → N → T → H → U → J`. Poszczególne moduły mogą powstawać po spełnieniu swoich zależności; nie wymagają wielkiego wspólnego refactoru. Pierwszy kamień milowy: składnia/formatter oraz konteksty/limity. Drugi: bufory, batch, kompilator i gramatyka. Trzeci: bindingi, porównania, CI i paczki.

## Kontrakty 0.6

- Produkt **0.6.0**, język **0.6**, format **0.4**, C API runtime **0.4** jako rozszerzenie API 0.3/0.2. Zachowujemy linię ABI/SONAME **0.4** ustaloną w 05-V. API kompilatora ma osobny numer **0.1** i osobną bibliotekę.
- Obowiązują [decyzje zgodności z 0.5](iprop-v0.5-plan.md#decyzje-projektowe): wcześniejsza składnia działa, defaulty/liczby/RNG/D4 i koszt instrukcji nie zmieniają się, istniejące struktury C pozostają zamrożone, nowe deskryptory mają określony rozmiar.
- `let` jest mutowalny i wymaga inicjalizatora; typ jawny dopuszcza obecne poszerzenie `i32 → f32`, kolejne przypisania są dokładnie typowane. Zasięg funkcyjny, bez shadowingu. Legacy locals zachowują dotychczasowe zerowe defaulty i dostają ostrzeżenia zamiast nowych błędów.
- `let`/`fn` rozpoznajemy kontekstowo; stare zmienne/helpery o tych nazwach pozostają. `vec2` i alias `shape` nie przejmują istniejących wiązań. `vec2` jest wartością VM; circle/rect pozostają złożeniami outputów, bez nowych rekordów.
- Parametry i indeksy należą do konkretnego programu. Program po przygotowaniu jest niezmienny; context ma stan mutowalny i jednego workera. Wynik legacy/owned pozostaje ważny po kolejnej generacji i zwolnieniu programu/contextu.
- Generacja owned zachowuje obecny ownership. Nowy tryb do bufora hosta jawnie rozdziela host-owned piksele od library-owned wyjść/statystyk. Nie podmieniamy bez ostrzeżenia semantyki starego `iprop_result`.
- Zwiększenie budżetu assetu, zmiana kosztów opcodes lub nowych typów binarnych nie są częścią planu. Limity hosta zawężają dozwolone wykonanie; anulowanie nie zmienia udanych wyników.
- Nazwy nowych symboli są robocze do review nagłówka i przykładu C99; wymagania semantyczne/ABI są wiążącymi kryteriami akceptacji.

## 0. Baza i kontrakty

- [ ] **06-0.1** Utrwalić wydane 0.5: SHA, artefakty, nagłówki i binaria klientów API 0.2/0.3, korpus source/binary, diagnostyki, obrazy/outputy/trapy/RNG oraz benchmarki 05-M. Uaktualnić rzeczywiste estymaty obu wydań i rejestr decyzji.
- [ ] **06-0.2** Przygotować reproduktory let/fn/vec2/shape i kolizji symboli, CFG inicjalizacji, życia buforów, współdzielenia programu, limitów partów/list oraz anulowania długiej operacji rasteryzacji. Zdefiniować nowe poprawne/błędne przykłady przed zmianą implementacji.
- [ ] **06-0.3** Zapisać kontrakty context/generate_into/batch/compiler i model zasobów; przygotować klienta C99 jako review projektu API. API 0.4 dodaje symbole/capabilities do zamrożonej linii ABI, nie zmienia starych struktur ani wartości. Metadane rozdzielają produkt, język, format, runtime API, compiler API i SONAME.

**Akceptacja:** baza jest odtwarzalna; każdy nowy mechanizm ma opis sukcesu, błędu, zasobów i czasu życia. Nie nazywamy programów thread-safe przed audytem i weryfikacją K/J.

## D. Jawne deklaracje i analiza inicjalizacji

- [ ] **06-D.1** Dodać w AST rozróżnienie deklaracji `let` od przypisania. Obsłużyć inferencję i typ jawny: `let strength: f32 = 1;`, `let tint = #9dc;`. Zachować legacy deklaracje przez przypisanie i ich dotychczasowe wartości domyślne w runtime.
- [ ] **06-D.2** Dodać `fn paint(x: f32, tint: color) { ... }` i postać z istniejącym `-> type`. `fn` nie zmienia widoczności uniformów ani reguł parametrów; rekurencja nadal jest odrzucana.
- [ ] **06-D.3** Analizować pewność inicjalizacji na wszystkich ścieżkach: przecięcie stanów po `if/else`, pętle mogące wykonać się zero razy, `break`, `continue` i `return`. Czytanie niepewnie zainicjalizowanego jawnego `let` jest błędem; self-reference w inicjalizatorze również.
- [ ] **06-D.4** Dla legacy locals zgłaszać tę sytuację jako ostrzeżenie, bez zmiany działania poprawnego źródła 0.4. Dodać opcjonalny profil lint dla kodu LLM: zaleca jawne deklaracje, inicjalizację przed rozgałęzieniem i czytelne konwersje. Walidacja języka i zalecenia stylistyczne mają odrębne kody diagnostyk.
- [ ] **06-D.5** Zabezpieczyć funkcje i zmienne o nazwach `let`/`fn`, nazwy parametrów, pętle oraz kolizje z input/output. Nie dodawać niejawnego przechwytywania uniformów przez helpery.

**Akceptacja:** nowy zapis ma jednoznaczne typy i mutowalność, a analiza nie uznaje inicjalizacji wewnątrz niewykonanej gałęzi lub pętli za pewną. Stary korpus nadal kompiluje się bez nowych błędów. Ostrzeżenia nie zmieniają bytecode'u.

## E. Nazwane konstruktory geometrii

- [ ] **06-E.1** Dodać `vec2(x=2, y=4)` jako równoważnik `{2, 4}` z obecnymi konwersjami numeric → `f32`. Konstruktor działa również jako lokalna wartość i argument helpera.
- [ ] **06-E.2** Dodać `shape.circle(x=8, y=8, radius=3)` i `shape.rect(x=0, y=0, width=8, height=12)` do istniejących miejsc przyjmujących złożenia outputów. Pozostawić `{x,y,r}` i `{x,y,w,h}` oraz obecne odczyty scalar outputów części.
- [ ] **06-E.3** Rejestrować sygnatury i ograniczenia konstruktorów w kontrakcie języka; korzystać z bindera 05-C, rozszerzonego o konstruktory. Nie utożsamiać `shape.rect(...)` z operacją rysującą `rect(..., color=...)`.
- [ ] **06-E.4** Zachować rozstrzyganie istniejących symboli: zadeklarowany helper `vec2` zachowuje swoje wywołania, alias części `shape` zachowuje `shape.draw`/`shape.outputs`. Konstruktor jest rozpoznawany tylko tam, gdzie nie przejmuje istniejącego wiązania. Konflikt opisuje diagnostyka i podręcznik.

**Akceptacja:** stare i nowe postacie mają te same bity wartości, outputy i transformacje D4, także dla outputów zapisanych przez helpery części. Próba przechowywania `shape.rect`/`shape.circle` w lokalnej lub przekazania ich jako nowego typu helpera dostaje jasny błąd, bez rozszerzenia VM.

## F. Formatter i lint jawnego zapisu

- [ ] **06-F.1** Rozszerzyć formatter 05-F o drukowanie helperów z `fn` i konstruktorów oraz nowy profil kanoniczny. Zachować rozwiązane wiązania i kolejność efektów; kolizje z helperem vec2/aliasem shape mogą wymagać pozostawienia legacy postaci.
- [ ] **06-F.2** Drukować jawne `let` z podanym typem, jeśli był zapisany. Nie dopisywać `let` do legacy przypisań i nie przenosić deklaracji między gałęziami; lint osobno proponuje migrację i inicjalizację przed rozgałęzieniem.
- [ ] **06-F.3** Zachować komentarze i ich przypięcie przy nowych deklaracjach/konstruktorach/named calls. Integracja z edytorem i galerią korzysta z jednego formattera.

**Akceptacja:** idempotencja, zachowane komentarze i ten sam kod wykonywalny po strip debug dla starego i nowego zapisu; szczególnie CFG, RNG, floaty i kolizje nazw. Ostrzeżenia/migracja nie zmieniają automatycznie źródła ani bytecode'u.

## G. Kontrakt języka i dokumentacja 0.6

- [ ] **06-G.1** Rozszerzyć `--describe-language` o faktyczne let/fn, reguły inicjalizacji, konstruktory i ich ograniczenia, gramatykę/profil i wersje komponentów. Sygnatury/efekty nadal pochodzą ze wspólnych rejestrów i bindera, nie z ręcznej listy dla modelu.
- [ ] **06-G.2** Regenerować kompaktowe `LLM_REFERENCE.md` z przykładami jawnych deklaracji, helperów, części, geometrii i parami błąd/diagnostyka/poprawka. Zachować budżet objętości i wybór sekcji; prompty używają referencji właściwej dla kompilatora.
- [ ] **06-G.3** Uaktualnić pełny podręcznik, galerię i migrację 0.5 → 0.6 oraz `cases.json`. Stare przykłady i signed/defaulty naprawione w 0.5 pozostają poprawne; przypadki historyczne są w bazie, nie jako nieaktualne oczekiwania bieżącej wersji.
- [ ] **06-G.4** Wersjonować schematy eksportu i kontrolować wszystkich konsumentów: generator dokumentacji, edytor, stronę, grammar adapter, compiler C API oraz paczki. Każde pole ma opis i test zgodności; breaking schema wymaga jawnej migracji.

**Akceptacja:** pełna i kompaktowa dokumentacja, kompilator i edytor opisują ten sam commit. Nowa reguła bez wykonywalnego przykładu lub oczekiwanego błędu zatrzymuje kontrolę dokumentacji.

## H. Gramatyka do ograniczania generacji

- [ ] **06-H.1** Przygotować jeden maszynowy opis gramatyki i generować z niego czytelny EBNF oraz artefakt dla adaptera. Zapewnić sprawdzaną zgodność z rzeczywistym lexerem/parserem; samo przepisywanie obecnego bloku EBNF z Markdown nie wystarcza.
- [ ] **06-H.2** Wyeksportować pełną gramatykę źródłową oraz profil kanoniczny dla modelu. Profil ogranicza wybór zapisu, np. do słów, `fn`, `let` i średników, ale jego programy mają dokładnie semantykę zwykłego `.ips`. Ująć komentarze, stringi importów, escape'y, liczby, priorytety, named args i nowe konstruktory.
- [ ] **06-H.3** Wykonać jeden adapter do backendu, który rzeczywiście obsługuje ograniczenia gramatyką. Rejestrować jego format i wersję; dla backendów bez tej możliwości pętla korzysta z normalnego generowania i `--check`. Nie zakładać wspólnego API gramatyki u wszystkich dostawców.
- [ ] **06-H.4** Sprawdzać korpus poprawnych i niepoprawnych konstrukcji oraz granice tokenów. Porównywać akceptację składniową z parserem; oddzielnie sprawdzać typowanie i ograniczenia semantyczne. Składniowo poprawna gramatyka może nadal dopuścić program z błędem typów.
- [ ] **06-H.5** Włączyć artefakty gramatyki, opis profili i ich hashe do wspólnego manifestu dokumentacji. Edycja składni bez regeneracji i kontroli zgodności ma zatrzymywać CI.

**Akceptacja:** adapter potrafi wygenerować kompletne źródło `.ips`, które parser przyjmuje; działa także zwykła ścieżka bez adaptera. Przypadki semantycznie błędne nadal trafiają do kompilatora i diagnostyki, a nie są raportowane jako sukces języka.

## K. Konteksty wykonania i współbieżność — P0

- [ ] **06-K.1** Dodać opaque context związany z przygotowanym programem i używający parametrów 05-P; create/reset/free oraz generację owned. Zależności utrzymują dane przy życiu zgodnie z opisem, także gdy host zwolni swój uchwyt programu. Stare generate pozostaje wejściem bez konieczności zarządzania contextem.
- [ ] **06-K.2** Przygotować niezmienne dane wykonania programu, np. mapy control-flow i schemat slotów; workspace/kolekcje/stosy są prywatne dla contextu. Audyt obejmuje LoadedFile, lazy cache, allocator, diagnostykę i lifetime, nie tylko const w sygnaturze.
- [ ] **06-K.3** Ponownie wykorzystywać scratch buffers/capacity i przygotowane dane, z określonym resetem po sukcesie, trapie, limicie i cancel. Nie przenosić RNG, poprzednich locals ani list outputów do kolejnej generacji. Parametry są odczytywane jako zgodny snapshot.
- [ ] **06-K.4** Wyniki są niezależne od reuse/free contextu i programu. Reuse scratchu nie unieważnia pikseli/outputów poprzedniego wyniku; ewentualny pool buforów musi mieć własny bezpieczny lifetime. Nie wprowadzać ukrytej globalnej puli ani globalnego last_error.
- [ ] **06-K.5** Po audycie i TSan zagwarantować współdzielenie jednego immutable programu przez osobne contexty/workery. Context i mutowalne parametry mają jednego użytkownika naraz; free nie ściga się z użyciem. Nie dodawać własnego thread poola ani obowiązkowego async API — scheduling należy do hosta.

**Akceptacja:** sekwencja seedów/sukcesów/błędów daje te same wyniki co osobne generacje; wcześniejsze wyniki nie zmieniają się. Współdzielenie programu daje deterministyczne wyniki bez wyścigów, a umowa wątkowa jest podana dla każdego uchwytu.

## L. Limity hosta i anulowanie — P0

- [ ] **06-L.1** Dodać politykę load `_ex`: max input/decoded bytes, kontrola limitu przed alokacją/dekompresją i limit struktur potrzebnych walidatorowi. Istniejące zamrożone limity formatu pozostają dodatkową granicą. Błąd polityki zasobów jest odróżniony od uszkodzonego formatu i rzeczywistego OOM.
- [ ] **06-L.2** Dodać limity generacji: fuel, piksele, pamięć robocza/wyniku, liczba elementów outputów. Budżet fuel jest najwyżej meta.fuel, koszt instrukcji nie zmienia się. Opcjonalność limitu i literalne zero mają zdefiniowane znaczenie; stare wywołanie stosuje dawną politykę.
- [ ] **06-L.3** Liczyć zasoby root i pośrednich canvasów partów, stosów, list outputów i buforów konwersji C, w tym rzeczywiste capacity/reuse. Jawnie oddzielić limity pamięci programu od wykonania i zdefiniować rozliczenie zewnętrznego bufora. Nie reklamować limitu całego procesu/RSS, jeśli kontrolowane są alokacje biblioteki. Checked arithmetic przed iloczynami/stride/reserve.
- [ ] **06-L.4** Dodać anulowanie przez bezpieczny opaque token i/lub callback z user_data, z określonym lifetime. Sprawdzać je także w długich operacjach rasteryzacji, w ograniczonych odstępach pracy; sam check między opcodes nie wystarczy. Brak cancel pozostaje tani i nie zmienia udanych pikseli ani fuel semantics.
- [ ] **06-L.5** Oddzielne przyczyny/statusy limit/cancel w nowych wejściach, żadnych częściowych owned results. Po błędzie context nadaje się do resetu/reuse. Opisać statystyki nieudanego wykonania i propagację do batch; przetestować limity dokładnie na granicy i podczas pracy partu/listy.

**Akceptacja:** ograniczenia faktycznie zatrzymują kosztowne ścieżki przed niekontrolowaną alokacją i pozwalają przerwać długie operacje. Standardowa generacja zachowuje wynik i budżet 0.4/0.5; ograniczenia nie są nową semantyką opcodes.

## M. Generacja do pamięci hosta — P1

- [ ] **06-M.1** Wprowadzić wewnętrzną granicę przechowywania obrazu: root-owned i host-provided buffer korzystają z tych samych operacji rasteryzacji, np. przez CanvasView/BufferTarget. Party nadal mają jawnie rozliczane bufory pośrednie. Nie tworzyć alternatywnego renderera ani wirtualnej fabryki każdego opcode.
- [ ] **06-M.2** Dodać generate_into z mutable RGBA8, capacity i row_stride; zweryfikować wymiary, alignment/stride i zakres potrzebnych bajtów z checked arithmetic przed zapisami. Padding poza aktywnymi pikselami pozostaje nienaruszony; wspieramy ten sam top-left i tę samą semantykę alfa.
- [ ] **06-M.3** Jawnie rozdzielić ownership: host utrzymuje pamięć pikseli, osobny uchwyt przechowuje wyjścia/statystyki; stary owned result nadal posiada swoje piksele. Przy trap/limit/cancel bufor hosta może zawierać częściowy obraz i jest oznaczony jako niepoprawny; nie przekazujemy udanego payloadu. Nie obiecywać transakcyjnego bufora bez staging copy.
- [ ] **06-M.4** Utrzymać standardowe owned generate i opcjonalny copy_pixels dla prostych bindingów. Sprawdzić porównanie owned/into/copy na stride, różnych rozmiarach, outputach i błędach; dopiero pomiary N uzasadniają twierdzenie o ograniczeniu alokacji/kopii.

**Akceptacja:** generate_into faktycznie renderuje root do wskazanej pamięci, a nie tylko dokleja końcowy memcpy. Wynik ma identyczne piksele/outputy; błędny deskryptor nie zapisuje poza buforem. Host nie zwalnia pamięci biblioteki i biblioteka nie zwalnia host-owned pamięci.

## N. Statystyki i benchmark wykonania

- [ ] **06-N.1** Rozszerzyć benchmark 05-M: legacy/nowe owned, pierwszy/ponowny context, into i batch dla tego samego korpusu. Rejestrować wersje/build/hardware/warmup, rozkład czasu i mierzalne alokacje/kopie/peak zarządzanej pamięci. Nie wprowadzać niestabilnego absolutnego progu czasu do każdego PR.
- [ ] **06-N.2** Dodać potrzebne, mierzalne liczniki do nowych sized statystyk, np. peak scratch/output bytes, liczba elementów i opcjonalne liczniki draw/VM. Profilowanie/tracing ma określony koszt i nie zmienia sukcesu ani RNG; nie dodawać liczników bez konsumenta. Raport porównuje 0.5/0.6 i wskazuje faktyczny zysk/ograniczenia.

**Akceptacja:** profile, limity i generacje bez instrumentacji zachowują bity. Raport pozwala ocenić wkład contextu, storage i batch oddzielnie; obietnice wydajności wynikają z danych.

## Q. Synchroniczna generacja batch

- [ ] **06-Q.1** Dodać batch przyjmujący niezależne żądania seed/variant/archetype/size/parameters i opcje zasobów, bez narzucania wątków hostowi. Kolekcja ma jawny stride/wersję elementu lub opaque kontener; nie opierać przyszłej zgodności na powiększaniu tablicy publicznych struktur.
- [ ] **06-Q.2** Wyniki/statusy/błędy mają indeks zgodny z żądaniem; błąd jednego nie usuwa ukończonych pozostałych. Określić fail-fast/continue, anulowanie całego batch i per-request limity. Każdy element ma własny seed/stan, bez RNG zależnego od kolejności sąsiadów.
- [ ] **06-Q.3** Opisać i sprawdzić własność kontenera/wyników oraz sposób przekazania pojedynczego wyniku klientowi; unikać podwójnego free i dangling views. Porównać batch z równoważną serią generate, obejmując sukces/error/cancel i reuse contextu.

**Akceptacja:** batch odpowiada pojedynczym wywołaniom, zachowuje indeksy i deterministyczne seedy. Scheduling i budowa atlasu mogą należeć do hosta; ta funkcja nie wymaga wewnętrznej puli wątków.

## T. Osobne C API kompilatora

- [ ] **06-T.1** Zbudować opcjonalne `ipropcompiler` z własnym C99 headerem/API 0.1 i targetem SDK, oddzielone od lekkiego `ipropc`. Runtime nie zaczyna zależeć od parsera, filesystemu ani procesów CLI. Używać wspólnego pipeline w pamięci z 05-B, nie drugiego kompilatora.
- [ ] **06-T.2** Udostępnić check/compile z UTF-8 source bytes, wirtualną nazwą pliku i opcjami, zwracające owned bytes `.iprop` oraz strukturalne diagnostyki. Gettery są sized, kody/zakresy zgodne z CLI/edytorem; błąd nie zapisuje plików ani nie uruchamia subprocessu.
- [ ] **06-T.3** Resolver importów jest callbackiem hosta z user_data, opisem bazowej/żądanej ścieżki i jasnym kontraktem source-buffer lifetime/release. Compiler utrwala potrzebne źródła; obsługuje błędy importu, zachowując obecne reguły części i zakaz nested imports. Adapter filesystemu pozostaje w CLI/hoście.
- [ ] **06-T.4** Udostępnić capabilities/opis języka i opcjonalne metadane źródłowe, np. rozróżnienie input i32 od U32. Nie odtwarzać informacji źródłowej z binarnego programu i nie zmieniać formatu 0.4. Osobne kontrakty dla limitów źródła/importów/diagnostyk; wyjątki nie wychodzą przez C boundary.
- [ ] **06-T.5** Dodać klienta C99 kompilującego w pamięci, przekazującego bytes do runtime, ustawiającego parametry i odczytującego wynik; te same błędy/check i artefakty co CLI dla identycznych opcji. Sprawdzić działanie runtime SDK bez tej opcjonalnej biblioteki.

**Akceptacja:** kompilacja/check/importy/diagnostyki nie wymagają uruchamiania `ipc`; reguły i bity są zgodne z CLI. Własność source/result/diagnostics jest jawna, OOM nie wypuszcza wyjątku i sam runtime zachowuje lekki zestaw zależności.

## U. C++ RAII i binding Rust

- [ ] **06-U.1** Dodać cienki, źródłowy wrapper C++ RAII dla program/parameters/context/result i błędów. Używa wyłącznie publicznego C API; nie otwiera ABI prywatnych klas runtime/kompilatora i nie dodaje drugiej polityki walidacji.
- [ ] **06-U.2** Dodać jeden kompletny przykład bindingu Rust: definicje FFI z nagłówka, zgodne layouty, typowany wrapper, Drop, lifetime borrowed views i mapowanie statusów. Send/Sync wyłącznie dla typów, których kontrakt został zweryfikowany w K; callbacks/import resolver mają jawny lifetime.
- [ ] **06-U.3** Pokazać przepływy load/reflection/parametry/generate/output, context+limity+cancel oraz into/batch; osobny przykład opcjonalnego compile in-memory. Binding negocjuje wersję/capabilities i zgłasza brak funkcji, zamiast polegać na samym numerze produktu.
- [ ] **06-U.4** Budować i uruchamiać klienta/binding z zainstalowanego SDK na wspieranych platformach, bez prywatnych include paths. Przykłady pokazują error paths, empty lists, duplicate names różnych ownerów i poprawne free, a dokumentacja wskazuje dokładną wersję kontraktu.

**Akceptacja:** RAII i binding nie wyciekają ani nie unieważniają widoków; static/shared i relocated SDK działają. Jest jeden sprawdzony binding, a nie kilka niezweryfikowanych deklaracji integracji.

## J. Integracje, CI i przygotowanie wydania

- [ ] **06-J.1** Eksport/manifesty/preflight opisują produkt/język 0.6, format 0.4, runtime API 0.4, compiler API 0.1 i niezmienną linię ABI/SONAME 0.4. Nie wymagają nowej specyfikacji binarnej tylko z powodu numeru wydania.
- [ ] **06-J.2** Uaktualnić testy release, generator dokumentacji, kolektor paczek i schematy eksportu dla capabilities, gramatyki, compiler SDK/RAII/bindingu. Zachować dokładne SHA, hashe i kontrolę kompletności materiałów z faktycznie rozpakowanych archiwów.
- [ ] **06-J.3** Utrzymać test klientów API 0.2/0.3 z nową biblioteką i klienta 0.4. Dodać ASan/UBSan dla lifetime/buffers/arithmetic i TSan dla współdzielonego programu/osobnych contextów oraz cancel. Sprawdzić C99/C++, shared/static, eksporty i layouty na wspieranych platformach; unsupported ABI nie udaje przetestowanego.
- [ ] **06-J.4** Rozszerzyć korpus języka/edytora/CLI i `cases.json`/`check-optimizations.py` o rzeczywiste zachowanie. Sprawdzić optimized/reference, debug/strip, compressed/plain i frozen obrazy/outputy/trapy/RNG dla native/WASM/C/SFML/Godot. Zachować zasadę optimized ≤ reference, a wzrost względem historycznej wersji raportować osobno.
- [ ] **06-J.5** Zintegrować nowe parametry/metadane/błędy i contexty w SFML/Godot; użyć limitów/cancel tam, gdzie istnieje konsument. Pakować podręcznik 0.6, LLM_REFERENCE, gramatykę z hashami, C API reference, przykłady i manifest w SDK/WASM/stronie oraz opcjonalny compiler/binding. Sprawdzić SDK po przeniesieniu i brak niepotrzebnej zależności runtime od kompilatora.
- [ ] **06-J.6** Po akceptacji zamknąć changelog, migrację 0.5 → 0.6, raporty performance, finalne review, zielone CI dla jednego SHA i commit przygotowania 0.6.0. Merge develop → main, tag i publikacja należą do późniejszego zadania release.

**Akceptacja:** wszystkie wymagane joby przechodzą, paczki zawierają opisane możliwości i właściwe wersje. Legacy C API i korpus 0.4/0.5 zachowują kontrakt, a nowe funkcje mają dowody poprawności i ograniczeń.

## Granice modułów i fabryki

| Odpowiedzialność | Docelowa granica |
| --- | --- |
| Analiza inicjalizacji | Osobny przebieg CFG nad AST/planem, bez logiki drukowania lub emittera |
| Deklaracje/konstruktory | Istniejący binder/typing/Planner; jeden plan efektów i kolejności |
| Gramatyka | Maszynowy opis, generatory EBNF/profilu, zgodność z rzeczywistym lexerem/parserem |
| Generowanie LLM | Adapter backendu |
| Dane programu | Niezmienne metadata/validated execution plan, współdzielone zgodnie z lifetime |
| Context | Workspace/reuse, snapshot parametrów i stan jednej generacji |
| Zasoby/cancel | Polityka budżetu i token; egzekwowanie w VM/raster/storage, mapowanie błędów w C adapterze |
| Obraz | Canvas storage/view dla owned/borrowed target; jedna implementacja operacji pikseli |
| Kompilator C | Adapter pipeline/diagnostics/import resolver; osobny target/link boundary |
| SDK | C jako jedyny stabilny ABI; C++/Rust są cienkimi konsumentami |

Fabryka jest uzasadniona przy wyborze backendu LLM, renderera diagnostyki lub sposobu przechowywania obrazu. Pozostałe reguły pozostają w konkretnych modułach z małymi interfejsami. Nie budujemy ogólnej hierarchii fabryk dla AST/VM i nie wymuszamy globalnych managerów.

## Przykład docelowego zapisu 0.6

To przykład planowanego języka, nie deklaracja działania obecnego kompilatora 0.4.

```ips
asset Crystal {
  size 32x32;
  input f32 offset = -0.5;
  input i32 count = -1;
  palette levels = [0, 0.5, 1];
  output point anchor;
  output rect bounds;

  draw {
    let x: f32 = 8 + offset;
    paint(tint=#9dc, x=x);
    anchor = vec2(x=x, y=4);
    bounds = shape.rect(x=x, y=4, width=8, height=20);
  }

  fn paint(x: f32, tint: color) {
    rect(x=x, y=4, w=8, h=20, color=tint);
  }
}
```

## Zasady pracy i zamknięcia

- Review sąsiednich kontraktów i adekwatna weryfikacja poprzedzają osobny commit każdej ukończonej naprawy; większe funkcje mają sekwencję działających kroków. Release nie miesza się z refactorem.
- Nie powtarzamy pełnej macierzy bez nowej zmiany/ryzyka; uruchamiamy ją na kamieniach milowych i kandydacie wydania.
- Każdy etap obejmuje opis API/języka, przykłady, przypadki negatywne i migrację konsumentów. Nie dodajemy klas/generyków/rekurencji/nested imports/nowych opcodes ani nowych formatów pikseli w ramach tych zadań.

**Kryteria zakończenia:**

- [ ] Wszystkie etapy 0/D/E/F/G/H/K/L/M/N/Q/T/U/J mają implementację, weryfikację i commity.
- [ ] Jawne deklaracje, analiza inicjalizacji, konstruktory i formatter działają razem, bez nowych błędów starych źródeł.
- [ ] Gramatyka i adapter mają właściwe referencje.
- [ ] Contexty, współbieżność, limity/cancel, owned/into i batch mają zweryfikowane zasoby, błędy i lifetime.
- [ ] Kompilator C działa w pamięci i jest oddzielny od runtime; RAII/binding są sprawdzone z zainstalowanego SDK.
- [ ] API 0.2/0.3 i formaty 0.1–0.4 zachowują zgodność, obrazy/outputy/trapy/RNG; SONAME nie zmienia się wraz z produktem.
- [ ] Raporty performance, pełna/kompaktowa dokumentacja, gramatyka i rozpakowane paczki odpowiadają jednemu SHA; finalny zakres ma review i wymagane zielone CI.
- [ ] Materiały są gotowe do odrębnego zadania release 0.6.0.
