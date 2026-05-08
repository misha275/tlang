# Спецификация легковесного браузера (MVP)

## Overview

- **Purpose:** Лёгкий демонстрационный браузер для UI-экспериментов и обучения.
- **Audience:** Разработчики, тестировщики UI-рантайма и демонстрационные сценарии.
- **Scope (MVP):** Адресная строка, навигация (вперёд/назад/перезагрузить), простая отрисовка страниц (текст/упрощённый markup), история, кнопка `GO`, Enter в поле — навигация.

## Feature Set (MVP)

- **Address Bar:** однострочное поле ввода `ADDRESS_BAR` в верхней части окна.
- **Go Button:** кнопка `GO_BUTTON` рядом с `ADDRESS_BAR`, запускает навигацию по введённому URL.
- **Back/Forward:** кнопки `BACK_BUTTON` и `FORWARD_BUTTON`, манипуляция историей.
- **Reload:** `RELOAD_BUTTON` — перезагрузка текущей страницы.
- **Home:** `HOME_BUTTON` — переход на стартовую страницу.
- **Content Area:** `CONTENT_AREA` — основной блок для вывода страницы (plain text / minimal markup).
- **Status Bar:** `STATUS_BAR` — отображает статус загрузки/ошибок/URL.
- **Keyboard:** Enter в `ADDRESS_BAR` эквивалентен нажатию `GO_BUTTON`.
- **Simple Routing:** локальные виртуальные страницы (`home`, `search`, `docs`, `about`) + внешние URL схема (`http(s)://...`) — внешние URL могут быть симулированы в MVP.

## UI Layout

- **Top Row:** `ADDRESS_BAR` (растягиваемое), справа от него `GO_BUTTON`.
- **Toolbar:** строка с `BACK_BUTTON`, `FORWARD_BUTTON`, `RELOAD_BUTTON`, `HOME_BUTTON` и опциональными быстрыми ссылками.
- **Main Area:** `CONTENT_AREA` занимает остальное пространство окна.
- **Bottom Row:** `STATUS_BAR` с кратким статусом.
- **Responsiveness:** элементы масштабируются при изменении окна; `ADDRESS_BAR` занимает доступное пространство.

## Interactions & UX

- **Navigation Flow:** ввод → валидация → переход → обновление `CONTENT_AREA` и `STATUS_BAR` → запись в истории.
- **History Semantics:** список записей с указателем текущей позиции; при навигации через адрес или `GO` все «вперед» удаляются.
- **Reload:** перезапрос содержимого текущего URL без изменения истории.
- **Enter Handling:** нажатие `Enter` в `ADDRESS_BAR` вызывает ту же логику, что `GO_BUTTON`.
- **Focus Behavior:** `Ctrl+L` фокусирует `ADDRESS_BAR` и выделяет весь текст; `Esc` снимает фокус.
- **Input UX:** автодополнение не требуется в MVP; показывать минимальный inline валидатор (некорректный URL — подсказка в `STATUS_BAR`).
- **Visual States:** кнопки `BACK`/`FORWARD` отключены, если недоступны; `GO` активна, если поле не пустое.
- **Error Handling:** ошибки загрузки показывать в `STATUS_BAR` и в `CONTENT_AREA` коротким сообщением; подробности доступны в логах.

## Data Model

- **PageRecord:** { `url` (string), `content` (string), `title` (string, optional), `timestamp` }.
- **History:** list<PageRecord>, `currentIndex` (int).
- **Settings:** `homeUrl` (string), `startPage` (PageRecord), `userPreferences` (e.g., `fontSize`).
- **Session-only:** без постоянного хранения в MVP; опционально — локальное сохранение истории/настроек.

## Networking / Content

- **Modes:**
  - `LocalRouting` — обслуживает встроенные страницы (home, docs, search).
  - `ExternalFetch` — опциональный HTTP fetch; в MVP может быть мокирован.
