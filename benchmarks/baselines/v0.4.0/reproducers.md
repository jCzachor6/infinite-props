# Reprodukcje 05-0.4

`reproducers.json` zawiera pełne źródła, komendy, stdout/stderr/exit oraz obserwacje zamrożonych narzędzi release66d.

89 przypadków języka (178 kompilacji optimized/reference),40 obserwacji C API,45 kontrolowanych prób OOM. Źródła .ips są w JSON; materializacje wykonawcze istnieją tylko pod /tmp/iprop-v05-repros/work. Zachowane .iprop/.png też są wyłącznie lokalne.

87 vs86: stary raport optymalizacji pomija examples/language/nested-import.ips; ten dodatkowy entry source jest compile-error, dlatego liczby renderów/trapów690/60 nie zmieniły się.

Potwierdzone błędy: std::bad_alloc przekracza C boundary podczas kopiowania komunikatu w catch FormatError (load-format-error, fail_at4) i catch Trap (generate-type-error, fail_at8); niepoprawny enum uniform.type=-1 jest akceptowany jako zero-color, potwierdzone też natywnym klientem C99.

OOM używa zastąpienia operator new i pojedynczego wyjątku w osobnym procesie. Nie wyczerpuje pamięci i nie żąda dużych alokacji. Konkretne numery alokacji dotyczą Darwin arm64/zamrożonego builda; przenośny test powinien wyznaczyć liczbę i zrobić sweep.

Zwykłe reguły starego uniform API (unknown ignored, first duplicate wins, wrong type traps) są zgodnością. Ucieczka wyjątków i niepoprawny enum są błędami do naprawy, nie docelowym kontraktem.

Przyszłe sized descriptors/reflection/parameters nie istnieją w0.4; JSON jawnie oznacza tych testów jako nierunnable i daje przypadki do aktywacji poV/R/P/X. Nie wywoływano nieistniejących funkcji ani nieprawidłowych rozmiarów pamięci starego ABI.
