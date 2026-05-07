# Интерактивные UI элементы для Pixel Graphics

## Обзор

Графическая библиотека `pixelgraphics.tlib` теперь содержит полный набор интерактивных элементов интерфейса с пиксельной графикой:

- **Пиксельный текст** - буквы, цифры, спецсимволы
- **Кнопки** - интерактивные с границами и текстом
- **Поля ввода** - для текстового ввода
- **Прямоугольники** - заполненные и с границами
- **Ярлыки** - статический текст

Все элементы поддерживают **масштабирование** (scale parameter).

## Функции текста

### `DRAWTEXT` - Рисование текстовой строки

```tlang
CALL DRAWTEXT CANVAS X Y TEXT COLOR SCALE
```

**Параметры:**
- `CANVAS` - объект окна/холста
- `X`, `Y` - координаты верхнего левого угла
- `TEXT` - строка текста для рисования
- `COLOR` - числовой код цвета (используйте GRAPHICSCOLOR или GRAPHICSHEX)
- `SCALE` - масштаб (1 = минимальный размер, 2 = в два раза больше и т.д.)

**Пример:**
```tlang
VARIABLE COLOR NUMBER RETURNS CALL GRAPHICSWHITE END LINE
CALL DRAWTEXT WINDOW 50 100 TEXT START Hello World TEXT END COLOR 2 END LINE
```

### `DRAWPIXELCHAR` - Рисование одного символа

```tlang
CALL DRAWPIXELCHAR CANVAS X Y CHAR COLOR SCALE
```

**Параметры:**
- `CANVAS` - объект окна
- `X`, `Y` - координаты
- `CHAR` - один символ
- `COLOR` - цвет
- `SCALE` - масштаб

## Функции кнопок

### `DRAWBUTTON` - Рисование кнопки

```tlang
CALL DRAWBUTTON CANVAS X Y WIDTH HEIGHT LABEL TEXTCOLOR BGCOLOR BORDERCOLOR SCALE
```

**Параметры:**
- `CANVAS` - объект окна
- `X`, `Y` - координаты верхнего левого угла
- `WIDTH`, `HEIGHT` - размеры кнопки в пиксельных единицах
- `LABEL` - текст на кнопке
- `TEXTCOLOR` - цвет текста
- `BGCOLOR` - цвет фона кнопки
- `BORDERCOLOR` - цвет границы кнопки
- `SCALE` - масштаб элемента

**Пример:**
```tlang
VARIABLE BLUE NUMBER RETURNS CALL GRAPHICSHEX TEXT START 3498DB TEXT END END LINE
VARIABLE WHITE NUMBER RETURNS CALL GRAPHICSWHITE END LINE
VARIABLE DARK NUMBER RETURNS CALL GRAPHICSDARKGRAY END LINE

CALL DRAWBUTTON WINDOW 50 100 150 50 TEXT START Click Me TEXT END WHITE BLUE DARK 2 END LINE
```

### `ISINRECT` - Проверка клика по кнопке

```tlang
VARIABLE CLICKED BOOL RETURNS CALL ISINRECT MOUSEX MOUSEY X Y WIDTH HEIGHT END LINE
```

Возвращает `1` если точка (MOUSEX, MOUSEY) находится внутри прямоугольника, иначе `0`.

## Функции полей ввода

### `DRAWINPUTFIELD` - Рисование поля ввода

```tlang
CALL DRAWINPUTFIELD CANVAS X Y WIDTH HEIGHT VALUE TEXTCOLOR BGCOLOR BORDERCOLOR SCALE
```

**Параметры:**
- `CANVAS` - объект окна
- `X`, `Y` - координаты
- `WIDTH`, `HEIGHT` - размеры поля
- `VALUE` - текущее значение/плейсхолдер
- `TEXTCOLOR` - цвет текста
- `BGCOLOR` - цвет фона
- `BORDERCOLOR` - цвет границы
- `SCALE` - масштаб

**Пример:**
```tlang
CALL DRAWINPUTFIELD WINDOW 50 200 300 50 TEXT START Enter name TEXT END 
                    BLACK WHITE CYAN 2 END LINE
```

## Функции ярлыков и прямоугольников

### `DRAWLABEL` - Статический текст

```tlang
CALL DRAWLABEL CANVAS X Y LABEL COLOR SCALE
```

Простой способ нарисовать текст без сложного форматирования.