- **Content Types Supported:** plain text, минимальный markup (заголовки, ссылки). Нет полного CSS/JS.
- **Security:** валидировать схему URL; по умолчанию запрещать `file://`; санитизировать отображаемый контент.
- **Timeouts / Errors:** конфигурируемый таймаут запросов; при ошибке показывать retry.

## APIs & Runtime Contracts

- **Рекомендованные runtime-функции:**
  - `open_url(string url)` → асинхронный результат или синхронная симуляция загрузки.
  - `get_content()` → возвращает текущий `content`.
  - `set_address_text(string)` / `get_address_text()` — управление текстом в `ADDRESS_BAR`.
  - `on_command(command_id)` — центральный диспетчер для действий кнопок.
- **Events:**
  - `on_navigation_start(url)`, `on_navigation_complete(url, success)`, `on_navigation_error(url, error)`.
- **Command IDs:** константы для кнопок и контролов для интеграции с рантаймом.

## Accessibility

- **Keyboard-first:** все основные действия доступны с клавиатуры.
- **ARIA-like Semantics:** метки для `ADDRESS_BAR`, `GO_BUTTON`, `CONTENT_AREA`, `STATUS_BAR`.
- **Focus Order:** логический порядок: `ADDRESS_BAR` → toolbar → `CONTENT_AREA`.
- **Contrast & Text Size:** учитывать пользовательские предпочтения; обеспечить контраст для статуса/ошибок.

## Keyboard Shortcuts

- `Enter` — submit `ADDRESS_BAR`.
- `Ctrl+L` — фокус и выделение `ADDRESS_BAR`.
- `Alt+Left` / `Alt+Right` — Back / Forward (опционально).
- `F5` / `Ctrl+R` — Reload.
- `Ctrl+H` — Open History (опционально).

## Non-functional Requirements

- **Performance:** быстрая маршрутизация локальных страниц; задержка навигации < 200 ms для встроенных страниц.
- **Portability:** целевой MVP — Windows; архитектура должна позволять портирование.
- **Simplicity:** минимальные зависимости; предпочтение одному исполняемому файлу.
- **Extensibility:** рантайм должен позволять добавлять обработчики страниц и swap network backends.

## Extensibility & Plugin Points

- **Custom Page Handlers:** возможность регистрировать `render(url) -> PageRecord`.
- **Network Layer Swap:** pluggable fetcher интерфейс для смены реализации (mock/local/external).
- **UI Themes:** поддержка простых tokens (background, text, accent).

## Acceptance Criteria & Tests

- **AC1 — Address Navigation:** ввод и нажатие Enter/GO обновляет `CONTENT_AREA`.
- **AC2 — History:** последовательность A→B→C, `BACK` возвращает к B, `FORWARD` к C.
- **AC3 — Reload:** `RELOAD` обновляет текущее содержимое.
- **AC4 — Disabled Buttons:** `BACK`/`FORWARD` недоступны при отсутствии истории.
- **AC5 — Error Display:** ошибки отображаются в `STATUS_BAR` и `CONTENT_AREA`.

**Тесты:** unit-тесты модели истории, интеграционные тесты навигационных сценариев, ручная проверка клавиатурных сценариев.

## Example User Flows

- **Navigate to URL:** фокус `ADDRESS_BAR` → ввод `http://example.com` → Enter → `on_navigation_start` → `CONTENT_AREA` показывает загрузку → `on_navigation_complete` → страница показана; запись в истории.
- **Use Back/Forward:** нажатие `BACK_BUTTON` → `currentIndex--` → обновление `CONTENT_AREA`.
- **Quick Address Focus:** `Ctrl+L` → фокус и выделение текста в `ADDRESS_BAR`.

## Minimal Wireframe (text)

Top: [BACK] [FORWARD] [RELOAD] [HOME]   [ADDRESS_BAR..........................][GO]

Middle: большая область для `CONTENT_AREA`

Bottom: `STATUS_BAR: ready`

---

Если нужно, могу:
1. Преобразовать спецификацию в набор задач (issues). 
2. Дать детальные примеры API и сигнатур для интеграции с рантаймом `tlang`.
3. Сгенерировать минимальную reference-реализацию на текущем рантайме.
