# Materiały w Infinite Props — wykonanie dla 0.8.0

Status: **Completed** na `develop`, po implementacji i review `feat/materials`.
Ten zapis opisuje ostateczny zakres przygotowany do 0.8.0. Pierwotny wywiad
z 2026-10-27 rozważał nowy typ VM, zwroty i kolekcje materiałów; te warianty
nie są częścią zrealizowanego kontraktu.

## Zrealizowany kontrakt

- [x] Osobne pliki `.ips` z jednym lub wieloma `material Name { ... }`, bez assetu.
- [x] Prywatne `palette` i `const`, importowane zwykłym `import alias from "path"`.
- [x] Kwalifikowane typy i konstruktory, np. `forest.Bark` i `forest.Bark()`.
- [x] Wymagane `base: color`; pozostałe pola autora typu color/i32/f32/vec2.
- [x] Zakresy i dystrybucje, jedna ewaluacja na pole w kolejności definicji.
- [x] Odczyt wcześniejszych pól, nazwane nadpisania i atomowe zastąpienie instancji.
- [x] Niemutowalne pola, kopie bez dodatkowego RNG, nominalne parametry helperów.
- [x] Kontrola typów i zapisu przed rozwinięciem; nazwy wewnętrzne bez kolizji.
- [x] Zachowane powiązania builtinów biblioteki i osobna przestrzeń nazw materiałów.
- [x] Sprawdzanie i formatowanie bibliotek w CLI i edytorze.
- [x] Przykład drzewa, dokumentacja języka, gramatyka i migracja 0.7 → 0.8.

## Technika i granice

Kompilator rozwija materiał do istniejących pól skalarnych. Nie ma nowego
wire type, opcode ani zmiany C API/ABI. Wiążące reguły są w
[kontrakcie materiałów](../../spec/materials-0.8.md) i
[specyfikacji 0.8](../../spec/iprop-v0.8.md).

`edge`, `edge_width` i `taper` są zwykłymi nazwami pól, bez wartości domyślnych
ani wbudowanego rysowania. Geometrię i wykorzystanie tych pól określa helper.
Zwroty materiałów, listy, samplery/steppery, const materiału i przekazywanie
całej instancji bezpośrednio do partu pozostają poza kontraktem 0.8.0.

## Weryfikacja

Testy w `tests/compiler/materials_test.cpp` porównują obraz, wyjścia i RNG z
ręcznie zapisanym kodem skalarnym, na wielu seedach i z optymalizacją oraz bez.
Obejmują zastąpienie instancji, prywatność deklaracji, kolizje nazw, kontrolę
typów, niezależne strumienie partów i formatowanie. Testy CLI/edytora sprawdzają
diagnostyki, komentarze i ochronę plików przy błędach. Scenariusze materiałów
wchodzą również do porównań native/WASM.

Bramki całego wydania i artefakty określa
[przygotowanie release 0.8.0](../release-v0.8.md).
