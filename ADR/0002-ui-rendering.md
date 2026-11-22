# ADR-0002: Собственный UI на Direct2D / DirectWrite

- Статус: Принято
- Дата: 2026-11-22
- Связано с: ADR-0001, ADR-0003, ADR-0012

## Контекст
Стандартные контролы Win32 (comctl32) плохо поддерживают тёмную тему на Windows 10 и выглядят устаревше. XAML Islands и WinUI тяжёлые. Нужен "очень красивый" тёмный интерфейс, плавный и при этом нулевая нагрузка в простое.

## Решение
Свой лёгкий retained-mode тулкит поверх Direct2D.

**Окно**
- Одно главное окно (`WS_OVERLAPPEDWINDOW`), своя отрисовка заголовка через `WM_NCCALCSIZE` / `WM_NCHITTEST`. Кнопки свернуть/развернуть/закрыть свои, с поддержкой Snap Layouts на Win11 (возврат `HTMAXBUTTON` при наведении).
- Тёмный заголовок и рамка: `DwmSetWindowAttribute(DWMWA_USE_IMMERSIVE_DARK_MODE)`. Скругления Win11: `DWMWA_WINDOW_CORNER_PREFERENCE`. Фон Mica на Win11 22H2+: `DWMWA_SYSTEMBACKDROP_TYPE`, на Win10 сплошной фон.
- DPI: Per-Monitor V2 в манифесте, пересчёт метрик на `WM_DPICHANGED`.
- Одна копия приложения (именованный mutex). Повторный запуск активирует уже открытое окно.

**Рендеринг**
- D3D11 device (аппаратный, при сбое WARP) + DXGI flip-model swap chain + `ID2D1DeviceContext`. Композиция через DirectComposition, чтобы не было мерцания при ресайзе и работал прозрачный фон под Mica.
- Рисуем только по запросу (invalidate). Пока нет анимаций и ввода, CPU 0%, GPU 0%.
- Анимации: кадровые часы включаются только на время анимации, синхронизация с vsync через `DXGI WaitForVBlank` / `DwmFlush`.
- Обработка потери устройства (`DXGI_ERROR_DEVICE_REMOVED`): пересоздание ресурсов без потери состояния.
- Тексты: кеш `IDWriteTextLayout` по строке+стилю, инвалидация при смене DPI/языка.

**Тулкит (минимальный набор виджетов)**
Panel/Stack (row/column, gap, padding, fill, align), ScrollView с инерционной прокруткой, Label, Button, IconButton, Toggle, Checkbox, Segmented control, TextBox, AmountBox (ввод суммы с калькулятором), DatePicker, Dropdown/ComboBox с поиском, ColorPicker (палитра), IconPicker, VirtualList (виртуализация строк, 100 000+ элементов без лагов), Table, Tabs, Sidebar, Dialog/Sheet, Toast, ContextMenu, Tooltip, Charts (Donut, Bars, Line, Sparkline, Progress).

**Ввод**
- Мышь, колесо, тачпад (высокоточная прокрутка), touch/pen через `WM_POINTER`.
- Клавиатура: полный фокус-обход Tab/Shift+Tab, горячие клавиши (ADR-0003).
- TextBox свой: `WM_CHAR` (кириллица и любые раскладки работают), выделение, буфер обмена, undo внутри поля, IMM32 для позиционирования окна композиции IME.

**Доступность**
Базовый провайдер UI Automation для экранных дикторов: этап 2 (не MVP).

## Рассмотренные варианты
- comctl32 + owner-draw: тёмная тема на Win10 частично недокументирована, качество отрисовки ниже.
- GDI/GDI+: нет нормального сглаживания и аппаратного ускорения.
- XAML Islands / WinUI 3: тяжёлые рантаймы.

## Последствия
- Полный контроль над видом и скоростью, одинаковый вид на Win10 и Win11.
- Нужно аккуратно реализовать TextBox и фокус; это самая трудоёмкая часть тулкита.
- Скриншот-тесты экранов (ADR-0016) обязательны, иначе регрессии вида не поймать.
