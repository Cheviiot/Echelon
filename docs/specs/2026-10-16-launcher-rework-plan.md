# План реализации: переработка лаунчера Echelon

> **Для агентов-исполнителей:** план разбит на фазы и задачи с чекбоксами (`- [ ]`).
> Каждая задача = атомарный коммит. Дерево обязано оставаться собираемым
> после каждого коммита. Спека: `docs/specs/2026-10-16-launcher-rework-design.md`.

**Цель:** полная замена монолитного лаунчера на слоистую реализацию
(Core → Domain → Content → Engine → Ui → App) с сохранением всех внешних
контрактов (данные `~/.Echelon`, CLI, stderr-маркеры, ABI движка).

**Архитектура:** см. спеку; зависимости строго вниз, `App` — единственная
точка связывания. Порядок фаз = порядок слоёв; каждая фаза даёт
работающий, тестируемый результат.

**Стек:** C++17, CMake (presets `linux64-deploy` / `macos-vulkan`), SDL3
(FetchContent, таргет `sdl3lib`), Dear ImGui v1.92.3 (новый FetchContent
pin; бэкенды `imgui_impl_sdl3` + `imgui_impl_sdlrenderer3` — проверены на
теге), libarchive (`archive_static`), OpenSSL::Crypto.

**Валидация:** локального тулчейна на рабочей станции нет — основной цикл
«push → CI». Задачи сборки/тестов ниже дают команды для восстановленного
окружения (см. Задачу 0).

---

## Фаза 0. Предпосылки

### Задача 0: Восстановить локальное окружение сборки

**Файлы:** нет (инфраструктура).

- [ ] Восстановить Distrobox `ubuntu-dev` по foundation-отчёту
  (vcpkg в `~/.generalsx/vcpkg`, GCC 13+, CMake 3.28+, Ninja) либо
  подтвердить альтернативный валидный хост. Проверка: успешный
  `cmake --preset linux64-deploy -DRTS_BUILD_UNIVERSAL_LAUNCHER=ON`.
- [ ] Коммитов нет. Без этой задачи все сборочные шаги ниже выполняются
  только через CI (медленный цикл).

### Задача 1: Сделать `main` зелёным после синка (решение E)

**Файлы:**
- Modify: аудио-CMake и источники под `GeneralsX/` (точный список — по
  логам упавших CI-джоб macOS/linux)
- Modify: `GeneralsX/Core/GameEngineDevice/...` — точки, где наши старые
  аудио-адаптеры пересекаются с апстримовым MiniAudio

