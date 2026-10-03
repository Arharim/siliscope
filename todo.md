# План работ

Снимок после `ss.pre.no-unused-include` (ещё не закоммичен). Каталог — 192 правила. Suite: 224. `embedded-cpp`: 157 включено, 78 с чекером, 79 без. `embedded-c`: 149 / 61 / 88. Pump, те же 11 `.cpp`: 3939 срабатываний, no-checker 79.

Каждый срез: фикстуры, suite, `--list`, прогон тех же 11 файлов pump. `just fw` для замера шума не использовать. Шум FreeRTOS, CMSIS и SEGGER оставляем: правила не ослабляем и отдельный путь для вендора не заводим. `ruleset/INDEX.md` и `ruleset/coverage.md` руками не правим. Коммит по просьбе, один срез за раз.

Baseline и подавление шире `--allow rule:name` в эту очередь не входят, пока отчёт ещё меряют, а не разбирают. Место сравнения двух логов — шаг 4. Место настоящего baseline — шаг 14.

## Уже закрыто

- Семейство `ss.cpp.*` (21 правило).
- Препроцессор: `no-path-in-include`, `ifdef-same-file`, `no-keyword-macro`, `no-stringify-then-paste`, `include-guard`, `macro-parens`, `comment-tokens`, `prefer-inline`, `no-commented-code`, `no-unused-include`.
- Срез конверсий, switch, CFG (все пути возврата, недостижимый код, noreturn, пары critical section и irq-mask) и локальный callgraph внутри одной единицы трансляции: рекурсия, ISR не как обычная функция, лог из ISR. `ss.mem.no-heap-after-init` запрещает любой `malloc` / `new`.

## Очередь

### 1. `ss.pre.no-unused-include`

Сделано. Advisory, оба языка. `#include` в `.c` / `.cpp` использован, если файл пишет макрос, тип или объявление, которое этот include принёс, включая определение функции, объявленной в заголовке. Повторный include того же файла, который сторож уже пропустил, не использован. На pump дельта ровно 71, все в своих `.cpp`. `--allow` берёт имя include как написано, без кавычек и скобок.

### 2. `ss.pre.source-includes-own-header`

Узкое правило рядом с шагом 1. Только C: `foo.c` включает `"foo.h"` первым, чтобы заголовок был самодостаточным. Из `embedded-cpp` исключено.

### 3. `ss.pre.limited`

После двух узких правил. Широкая политика: include guard, `#include` и короткие макросы остаются, token paste, рекурсивные макросы и лес `#if` без комментария «зачем» — нет. На этот шаг рано, пока 1 и 2 не готовы.

### 4. Снимок синтаксического отчёта

Сразу перед dataflow, когда препроцессорный план уже закрыт и синтаксические срабатывания перестают прыгать на каждом срезе. Сравнить два лога pump, чтобы дельта следующего прогона была видна. Это ещё не подавление и не файл, который надо обновлять на каждом коммите.

### 5. Один dataflow-проход внутри единицы трансляции

Четыре правила, не все 15 вида `dataflow`:

- `ss.expr.uninit`
- `ss.mem.bounds`
- `ss.mem.no-dangling`
- `ss.ctrl.loop-bound` (в каталоге это `cfg`; до этого шага оно остаётся сторожем no-checker)

Проход внутри файла. Значение, вышедшее через вызов в другом файле, здесь ещё не прослеживается.

### 6. Граф вызовов между единицами трансляции

Связать вызовы одного запуска. Уже живые `ss.ctrl.no-recursion`, `ss.emb.isr-not-called` и `ss.emb.no-log-in-isr` начинают видеть калли в других `.c` / `.cpp`. Новых id у самого графа нет. `ss.conc.no-block-in-cs` и режим «куча только в init» от появления рёбер сами не включаются: это шаг 8.

После этого шага инструмент уже ловит на прошивке дефекты, которые синтаксис не видит. Каталог при этом открыт: живыми станут около 90 правил из 192; в `embedded-cpp` с чекером будет примерно 83 из 157 включённых (шаги 1, 3 и четыре правила шага 5; свой заголовок в этот профиль не входит).

### 7. Остаток dataflow на том же проходе

Двенадцать правил:

- `ss.mem.no-null-deref`
- `ss.mem.no-use-after-free`
- `ss.mem.no-overlap-copy`
- `ss.mem.string-room-for-nul`
- `ss.ptr.no-deref-one-past`
- `ss.ptr.same-array`
- `ss.libc.copy-fits-dest`
- `ss.conv.no-div-zero`
- `ss.conv.no-signed-overflow`
- `ss.expr.no-unseq`
- `ss.expr.fp-must-be-finite`
- `ss.ctrl.no-invariant-condition`

### 8. Политики поверх графа

- `ss.conc.no-block-in-cs`: не блокироваться, пока критическая секция или маска прерываний ещё открыта.
- Поздний режим `ss.mem.no-heap-after-init`: выделение только в фазе init, в том числе через вызов в другом файле. Сплошной запрет `malloc` / `new` до этого остаётся как есть.

### 9. Межпроцедурный dataflow

Тот же граф, значения через вызов: uninit, null, границы. Это продолжение шагов 5 и 7, отдельное от самого графа.

### 10. Таблица символов на весь запуск

Драйвер уже принимает несколько файлов. Граф вызовов эту таблицу не заменяет. На неё садятся:

- `ss.fn.single-definition`
- `ss.fn.header-decl`
- `ss.fn.param-names-consistent`
- `ss.decl.extern-in-one-header`

### 11. Остальные types, затем syntax

Сначала типы и узкие проверки, которые ещё default on:

- Остаток `ss.conv.*`: `signed-unsigned-mix`, `no-unsigned-wrap-const`, `no-unary-minus-unsigned`, `no-object-ptr-cast`, `no-fn-ptr-cast`, `no-fp-narrow`, `fp-to-int-explicit`, `no-int-to-enum`. `no-mixed-categories` остаётся выключенным до профиля `strict`.
- Указатели и размер кадра: `no-ptr-arith`, `no-void-arith`, `array-arg`, `max-indirection`, `sizeof-array-param`, `no-large-frame`. `no-fn-pointer` остаётся advisory.
- Объявления, условия, инициализация: `decl.no-unused`, `decl.min-scope`, `decl.align-same`, `boolean-condition`, `no-fp-loop-counter`, `aggregate-init`, `inline-is-static`.
- Libc: `memcmp-not-for-c-strings`, `ctype-uchar-or-eof`.
- Типы: `fixed-width`, `bool`, `bitfield`, `enum-init`, `no-bool-math`, `explicit`, `no-restrict`, `array-size`. `no-union` и `no-fp-unless-needed` остаются advisory; сплошного запрета union нет.

Потом syntax, который ещё default on и без чекера: `isr-marked`, `layout-static-assert`, `localize-extensions`, `no-errno-host`, `no-trigraphs`, `init-side-effect-free`, `decl.no-reserved`, `decl.one-per-line`. Advisory (`max-if-nesting`, `named-loop-limit`, `max-length`, `max-params`, `parens`, `named-constants`, `no-time`, `no-tgmath-fenv`, `seq-cst-or-documented`) — после default on. Десять `ss.style.*` — в самом конце, только вместе с профилем `style`.

### 12. Остаток cfg

`for-well-formed`, `no-dead-code`, `no-unused-label`, `cyclomatic`. `single-break-in-loop` advisory, после них. `ss.fn.single-exit` остаётся `default: off`: единственный `return` внизу функции не требуем.

### 13. Review-подсказки

Девять правил вида `review`. Это замечание для ревью, не доказательство:

- `ss.conc.no-data-race`
- `ss.conc.lock-order`
- `ss.conc.no-volatile-mutex`
- `ss.decl.volatile`
- `ss.emb.default-isr`
- `ss.emb.isr-body`
- `ss.fn.check-params`
- `ss.type.hw-layout`
- `ss.type.no-representation-pun`

### 14. Разбор отчёта

Когда граф уже есть и отчёт начинают разбирать, а не только считать. Baseline / diff и подавление шире, чем `--allow rule:name`: файл или сохранённый набор. Правила при этом не ослабляем и особый путь для FreeRTOS, CMSIS и SEGGER не появляется.

## Вне очереди

- `ss.proc.all-warnings` и `ss.proc.static-analysis` — политика сборки, не чекер исходника.
- Из `ruleset/coverage.md`: сплошной запрет union, сплошной запрет указателя на функцию, обязательный единственный `return`, host POSIX и format-string, чужая алгебра essential type, требование исключений, кучи или запрета статической памяти, сертификационная бумага.
