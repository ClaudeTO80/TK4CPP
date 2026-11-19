# TK4CPP — Stato del progetto

Documento di tracciamento. Ogni valore riporta la data e il commit di verifica.
Ultima verifica: 2026-11-19, commit `de4bd62`, GCC, Linux. Configurazioni provate con `cmake --build <dir> -- -k 0`.

## Configurazioni verificate

| Configurazione | Build | Test | Note |
| -------------- | ----- | ---- | ---- |
| Static, tests ON (XML/Zip OFF) | OK, 67/67 step | 88/88 pass | `build-static` |
| Shared, tests ON (XML/Zip OFF) | OK | 88/88 pass | `build-shared` |
| XML + static, tests ON (Zip OFF) | OK | 116/116 pass | `build-xmlt` |

Non verificate: Windows, macOS, Clang, C++20, sanitizer, build Doxygen, preset `msvc` e `debug-clang`, Zip (richiede libzip ≥ 1.11).

## Stato dei problemi noti (CLAUDE.md)

| # | Problema | Stato al 2026-11-19 |
| - | -------- | ------------------- |
| 1 | Target `TK4CPP::ThreadPool` inesistente nei test Thread | Risolto: la configurazione con tests passa |
| 2 | XML non compila (`FolderHandler::create` statico) | Risolto: XML compila, 116/116 test |
| 3 | Utility test stale | Risolto (già segnato nel CLAUDE.md) |
| 4 | Export shared Encode/Thread | Risolto: la build shared passa, 88/88 |
| 5 | Pacchetto aggregato senza Logging/Process/Zip | Aperto, non ricontrollato in questa sessione |
| 6 | Chiave JWT hardcoded `"HiAll"` | Aperto: `modules/JWT/src/JWTHandler.cpp` righe 35 e 66 |
| 7 | Dipendenza `Utility → Thread` mancante in root CMake | Aperto: `CMakeLists.txt` dichiara solo `XML → Utility` |
| 8 | Placeholder `OWNER` in HOMEPAGE_URL; notice spdlog/libzip mancanti | Aperto: `CMakeLists.txt` riga 6; `THIRD_PARTY_NOTICES.md` non cita spdlog/libzip |
| 9 | Piattaforme e varianti non verificate | Aperto, invariato |

## Moduli

| Modulo | Stato build | Stato test | Note |
| ------ | ----------- | ---------- | ---- |
| Utility | OK | OK | Incluso nei 88/88 e nei 116/116 |
| JWT | OK | OK | Chiave hardcoded (problema 6) |
| XML | OK | OK (116/116) | Richiede libxml2 sottomodulo |
| Encode | OK | OK | Export shared corretto |
| Logging | OK | OK | Build standalone non ricontrollata |
| Process | OK | OK | Test anche in variante `forkpath` |
| Thread | OK | OK | Target `TK4CPP::Thread`; il nome `ThreadPool` è solo in README |
| Zip | Non verificato | Non verificato | Richiede libzip ≥ 1.11 installato |
