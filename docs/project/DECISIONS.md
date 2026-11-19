# TK4CPP — Registro delle decisioni

Ogni voce riporta data, decisione e motivazione. Le voci non si cancellano: si marcano come superate.

## Aperte

- **FolderHandler: metodo statico o istanza?** (problema 2 del CLAUDE.md)
  La questione è sorta perché `XML.h` chiamava `FolderHandler::create` come statico, mentre il metodo è diventato membro non statico.
  Stato: non serve più per il build (XML compila), ma la scelta di API resta aperta.
- **Chiave di firma JWT** (problema 6)
  Deve diventare iniettabile. Il modulo non va usato in produzione finché la chiave è hardcoded.

## Prese

- **2026-11-19 — Il tracciamento vive in `docs/project/`.**
  Motivazione: separare i file di stato dalla radice, accanto alla documentazione esistente.

## Superate

Nessuna.
