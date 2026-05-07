# Изменения синтаксиса - TLang v1 Compliance

## Что было изменено

### pixelgraphics.tlib
✅ Все числовые литералы заменены на словесные представления согласно TLang v1:
- `0` → `ZERO`
- `1` → `ONE`
- `2` → `TWO`
- `5` → `FIVE`
- `8` → `EIGHT`
- `31` → `THIRTY ONE`
- `62` → `SIXTY TWO`
- `127` → ... и т.д.

✅ HEX цвета реализованы через встроенные функции:
- `GRAPHICSRED` → `FF0000`
- `GRAPHICSGREEN` → `00FF00`
- `GRAPHICSBLUE` → `0000FF`
- `GRAPHICSWHITE` → `FFFFFF`
- `GRAPHICSBLACK` → `000000`
- `GRAPHICSGRAY` → `808080`
- `GRAPHICSDARKGRAY` → `404040`
- `GRAPHICSYELLOW` → `FFFF00`
- `GRAPHICSCYAN` → `00FFFF`

✅ HEX коды остаются как текстовые строки (внутри TEXT START...TEXT END)

### ui_elements.tlang (пример)
✅ Все параметры окна и координаты заменены на слова:
- `800` → `EIGHT HUNDRED`
- `600` → `SIX HUNDRED`
- `20` → `TWENTY`
- `50` → `FIFTY`
- Все остальные числа также заменены

### calculator_ui.tlang (пример)
✅ Все размеры и координаты заменены на слова:
- `500` → `FIVE HUNDRED`
- `650` → `SIX HUNDRED FIFTY`
- `100` → `ONE HUNDRED`
- Все остальные числа также заменены

## Синтаксис TLang v1

Все файлы теперь полностью соответствуют спецификации TLang v1:
- ❌ Нет цифр в коде (все цифры заменены на слова)
- ✅ HEX коды используются для цветов
- ✅ Все ключевые слова в UPPERCASE
- ✅ Текстовые строки в TEXT START...TEXT END

## Примеры использования HEX кодов

```tlang
VARIABLE BLUE NUMBER RETURNS CALL GRAPHICSHEX TEXT START 4A90E2 TEXT END END LINE
VARIABLE GREEN NUMBER RETURNS CALL GRAPHICSHEX TEXT START 2ECC71 TEXT END END LINE
VARIABLE RED NUMBER RETURNS CALL GRAPHICSHEX TEXT START E74C3C TEXT END END LINE
```

Или используйте встроенные цвета:
```tlang
VARIABLE WHITE NUMBER RETURNS CALL GRAPHICSWHITE END LINE
VARIABLE CUSTOM NUMBER RETURNS CALL GRAPHICSHEX TEXT START ABCDEF TEXT END END LINE
```