### `DRAWRECT` - Заполненный прямоугольник

```tlang
CALL DRAWRECT CANVAS X Y WIDTH HEIGHT COLOR
```

### `DRAWRECTBORDER` - Прямоугольник с границей

```tlang
CALL DRAWRECTBORDER CANVAS X Y WIDTH HEIGHT COLOR THICKNESS
```

Рисует прямоугольник с внутренней границей заданной толщины.

## Поддерживаемые символы

### Буквы
A-Z (латинские)

### Цифры
0-9

### Спецсимволы
- Пробел ` `
- Точка `.`
- Запятая `,`
- Дефис `-`
- Восклицание `!`
- Вопрос `?`
- Двоеточие `:`
- Точка с запятой `;`

## Встроенные цвета

```tlang
CALL GRAPHICSWHITE END LINE      TEXT START FFFFFF TEXT END
CALL GRAPHICSBLACK END LINE      TEXT START 000000 TEXT END
CALL GRAPHICSRED END LINE        TEXT START FF0000 TEXT END
CALL GRAPHICSGREEN END LINE      TEXT START 00FF00 TEXT END
CALL GRAPHICSBLUE END LINE       TEXT START 0000FF TEXT END
CALL GRAPHICSGRAY END LINE       TEXT START 808080 TEXT END
CALL GRAPHICSDARKGRAY END LINE   TEXT START 404040 TEXT END
CALL GRAPHICSYELLOW END LINE     TEXT START FFFF00 TEXT END
CALL GRAPHICSCYAN END LINE       TEXT START 00FFFF TEXT END
```

Или используйте произвольные цвета через HEX:
```tlang
VARIABLE CUSTOMCOLOR NUMBER RETURNS CALL GRAPHICSHEX TEXT START 2ECC71 TEXT END END LINE
```

## Примеры использования

### Простой интерфейс с кнопками

```tlang
INCLUDE TEXT START pixelgraphics.tlib TEXT END

VARIABLE WINDOW NUMBER RETURNS CALL GRAPHICSWINDOW TEXT START My App TEXT END 600 400 END LINE

CALL GRAPHICSCLEAR WINDOW END LINE
CALL DRAWLABEL WINDOW 20 20 TEXT START Welcome TEXT END (CALL GRAPHICSWHITE) 2 END LINE

VARIABLE BLUE NUMBER RETURNS CALL GRAPHICSHEX TEXT START 3498DB TEXT END END LINE
CALL DRAWBUTTON WINDOW 50 100 150 50 TEXT START Start TEXT END 
                (CALL GRAPHICSWHITE) BLUE (CALL GRAPHICSDARKGRAY) 2 END LINE

CALL DRAWBUTTON WINDOW 250 100 150 50 TEXT START Exit TEXT END 
                (CALL GRAPHICSWHITE) (CALL GRAPHICSRED) (CALL GRAPHICSDARKGRAY) 2 END LINE

CALL GRAPHICSSHOW WINDOW END LINE
LOOP CALL GRAPHICSWAIT DO END LOOP
```

### Форма с полями ввода

```tlang
CALL DRAWINPUTFIELD WINDOW 30 120 350 50 TEXT START Name TEXT END 
                    (CALL GRAPHICSBLACK) (CALL GRAPHICSWHITE) 
                    (CALL GRAPHICSCYAN) 2 END LINE

CALL DRAWINPUTFIELD WINDOW 30 200 350 50 TEXT START Email TEXT END 
                    (CALL GRAPHICSBLACK) (CALL GRAPHICSWHITE) 
                    (CALL GRAPHICSCYAN) 2 END LINE
```

## Советы по дизайну

1. **Масштабирование**: Используйте SCALE 1 для компактных интерфейсов, 2-3 для удобочитаемости
2. **Контрастность**: Выбирайте цвета, контрастирующие с фоном
3. **Равномерное расстояние**: Оставляйте одинаковые промежутки между элементами
4. **Группировка**: Располагайте связанные элементы рядом
5. **Цветовое кодирование**: Используйте разные цвета для разных типов кнопок (OK=зелёный, Cancel=красный, Action=синий)

## Примеры файлов

- `ui_elements.tlang` - демонстрация всех элементов интерфейса
- `calculator_ui.tlang` - полноценный пиксельный калькулятор с интерактивными кнопками

Откройте эти файлы, чтобы увидеть красивые примеры использования!