- [ ] Снять наш OpenAL-бэкенд: удалить адаптеры OpenAL из device-слоя,
  оставить апстримовый MiniAudio как единственный аудио-бэкенд (upstream
  #358 принят как есть; апстрим в аудио не оспариваем).
- [ ] Устранить остаточные ошибки сборки от реструктуризации Core
  (перенос W3D и др.) по логам CI: `gh run view -R Cheviiot/Echelon
  --log-failed` для упавших джоб.
- [ ] Проверка: CI «Echelon CI» на `main` — все джобы green (linux64-deploy,
  macos-vulkan, flatpak, windows).
- [ ] Коммит: `fix(audio): align audio stack with upstream MiniAudio`
  (+ отдельные `fix(build): ...` при необходимости).

---

## Фаза 1. Core-слой

Дерево файлов (создаётся всё сразу в Задаче 2, наполняется по задачам):

```
Launcher/Core/
  CMakeLists.txt
  Include/Echelon/Core/{Result.h, OperationError.h, Logger.h,
                        StringUtils.h, IniDocument.h, FileUtils.h, FsLock.h}
  Source/{Logger.cpp, StringUtils.cpp, IniDocument.cpp, FileUtils.cpp,
          FsLock.cpp}
  Tests/{CMakeLists.txt, TestKit.h,
         ResultTests.cpp, StringUtilsTests.cpp, IniDocumentTests.cpp,
         FileUtilsTests.cpp, FsLockTests.cpp}
```

### Задача 2: Скелет Core + CMake + TestKit

**Файлы:** создать всё дерево выше (заголовки-скелеты с guarded TODO —
нет, по правилам без заглушек: объявления без реализаций не собираются;
вместо этого создаём только CMake + TestKit + пустые translation units).

- [ ] `Launcher/Core/CMakeLists.txt`: статик-lib `echelon_core`
  (`cxx_std_17`, инклюд `Include/`, линк `OpenSSL::Crypto`), исполняемый
  `echelon_core_tests` из `Tests/*.cpp` + `add_test(NAME echelon_core ...)`.
- [ ] `Launcher/Tests/TestKit.h`: микро-харнесс — макросы
  `CHECK(cond)`, `CHECK_EQ(a,b)`, `CHECK_THROWS(expr)` со счётчиком
  падений и `main()`-репортом (по образцу старого
  `LauncherSettingsTests.cpp`, но общий).
- [ ] Подключить `add_subdirectory(Core)` в `Launcher/CMakeLists.txt`
  (выше старых таргетов — они не трогаются).
- [ ] Проверка: `cmake --preset linux64-deploy && cmake --build
  build/linux64-deploy --target echelon_core_tests && ctest --test-dir
  build/linux64-deploy -R echelon_core` → PASS (пустой набор).
- [ ] Коммит: `build(launcher): add core layer scaffold with test harness`.

### Задача 3: OperationError + Result

**Файлы:**
- Create: `Launcher/Core/Include/Echelon/Core/OperationError.h`
- Create: `Launcher/Core/Include/Echelon/Core/Result.h`
- Test: `Launcher/Core/Tests/ResultTests.cpp`

- [ ] Тест (`ResultTests.cpp`): успех хранит значение; ошибка хранит
  `stage/code/retryable`; `Status` (=`Result<void>`); цепочка
  `Map` не вызывается при ошибке.
- [ ] `OperationError.h`: `enum class ErrorStage { None, Discovery,
  Validation, IO, Archive, Hash, Engine, Internal }`; коды перенести из
  `Launcher/LauncherDiagnostics.h` (значения сохранить — логи/QA);
  `struct OperationError { ErrorStage stage; ErrorCode code; bool
  retryable; std::string detail; std::string Message() const; }`.
- [ ] `Result.h`: `template<typename T> class Result` с фабриками
  `Ok/Err`, `bool HasValue()`, `T& Value()`, `const OperationError&
  Error()`, `template<typename U> Result<U> Map(std::function<U(T&)>)`.
- [ ] Проверка: ctest `echelon_core` → PASS.
- [ ] Коммит: `feat(launcher-core): add unified Result and OperationError`.

### Задача 4: StringUtils + Logger

**Файлы:**
- Create: `Launcher/Core/{Include/Echelon/Core/StringUtils.h, Source/StringUtils.cpp}`
- Create: `Launcher/Core/{Include/Echelon/Core/Logger.h, Source/Logger.cpp}`
- Test: `Launcher/Core/Tests/StringUtilsTests.cpp`

- [ ] Тест: Trim/ToLower/EqualsNoCase/StartsWith/EndsWith/Split (пустые
  поля, множественные разделители)/Join/Slugify (кириллица проходит
  неизменной, спецсимволы → `-`).
- [ ] Реализация — одна на весь проект; поведение `Slugify` копировать из
  `Launcher/LauncherInstaller.cpp` (текущий канон).
- [ ] `Logger.h`: `enum class LogLevel { Info, Warning, Error }`,
  `void Log(LogLevel, std::string_view component, const char* fmt, ...)`
  → `fprintf(stderr)+fflush` с префиксом `[component]`; макросы
  `ECHELON_LOG_INFO/WARN/ERROR`.
- [ ] Коммит: `feat(launcher-core): add string utils and logger facade`.

### Задача 5: IniDocument

**Файлы:**
- Create: `Launcher/Core/{Include/Echelon/Core/IniDocument.h, Source/IniDocument.cpp}`
- Test: `Launcher/Core/Tests/IniDocumentTests.cpp`

- [ ] Тест: парсинг секций/ключей/значений с пробелами и `;`комментариями;
  регистронезависимость ключей (по поведению `LauncherSettings.cpp`);
  round-trip (parse→serialize→parse, эквивалентны); неизвестные ключи
  сохраняются; `SchemaVersion` доступен как обычный ключ.
- [ ] API: `class IniDocument { bool Load(path, OperationError&);
  bool Save(path, OperationError&) const; std::string Get(section, key,
  def="") const; void Set(section, key, value); bool Has(section,key);
  std::vector<std::string> Sections() const; Keys(section); Erase(section,
  key); }` — порядок секций/ключей сохраняется (deque/map с порядком).
- [ ] Коммит: `feat(launcher-core): add single INI document model`.

### Задача 6: FileUtils (атомарность + дайджест + безопасность путей)

**Файлы:**
- Create: `Launcher/Core/{Include/Echelon/Core/FileUtils.h, Source/FileUtils.cpp}`
- Test: `Launcher/Core/Tests/FileUtilsTests.cpp`

- [ ] Тест: `IsInsideRoot` (включая `..`, symlink-escape — решение
  копировать из `Launcher/LauncherInstaller.cpp:124` как канон);
  `AtomicWriteFile` (temp-файл в том же каталоге, `fsync` файла и
  каталога, rename; после «сбоя» (не удалён temp) повторная запись чистит
  temp); `Sha256HexFile` (совпадает с `sha256sum` для тестового вектора);
  `ReadFile`/`NormalizePath`.
- [ ] API: перечисленные функции + `bool EnsureDir(path)`.
- [ ] Коммит: `feat(launcher-core): add atomic file io and path safety`.

### Задача 7: FsLock

**Файлы:**
- Create: `Launcher/Core/{Include/Echelon/Core/FsLock.h, Source/FsLock.cpp}`
- Test: `Launcher/Core/Tests/FsLockTests.cpp`

- [ ] Тест: перенести сценарии из `LauncherSettingsTests.cpp` (lock +
  owner-token, stale-detection по PID). Поведение копировать из
  `Launcher/LauncherLocks.cpp`.
- [ ] API: `class FsLock { bool Acquire(path, OperationError&);
  void Release(); bool IsLocked() const; static bool IsStale(path); }`.
- [ ] Коммит: `feat(launcher-core): add directory locks`.

---

## Фаза 2. Domain-слой

```
Launcher/Domain/
  CMakeLists.txt
  Include/Echelon/Domain/{Paths.h, Settings.h, LaunchProfile.h,
                          ModManifest.h, ModCatalog.h, Selection.h,
                          Workspace.h, Localizer.h}
  Source/{Paths.cpp, Settings.cpp, LaunchProfile.cpp, ModManifest.cpp,
          ModCatalog.cpp, Selection.cpp, Workspace.cpp, Localizer.cpp}
  Tests/{SettingsTests.cpp, LaunchProfileTests.cpp, ModCatalogTests.cpp,
         SelectionTests.cpp, WorkspaceTests.cpp, LocalizerTests.cpp}
```

### Задача 8: Paths + LaunchProfile

- [ ] `Paths`: все константы раскладки `~/.Echelon` из старого кода
  (Profiles/, Launcher/Settings.v3.ini, UserData/<engine>/,
  Mods/Installed/..., Mods/Profiles/<engine>.ini, .trash, .staging) —
  единственное место знания путей. Тест: конкатенации и нормализация.
- [ ] `LaunchProfile`: модель `LauncherProfileV1`, сериализация в
  `Profiles/{generals,zerohour}.ini` через IniDocument. Порт из
  `Launcher/LauncherMain.cpp` (`ReadProfile`, 294-343). Тесты: round-trip,
  значения по умолчанию, повреждённый файл → ошибка Validation.
- [ ] Коммит: `feat(launcher-domain): add paths and launch profiles`.

### Задача 9: Settings (модель + транзакционная публикация)

- [ ] Модель настроек — перенести поля/значения по умолчанию из
  `Launcher/LauncherSettings.cpp`; сериализация в `Settings.v3.ini`
  (`SchemaVersion=3`); публикация пачкой через temp+rename (FileUtils).
- [ ] Тесты: перенести сценарии `echelon_settings_tests`, связанные с
  настройками (round-trip, defaults, concurrent .tmp/.bak — фикс из
  LauncherSettings.cpp:140-144 применить и в ModProfiles-логике).
- [ ] Коммит: `feat(launcher-domain): add transactional settings`.

### Задача 10: ModManifest + ModCatalog + Selection

- [ ] `ModManifest`: `manifest.ini` парсинг (тот же INI-движок), типы
  mod/patch/addon, semver-сравнение — порт из `Launcher/LauncherMods.cpp`.
- [ ] `ModCatalog`: сканирование `Mods/Installed/<engine>/...`,
  дедупликация без O(n²) (исправить 470-473 старого файла).
- [ ] `Selection`: стек `ModificationStack` из `LauncherModProfiles.cpp`;
  применить unique-temp фикс к `.tmp/.bak` (старый фикс туда не доехал).
- [ ] Тесты: из старого харнесса (загрузка каталога, резолвинг стека,
  конфликты версий).
- [ ] Коммит: `feat(launcher-domain): add mod catalog and selection`.

### Задача 11: Workspace

- [ ] Порт из `Launcher/LauncherWorkspace.cpp`: verify → materialize
  (hardlink, fallback copy) → атомарная публикация поколения;
  `Generation()` на `steady_clock`-совместимой логике (безопасность
  system_clock-сбоев); кварантин tampered-слоёв.
- [ ] Тесты: tampered workspace, malformed transaction, reuse/recovery —
  сценарии из старого `LauncherSettingsTests.cpp`.
- [ ] Коммит: `feat(launcher-domain): add content workspace`.

### Задача 12: Localizer

- [ ] Таблица строк EN/RU — статический массив msgid → {en, ru} вне
  кода логики; API `Localizer::Get(msgid, Language)`.
- [ ] Тесты: полнота (у каждого msgid оба языка), отсутствующий msgid →
  fallback на EN + `ECHELON_LOG_WARN`.
- [ ] Коммит: `feat(launcher-domain): add localization table`.

---

## Фаза 3. Content-слой

```
Launcher/Content/
  CMakeLists.txt
  Include/Echelon/Content/{TaskQueue.h, Progress.h, InstallOp.h,
                           MigrationOp.h, Recovery.h}
  Source/{TaskQueue.cpp, InstallOp.cpp, MigrationOp.cpp, Recovery.cpp}
  Tests/{TaskQueueTests.cpp, InstallOpTests.cpp, RecoveryTests.cpp}
```

### Задача 13: TaskQueue + Progress

- [ ] Один рабочий поток; состояния `queued/running/cancelling/done/
  failed`; `struct Progress { int64_t done, total; std::string step; }`;
  `CancelToken` (атомарный флаг + прогресс-атомики с `release/acquire`).
- [ ] Тесты: порядок выполнения, отмена на середине, прогресс
  монотонен, shutdown с активной задачей завершается по таймауту.
- [ ] Коммит: `feat(launcher-content): add task queue`.

### Задача 14: InstallOp

- [ ] Порт из `Launcher/LauncherInstaller.cpp` (1225 строк):
  распаковка libarchive с лимитом путей 1023 байта (aa80acca3), BIG
  валидация, SHA-256 через Core, staging → verify → атомарный rename →
  `.trash`, журналы `operation.ini` с owner PID.
- [ ] Тесты: архивы-фикстуры из старого харнесса (вложенные zip, BIG,
  tampered sha256, отмена).
- [ ] Коммит: `feat(launcher-content): add crash-safe install operation`.

### Задача 15: MigrationOp + Recovery

- [ ] `MigrationOp`: копирующий импорт retail-дерева — порт
  `Launcher/LauncherDataImport.cpp` + сценарии из `LauncherMain.cpp`
  (диалоги не нужны — операция чистая).
- [ ] `Recovery`: replay журналов `.trash`/`operation.ini` при старте —
  порт логики `LauncherInstaller.cpp` (924-1012).
- [ ] Тесты: прерванная установка → recovery; конфликтующий импорт →
  backup конфликтов.
- [ ] Коммит: `feat(launcher-content): add migration and recovery`.
- [ ] Переименовать ctest-агрегат: `echelon_core_tests`/`domain`+
  `content` тесты также регистрируются под именем `echelon_local_content`
  (совместимость со smoke-скриптами): `add_test(NAME
  echelon_local_content COMMAND echelon_domain_tests)` (проверить, что
  скрипты ждут именно имя, не бинарь).

---

## Фаза 4. Engine-слой

```
Launcher/Engine/
  CMakeLists.txt
  Include/Echelon/Engine/{ModuleLoader.h, SessionHost.h,
                           PresentationBridge.h, Supervisor.h}
  Source/{ModuleLoader.cpp, SessionHost.cpp, PresentationBridge.cpp,
          Supervisor.cpp}
  Tests/{SessionHostContractTests.cpp}   # headless: контракты/моки
```

### Задача 16: ModuleLoader + SessionHost

- [ ] `ModuleLoader`: `SDL_LoadObject`/resolve V2+V3 из
  `EngineIntegration/Include/LauncherIntegration/EngineModuleAPI.h`
  (заголовок не меняется); ошибки через Result/OperationError
  (никакого `error_code + 100` — маппинг явный).
- [ ] `SessionHost`: единый интерфейс `Run(config)`; внутри две ветки:
  V3 (Create→Prepare→Start→Step→Stop→Quiescent→Destroy, кадры качает
  хост) и V2 (блокирующий run + scoped env/cwd-мост). Порт из
  `Launcher/LauncherMain.cpp` (`RunProfile` 1840-2104) и
  `Launcher/EngineSession.cpp`.
- [ ] Тесты: мок-модуль (тестовый .so с двумя экспортами) — загрузка,
  версионирование через `struct_size`, quiescence-маска `0x7f`,
  teardown после failed frame.
- [ ] Коммит: `feat(launcher-engine): add module loader and session host`.

### Задача 17: PresentationBridge

- [ ] Порт из `Launcher/PresentationService.cpp` + hosted-хуков
  `W3DDisplay`: started/frame/stopping/quiescent/failed; **без лога на
  каждый кадр** (агрегированная статистика раз в N секунд).
- [ ] Тест: коллбэк-счётчики, порядок состояний.
- [ ] Коммит: `feat(launcher-engine): add presentation bridge`.

### Задача 18: Supervisor

- [ ] Порт `RunSupervisor` из `Launcher/LauncherMain.cpp` (2134-2193):
  worker-процесс (`--internal-ui-worker`), лимит 3 восстановления/мин,
  маркеры перезапуска в stderr.
- [ ] Тесты: headless-разбор аргументов + политика лимита (unit на
  счётчик окна); полный supervisor-сценарий — smoke `test-echelon-
  supervisor-wayland.sh` в Фазе 7.
- [ ] Коммит: `feat(launcher-engine): add supervisor`.

---

## Фаза 5. Ui-слой

```
Launcher/Ui/
  CMakeLists.txt
  Include/Echelon/Ui/{DesignSystem.h, Widgets.h, Screen.h,
                      MainScreen.h, SettingsScreen.h, ModsScreen.h,
                      InstallScreen.h, Resources.h}
  Source/{DesignSystem.cpp, Widgets.cpp, MainScreen.cpp,
          SettingsScreen.cpp, ModsScreen.cpp, InstallScreen.cpp,
          Resources.cpp}
```

### Задача 19: ImGui-зависимость + DesignSystem

- [ ] FetchContent pin: `ocornut/imgui` tag `v1.92.3`, собирать
  `imgui.cpp imgui_draw.cpp imgui_tables.cpp imgui_widgets.cpp
  backends/imgui_impl_sdl3.cpp backends/imgui_impl_sdlrenderer3.cpp` в
  статик-таргет `imguilib` (include-каталоги `imgui/` и `imgui/backends`).
- [ ] `DesignSystem`: шрифты (DejaVuSans.ttf + кириллические глифы),
  палитра Echelon, `ApplyStyle(ImGuiStyle&)`.
- [ ] Проверка: сборка таргета `imguilib` на обеих платформах (CI).
- [ ] Коммит: `build(launcher-ui): add imgui dependency and design system`.

### Задача 20: Screen-база + Resources

- [ ] `class Screen { virtual ~Screen(); virtual void HandleEvent(...);
  virtual void Render(AppState&) = 0; }` — `AppState` (в App-слое,
  forward-declare).
- [ ] `Resources`: кэш обложек (stb_image → SDL_Texture через renderer),
  звуки — порт из `LauncherMain.cpp` (792-849, 1018-1034) в виде класса.
- [ ] Коммит: `feat(launcher-ui): add screen base and resources`.

### Задача 21: Экраны (Main/Settings/Mods/Install)

- [ ] `MainScreen`: фон, логотип, кнопки запуска generals/zerohour,
  переходы — паритет со старым видом (скриншоты старого бинаря как
  референс).
- [ ] `SettingsScreen`: **декларативный список SettingDescriptor**
  `{id, labelId, type, get/set}` — новые настройки добавляются одной
  строкой, без switch-по-индексу.
- [ ] `ModsScreen`: карточки каталога, контексты folders/links,
  сохранение стека через Domain::Selection.
- [ ] `InstallScreen`: прогресс из Content::TaskQueue, отмена.
- [ ] Дисциплина id: `PushID(screenName)` на входе каждого экрана.
- [ ] Коммит: `feat(launcher-ui): add screens` (или по экрану, если
  разрастётся).

---

## Фаза 6. App-слой и swap

```
Launcher/App/
  CMakeLists.txt
  Include/Echelon/App/{Cli.h, Markers.h, AppState.h, App.h}
  Source/{Cli.cpp, Markers.cpp, App.cpp, main.cpp}
```

### Задача 22: Markers + Cli

- [ ] `Markers.h`: все `constexpr std::string_view`
  `[CONTENT-STACK]`, `[CONTENT-LAYER]`, `[WINDOW-HANDOFF]`,
  `[WINDOW-MODE]`, `[WORKER-RESOURCES]`, `[PRESENTATION-BRIDGE]`,
  `engine quiescence flags: 0x`, строки supervisor. Сверить дословно с
  `scripts/qa/smoke/*.sh` (grep'ать скрипты, не память).
- [ ] `Cli`: полный парсер старого контракта (`--launcher`,
  `--profile=`, `--mod/--patch/--addon`, `--no-mods`, `-headless`,
  `-quickstart`, `-noshellmap`, `--internal-test-*`, проброс прочего
  движку) — порт `LauncherMain.cpp` (2195-2260); структура
  `LaunchRequest`.
- [ ] Тесты: таблица кейсов (флаг → поля LaunchRequest), включая все
  `--internal-test-*`.
- [ ] Коммит: `feat(launcher-app): add cli contract and qa markers`.

### Задача 23: App + main + цикл кадра

- [ ] `App`: SDL init (Vulkan-фильтр ICD — порт 406-467),
  ImGui init/shutdown, роутинг экранов, wiring TaskQueue/SessionHost/
  Supervisor, graceful exit (join с таймаутом).
- [ ] `main()`: parse → headless-диспетч (маркер quiescence) или
  supervisor/worker UI.
- [ ] Коммит: `feat(launcher-app): add application shell`.

### Задача 24: Swap таргета и удаление старого

- [ ] `Launcher/CMakeLists.txt`: `echelon_launcher` → новые исходники
  (`App` + линк `echelon_ui echelon_engine echelon_content
  echelon_domain echelon_core imguilib`); **убрать `CURL::libcurl` и
  `yaml-cpp`** из линковки (OpenSSL остаётся, libarchive — только в
  content).
- [ ] Удалить старые файлы: `LauncherMain.cpp`, `LauncherInstaller.*`,
  `LauncherDownloads.*`, `LauncherMods.*`, `LauncherRepositories.*`,
  `LauncherS3.*`, `LauncherSettings.*`, `LauncherWorkspace.*`,
  `LauncherLocks.*`, `LauncherModProfiles.*`, `LauncherDataImport.*`,
  `EngineSession.*`, `PresentationService.*`, `LauncherLocalization.*`,
  `LauncherSettingsTests.cpp`, `LauncherDiagnostics.h` (перенесено).
- [ ] `echelon_settings_tests` → заменить агрегатом новых тестов под
  именем `echelon_local_content` (проверить smoke-скрипты).
- [ ] Проверка: `cmake --build build/linux64-deploy --target
  echelon_launcher echelon_settings_tests` + ctest `-R echelon` → green.
- [ ] Коммит: `refactor(launcher): replace monolith with layered
  implementation`.

---

## Фаза 7. QA-паритет и документы

### Задача 25: QA-границы и smoke

- [ ] `python3 scripts/qa/check-echelon-boundaries.py --build
  build/linux64-deploy` → PASS; `--standalone` тоже.
- [ ] Прогнать `scripts/qa/smoke/` на машине с Wayland (lifecycle,
  supervisor, headless dispatch, content stack, mod manager, quickstart,
  flatpak) с retail-фикстурами `ECHELON_QA_DATA_ROOT`. Править маркеры/
  флаги только синхронно со скриптами.
- [ ] Коммиты по факту: `fix(launcher-...)` / `test(launcher-...)`.

### Задача 26: macOS

- [ ] `./scripts/build/macos/bundle-macos-echelon.sh macos-vulkan` →
  бандл собирается, CI macOS green.

### Задача 27: Документы

- [ ] `docs/HOWTO/ECHELON_LAUNCHER.md` — обновить под новую архитектуру
  (CLI, маркеры, data layout).
- [ ] `docs/WORKLOG/2026-10-DIARY.md` — записи по ходу (возобновить
  дневник: раскладка и контракты, смена архитектуры, swap).
- [ ] Коммит: `docs: refresh launcher guide and worklog`.

---

## Самопроверка плана (по чек-листу blueprint)

- **Покрытие спеки:** A→Задачи 19-21; B→Задачи 2-24 (порядок слоёв) +
  24 (swap); C→все фазы; D→Задача 24 (удаление) + CMake; E→Задача 1;
  контракты→8-11, 22; тестирование→TestKit + фазы; QA→25-27. Пропусков
  нет.
- **Фантомные API:** ImGui v1.92.3 SDL3-бэкенды проверены по исходникам
  тега (`imgui_impl_sdl3.h`: `ImGui_ImplSDL3_InitForSDLRenderer`,
  `ImGui_ImplSDLRenderer3_*`); `sdl3lib`, `archive_static`,
  `OpenSSL::Crypto` — существующие таргеты; ABI-заголовок
  `EngineModuleAPI.h` — на месте.
- **Согласованность имён:** `echelon::core/domain/content/engine/ui/app`;
  `OperationError`/`Result`/`IniDocument`/`FsLock`/`TaskQueue`/
  `SessionHost`/`Markers`/`Cli` используются единообразно во всех фазах.
- **Осознанное отклонение от blueprint:** код механических портов
  (InstallOp 1225 строк и т.п.) в плане не дублируется — даются точные
  источник-ссылки и тестовая спецификация; старые файлы остаются в git
  до Задачи 24 и служат эталоном поведения.
