# TK4CPP — Roadmap e prossimi passi

Ordinato per priorità. Ogni voce indica il problema di origine e il criterio di chiusura.
Ultimo aggiornamento: 2026-11-19.

## Priorità alta

1. **Dipendenza `Utility → Thread` nel root CMake** (problema 7)
   - Aggiungere `set(TK4CPP_MODULE_DEPENDS_UTILITY Thread)` in `CMakeLists.txt`.
   - Chiusura: con `TK4CPP_BUILD_THREAD=OFF` e `TK4CPP_BUILD_UTILITY=ON`, la configurazione fallisce con un messaggio chiaro invece di un errore di link.
2. **Chiave JWT iniettabile** (problema 6)
   - Rimuovere `"HiAll"` da `JWTHandler.cpp` e aggiungere un setter o un parametro.
   - Chiusura: nessun segreto nel sorgente; test di firma/verifica passano con chiave esterna.

## Priorità media

3. **Placeholder metadati** (problema 8)
   - Sostituire `OWNER` in `HOMEPAGE_URL` con l'owner reale; aggiungere spdlog e libzip a `THIRD_PARTY_NOTICES.md`.
4. **Pacchetto aggregato per Logging, Process, Zip** (problema 5)
   - Chiusura: `find_package(TK4CPP REQUIRED COMPONENTS Logging)` funziona senza impostare `*_DIR`.
5. **Verifica di Zip** (problema 9)
   - Installare libzip ≥ 1.11 e lanciare `zip_tests`, `zip_highlevel_tests`.

## Priorità bassa

6. Verifica su Clang, C++20 e sanitizer (non ancora eseguita).
