# ADR-0017: Структура репозитория

- Статус: Предложено
- Дата: 2026-11-22

## Решение
```
Finpulse/
  ADR/                  архитектурные решения
  docs/                 ARCHITECTURE, FEATURES, QUESTIONS, schema.sql, VERIFY-OFFLINE
  src/
    app/ platform/ ui/ screens/ services/ domain/ storage/ crypto/ i18n/
    resources/          иконка, манифест, .rc, path-данные иконок
  third_party/
    sqlite3mc/          амальгама + LICENSE + VERSION
    argon2/             эталонная реализация + LICENSE + VERSION
  tests/                unit, storage, crypto, fuzz, screenshots
  tools/                check_imports, генератор иконок, генератор тестовых данных
  CMakeLists.txt
  build.cmd  test.cmd  bench.cmd
  README.md
```
Git-репозиторий (по аналогии с MediaSense: `axionode/Finpulse`), приватный или публичный: под вопросом. Ветки: main + короткие feature-ветки, коммиты по Conventional Commits.
