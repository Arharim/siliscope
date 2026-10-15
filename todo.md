# План работ

Снимок после advisory syntax: вложенный if глубже двух уровней, голая граница цикла, тело длиннее 80 строк, больше восьми параметров, скобки в strict, число без имени в условии, задержке, размере буфера или поле протокола, time.h, tgmath.h и fenv.h, порядок атомика без комментария. Каталог — 192 правила. Suite: 454. `embedded-cpp`: 157 включено, 137 с чекером, 20 без. `embedded-c`: 149 / 130 / 19. `strict`: 176 / 154 / 22. Живых чекеров 155. Pump, те же 11 `.cpp`: 5383 срабатывания, дельта +833 относительно `out/pump-syntax.txt` и дельта +376 относительно `out/pump-syntax-rules.txt`. no-checker 20. Stderr совпал с прошлым: 0 fatal, 6 ошибок clang, 3884 предупреждения. Лог этого прогона: `out/pump-advisory.txt`. Замороженные `out/pump-syntax.txt`, `out/pump-syntax-rules.txt`, `out/pump-dataflow.txt`, `out/pump-callgraph.txt`, `out/pump-dataflow2.txt`, `out/pump-policies.txt`, `out/pump-interproc.txt`, `out/pump-symbols.txt`, `out/pump-conv.txt`, `out/pump-ptr.txt`, `out/pump-decl.txt`, `out/pump-libc.txt` и `out/pump-types.txt` не трогали, в git они не входят.

Каждый срез: фикстуры, suite, `--list`, прогон тех же 11 файлов pump. Дельту считать так: `python3 tools/diff_report.py out/pump-syntax.txt НОВЫЙ`. Сырой лог в разбор не тащить. `out/pump-syntax.txt` не подменять следующим прогоном. `just fw` для замера шума не использовать. Шум FreeRTOS, CMSIS и SEGGER оставляем: правила не ослабляем и отдельный путь для вендора не заводим. `ruleset/INDEX.md` и `ruleset/coverage.md` руками не правим. Коммит по просьбе, один срез за раз.

Baseline и подавление шире `--allow rule:name` в эту очередь не входят, пока отчёт ещё меряют, а не разбирают. Место сравнения двух логов — шаг 4. Место настоящего baseline — шаг 14.

## Уже закрыто

- Семейство `ss.cpp.*` (21 правило).
- Препроцессор: `no-path-in-include`, `ifdef-same-file`, `no-keyword-macro`, `no-stringify-then-paste`, `include-guard`, `macro-parens`, `comment-tokens`, `prefer-inline`, `no-commented-code`, `no-unused-include`, `source-includes-own-header` (только C, из `embedded-cpp` исключено), `limited`.
- Срез конверсий, switch, CFG (все пути возврата, недостижимый код, noreturn, пары critical section и irq-mask). Рекурсия, ISR не как обычная функция, лог из ISR, блокировка внутри секции и куча только в фазе init смотрят граф всего запуска.

## Очередь

### 1. `ss.pre.no-unused-include`

Сделано. Advisory, оба языка. `#include` в `.c` / `.cpp` использован, если файл пишет макрос, тип или объявление, которое этот include принёс, включая определение функции, объявленной в заголовке. Повторный include того же файла, который сторож уже пропустил, не использован. На pump дельта ровно 71, все в своих `.cpp`. `--allow` берёт имя include как написано, без кавычек и скобок.

### 2. `ss.pre.source-includes-own-header`

Сделано. Только C, из `embedded-cpp` исключено. `foo.c` включает свой заголовок первым: имя файла в кавычках равно `foo.h`, в том числе `"inc/foo.h"`. Угловые скобки не считаются. Если своего include нет, правило срабатывает только когда `foo.h` лежит рядом с `foo.c`. На pump дельта 0: профиль `embedded-cpp` это правило не включает. `--allow` называет `foo.h`.

### 3. `ss.pre.limited`

Сделано. Оба языка. Три сообщения одного id. `##` даёт «do not paste tokens» один раз на макрос; `#` (stringize) молчит. Имя макроса в списке замены, если это не параметр, даёт «do not write a recursive macro». Взаимная рекурсия не ищется. `#if`, `#ifdef`, `#ifndef`, `#elif`, `#elifdef` и `#elifndef` без непустого комментария на этой строке или на предыдущей дают «say why this condition is here». Пустой `//` не считается, строка кода с комментарием внутри тоже. Открывающий `#ifndef` классического сторожа молчит. `#pragma once` поздний `#ifndef` не освобождает, `#if !defined` сторожем не считается. `#else` и `#endif` молчат. `--allow` называет макрос и глушит paste вместе с рекурсией. Условие глушится комментарием. На pump дельта 611: 601 условие, 10 paste в `cmsis_os.h`, рекурсий 0. Больше всего в `FreeRTOS.h` (220). Stderr тот же, что у прошлого прогона.

### 4. Снимок синтаксического отчёта

Сделано. `tools/diff_report.py` сравнивает два текстовых лога и печатает дельту: какие диагностики появились и какие пропали, по правилу и по тексту, плюс толстые файлы и несколько строк. Подавления здесь нет. Повторный прогон тех же 11 `.cpp` совпал с логом после `limited` байт в байт, включая stderr: 4550 срабатываний, дельта 0. Копия лежит в `out/pump-syntax.txt`. Этот файл не переписывать на каждом срезе. Следующий прогон dataflow сравнивать с ним.

### 5. Один dataflow-проход внутри единицы трансляции

Сделано. Четыре правила, не все 15 вида `dataflow`: `ss.expr.uninit`, `ss.mem.bounds`, `ss.mem.no-dangling`, `ss.ctrl.loop-bound` (в каталоге это `cfg`). Один проход на функцию: четыре строки реестра анализ не повторяют. Внутри файла. Вызов непрозрачен, значение в другом файле не прослеживается.

Непроинициализированное чтение и запись вида `int x = x`. Постоянный индекс за известной границей массива, включая `int i = 4; a[i]`; переменный индекс без константы молчит. Адрес автоматического объекта, который возвращают или кладут в более долгую жизнь; адрес, переданный в вызов, молчит. Цикл без потолка: счётчик в условии `<` `<=` `>` `>=` `!=` и его `++` / `--` / `+=` / `-=`, либо `while (n) { --n; }`. `for (;;)` и `while (1)` без `break` / `return` / `goto` остаются суперциклом. `--allow` называет переменную, а у цикла — функцию.

На pump дельта +21, все в `main/tasks`: ожидание флага или статуса, `for (;;)` с `break`, разбор строки `while (*p == ' ')`. `uninit`, `bounds` и `no-dangling` на этих 11 файлах молчат. Сторож no-checker теперь `ss.mem.no-null-deref`.

### 6. Граф вызовов между единицами трансляции

Сделано. Новых id нет. Прямые вызовы одного запуска складываются в один граф после разбора всех файлов: узел — USR, у файлового `static` в ключе ещё и файл, чтобы одно имя в двух `.c` не склеилось. `ss.ctrl.no-recursion`, `ss.emb.isr-not-called` и `ss.emb.no-log-in-isr` смотрят этот граф, в том числе методы C++. Вызов через указатель ребром не становится. `ss.conc.no-block-in-cs` и режим «куча только в init» не включились: это шаг 8.

На pump дельта 0 относительно лога после dataflow. Три прежних `ss.emb.no-log-in-isr` в `can_task.cpp` остались на тех же строках. Рекурсии и вызова ISR как обычной функции по-прежнему нет. Счётчик чекеров тот же: живых 88 из 192, в `embedded-cpp` 83 из 157. `ss.pre.source-includes-own-header` в этот профиль не входит.

### 7. Остаток dataflow на том же проходе

Сделано. Те же двенадцать правил и тот же проход на функцию. Вызов непрозрачен. Отмечается доказанное нарушение, а не всякий непроверенный указатель, делитель или индекс. `--allow` называет функцию. Сторож no-checker теперь `ss.conv.signed-unsigned-mix`.

- `ss.mem.no-null-deref`: `*`, `->` и индекс, когда указатель точно null. `if (p)` и `if (p == 0) return` факт снимают. Адрес локальной, переданный в вызов, факт сбрасывает: вызываемый может её записать.
- `ss.mem.no-use-after-free`: чтение, запись или передача после `free` / `delete`. Сам освобождающий вызов использованием не считается. `p = NULL` и `p = realloc(p, n)` факт снимают. В чистой фикстуре `free` нет: сплошной запрет кучи на `--allow` не смотрит.
- `ss.mem.no-overlap-copy`: `memcpy` / `mempcpy` с пересечением и присваивание структуры самой себе. `memmove` и скаляр `x = x` молчат.
- `ss.mem.string-room-for-nul`: литерал, которому не хватает байта под NUL, и `strncpy`, чья длина не меньше ёмкости. Для `memcpy` длина, равная ёмкости, ещё влезает.
- `ss.ptr.no-deref-one-past`: разыменование ровно на элемент за концом. `ss.mem.bounds` на том же месте тоже срабатывает.
- `ss.ptr.same-array`: вычитание и `<` `<=` `>` `>=` двух доказанно разных объектов. `==` / `!=` и неизвестная сторона молчат.
- `ss.libc.copy-fits-dest`: постоянная длина `memcpy`, `memmove`, `memset`, `strncpy` или размер `snprintf` больше ёмкости назначения. Неизвестный буфер молчит.
- `ss.conv.no-div-zero`: `/` и `%`, у которых делитель точно ноль, включая ребро `if (d == 0) return n / d`.
- `ss.conv.no-signed-overflow`: `+` `-` `*` `/` `%` `<<` и унарный минус, чей результат не влезает в знаковый тип. `1 << 31` на `int` сюда входит. Слагаемое с несколькими возможными значениями молчит.
- `ss.expr.no-unseq`: два изменения одного объекта или изменение вместе с чтением в одном полном выражении. `i = i + 1` и инкремент `for` молчат. Через `&&`, `||` и запятую эффекты не склеиваются.
- `ss.expr.fp-must-be-finite`: доказанные Inf и NaN. Аргумент `isfinite` / `isinf` / `isnan` / `finite` молчит.
- `ss.ctrl.no-invariant-condition`: условие, которое сворачивается в истину или ложь. `while (0)`, `do while (0)`, `while (false)` и `for (; 0;)` остаются пустым идиомом. `for (;;)` молчит, `while (1)` отмечается. `sizeof` / `alignof`, комментарий со `static_assert` на этой или предыдущей строке и `if constexpr` молчат.

На pump дельта +5 относительно лога после графа вызовов, все `while (1)`: `can_task.cpp:368`, `esp32_uart_task.cpp:542`, `exp_pump_task.cpp:237`, `pump_task.cpp:321`, `pump_task.cpp:1804`. Остальные десять правил на этих 11 файлах молчат. Прежние 21 `ss.ctrl.loop-bound` на месте.

### 8. Политики поверх графа

Сделано. `ss.conc.no-block-in-cs` и поздний режим `ss.mem.no-heap-after-init` сидят на том же графе. Имена входа и выхода секции и маски те же, что у `ss.emb.cs-balanced` и `ss.emb.irq-mask-balanced`. Вызов конструктора — ребро. Вызов через указатель ребром не становится. На развилке CFG берётся первый путь, как у проверок баланса.

- `ss.conc.no-block-in-cs`: вызов, пока секция или маска ещё открыта, если он сам или кто-то по прямым вызовам делает delay, ожидание очереди или семафора, `malloc` / `new` или стирание flash. `FromISR` ожиданием не считается. `--allow` называет прямой вызываемый идентификатор. Сообщение: «do not block while a critical section or interrupt mask is held».
- `ss.mem.no-heap-after-init`: `malloc`, `calloc`, `realloc`, `free`, `aligned_alloc`, `posix_memalign`, `new` и `delete` по-прежнему ошибка. Тихо только если каждый путь к функции начинается в статической инициализации файла или в функции из `--allow ss.mem.no-heap-after-init:NAME`, включая помощника в другом файле. Статическая локальная — не init. Функция без такого пути, в том числе никем не вызванная, по-прежнему отмечается. Текст тот же: «do not use the heap».

На pump дельта 0 относительно лога после остатка dataflow. Оба правила на этих 11 файлах молчат. Живых чекеров 101 из 192. В `embedded-cpp` 96 из 157. Сторож no-checker по-прежнему `ss.conv.signed-unsigned-mix`.

### 9. Межпроцедурный dataflow

Сделано. Новых id нет. Тот же проход, что в шагах 5 и 7, для `ss.expr.uninit`, `ss.mem.no-null-deref`, `ss.mem.bounds` и `ss.ptr.no-deref-one-past`. Прямой вызов получает сводку по тому же ключу, что у графа: USR, у файлового `static` ещё и файл. Сводка разбирается после всех файлов, поэтому вызывающий, разобранный раньше тела, всё равно доходит.

Отмечается доказанный факт. `if (p) *p` обязательным разыменованием не считается. Вызов через указатель, вызов оператора, тело, которого нет в запуске, и цикл остаются непрозрачными. Запись в объект новых срабатываний не добавляет.

- Неинициализированный адрес, который вызываемый на каждом пути читает, отмечается на вызове. Если каждый такой вызов объект не трогает, позднее чтение в вызывающем тоже отмечается. Запись или непрозрачное тело это чтение гасит.
- Аргумент, который точно null и который вызываемый на каждом пути разыменовывает. То же для возврата null или того же аргумента, если результат потом разыменовывают.
- Постоянный индекс за границей объекта вызывающего, включая сдвиг возвращённого указателя. Ровно one-past даёт ещё и `ss.ptr.no-deref-one-past`.

Сообщения те же, что внутри функции. На pump дельта 0 относительно лога после политик графа. Эти четыре правила на 11 файлах молчат. Живых чекеров 101 из 192. В `embedded-cpp` 96 из 157. Сторож no-checker по-прежнему `ss.conv.signed-unsigned-mix`.

### 10. Таблица символов на весь запуск

Сделано. Четыре правила, один проход на весь запуск. Граф вызовов эту таблицу не заменяет. Ноль определений в запуске молчит. Два внешних определения в разных файлах запуска — дефект. Предварительное `int g;` и настоящее `int g = 1` в одном файле считаются одним. Тело заголовка, включённое из двух файлов, даёт одно диагностическое место.

- `ss.fn.single-definition`: имя с внешней связностью определено больше одного раза. `inline`, шаблон и C `inline` без `extern` могут повторяться. Слабая связь считается определением. Правило включено в `embedded-c` и `embedded-cpp`. Сообщение: «this symbol is defined more than once».
- `ss.fn.header-decl`: только C, из `embedded-cpp` исключено. Определение в исходнике без объявления снаружи этого файла. `main`, `_init`, `_fini` и обработчик прерывания заголовок не требуют. Сообщение: «declare this in a header».
- `ss.fn.param-names-consistent`: advisory, только профиль `strict`. Имя параметра в определении сверяется с именованным объявлением. Пустое имя в объявлении эту позицию не держит. Сообщение: «parameter name does not match the declaration».
- `ss.decl.extern-in-one-header`: только C, только объекты, из `embedded-cpp` исключено. `extern`, написанный в `.c`, отмечается всегда. Один заголовок молчит. Два разных заголовка — дефект. Сообщение: «declare this object in one header».

`--allow` называет функцию или объект. На pump дельта 0 относительно лога после межпроцедурного dataflow. В `embedded-cpp` из четырёх включено только `single-definition`, и на этих 11 файлах оно молчит. Живых чекеров 107 из 192. В `embedded-cpp` 97 из 157. Сторож no-checker по-прежнему `ss.conv.signed-unsigned-mix`.

### 11. Остальные types, затем syntax

Остаток `ss.conv.*` сделан. Восемь правил, каждое своим проходом. `no-mixed-categories` остаётся выключенным до профиля `strict`. Сторож no-checker теперь `ss.ptr.no-ptr-arith`.

- `ss.conv.signed-unsigned-mix`: после целочисленных продвижений знаковый и беззнаковый тип встречаются в одном арифметическом или сравнивающем выражении. Узкий беззнаковый тип, который становится `int`, молчит. Сдвиг сюда не входит. Сообщение: «signed and unsigned integers meet in this expression».
- `ss.conv.no-unsigned-wrap-const`: константные `+`, `-` и `*` беззнакового типа переполняются. Комментарий со словом mask на этой строке или на предыдущей комментарной строке оставляет выражение тихим. Сообщение: «unsigned constant wraps».
- `ss.conv.no-unary-minus-unsigned`: унарный минус у значения, которое после продвижения всё ещё беззнаковое. Сообщение: «do not negate an unsigned value».
- `ss.conv.no-object-ptr-cast`: явное приведение между неродственными указателями на объекты. `void *`, указатель на символ, `std::byte *`, упакованная структура, база и наследник молчат. Сообщение: «do not cast between unrelated object pointers».
- `ss.conv.no-fn-ptr-cast`: указатель на функцию приводится к другому типу. Та же сигнатура, `nullptr` и отбрасывающий `(void)` молчат. Сообщение: «do not convert a function pointer to another type».
- `ss.conv.no-fp-narrow`: неявная потеря точности плавающего типа. Явное приведение молчит. Сообщение: «do not drop floating precision implicitly».
- `ss.conv.fp-to-int-explicit`: float становится целым, и неявно, и явным приведением. `--allow` называет функцию-помощник. Сообщение: «convert a float to an integer in a named helper».
- `ss.conv.no-int-to-enum`: целое становится перечислением. Имя перечислителя молчит. `--allow` называет функцию. Сообщение: «do not convert an integer to an enum».

На pump дельта +262 относительно лога после таблицы символов: 206 смесь знакового с беззнаковым, 28 float в целый, 23 целое в enum, 4 чужих указателя на объекты, 1 неявное сужение float. Переполнение константы, унарный минус и приведение указателя на функцию на этих 11 файлах молчат. Живых чекеров 115 из 192. В `embedded-cpp` 105 из 157.

Указатели и размер кадра сделаны. Шесть правил. `no-fn-pointer` остаётся advisory. Сторож no-checker теперь `ss.decl.no-unused`.

- `ss.ptr.no-ptr-arith`: `p + n`, `p - n`, `+=`, `-=`, `++` и `--` на указателе. Индексация `a[i]`, `p[i]` и `&a[i]` остаётся разрешённой формой. Вычитание двух указателей и их сравнение остаются у `ss.ptr.same-array`. Range-for по массиву — сахар компилятора, оба арифметических правила его пропускают. Сообщение: «do not do pointer arithmetic; index an array».
- `ss.ptr.no-void-arith`: только C, из `embedded-cpp` исключено. Арифметика на `void *`. `void **` идёт в обычное правило. Сообщение: «do not do arithmetic on a void pointer».
- `ss.ptr.array-arg`: только C. Прямой вызов с параметром `T a[N]` требует массив, чья внешняя постоянная граница не меньше N, включая строковый литерал вместе с нулём. Указатель, `&arr[0]` и неполный массив длину не доказывают. Сообщение: «pass an array of at least the declared length».
- `ss.ptr.max-indirection`: глубже `T **` — дефект. Массив и ссылка уровень не добавляют. Потолок зашит: два. Сообщение: «more than two pointer levels».
- `ss.mem.sizeof-array-param`: только C, из `embedded-cpp` исключено. `sizeof` параметра, который схлопнулся в указатель. `sizeof(*p)`, `sizeof(p[0])` и `sizeof(int)` молчат. Сообщение: «sizeof of a decayed array parameter is the pointer size».
- `ss.mem.no-large-frame`: сумма автоматических локальных строго больше 512 байт. `static` не считается. Ровно 512 молчит. `--allow ss.mem.no-large-frame:NAME` называет функцию. Сообщение: «this function's locals exceed 512 bytes».

`&a[N]` — адрес, поэтому `ss.ptr.no-deref-one-past` и `ss.mem.bounds` на таком индексе молчат. `a[N]`, `*(a + N)` и последующее `*p` по-прежнему отмечаются. В `ok_ret` возврат стал `return p`: `return &p[1]` ловил `ss.mem.no-dangling`.

На pump дельта +28 относительно лога после конверсий, пропавших диагностик нет. Все 28 — `ss.ptr.no-ptr-arith`: 20 в `usb_task.cpp`, 8 в `settings.hpp`. Глубина указателей и размер кадра на этих 11 файлах молчат. Три правила только для C в `embedded-cpp` не входят. Живых чекеров 121 из 192. В `embedded-cpp` 108 из 157. В `embedded-c` 97 из 149. В `strict` 120 из 175.

Объявления, условия и инициализация сделаны. Семь правил. `inline-is-static` только C и из `embedded-cpp` исключено. Сторож no-checker теперь `ss.libc.memcmp-not-for-c-strings`.

- `ss.decl.no-unused`: статическая функция, объект файловой области или typedef, которыми этот файл не пользуется. Статическая функция отмечается, только если в том же файле есть функция с внешней связностью. Файл из одних статических функций молчит. Заголовок, включённый в файл, молчит. Сообщения: «this static function is never used», «this file-scope object is never used», «this typedef is never used».
- `ss.decl.min-scope`: объект файловой области с внутренней связностью, который использует одна функция, и локальная с тривиальным инициализатором, которой пользуется только более узкий блок. Сообщения: «declare this object in the function that uses it», «declare this where it is used».
- `ss.decl.align-same`: у объявлений объекта разный `_Alignas` / `alignas`. Унаследованная копия, которую Clang ставит на определение, не считается написанным выравниванием. Сообщение: «alignment differs across declarations».
- `ss.ctrl.boolean-condition`: условие `if`, `while`, `for` и `do` — булево значение или сравнение. `if (n)` и `while (1)` отмечаются. `--allow` называет функцию. Сообщение: «condition should be a boolean or a comparison».
- `ss.ctrl.no-fp-loop-counter`: счётчик цикла имеет целый тип. Это ошибка. Сообщение: «a loop counter shall have integer type».
- `ss.expr.aggregate-init`: неполная инициализация, designated-массив без размера и плоский список вместо вложенных скобок. Пустой `{}` — полная инициализация. Сообщения: «initialize every element of this aggregate», «state the size of this designated array», «enclose this nested aggregate in braces».
- `ss.fn.inline-is-static`: только C. `inline` без `static` в этом файле. Тело из заголовка молчит. Сообщение: «make this inline function static».

На pump дельта +90 относительно лога после указателей, пропавших диагностик нет. 47 условий не булевы, 32 узких области (23 локальные, 9 файловые), 9 неполных агрегатов, 1 вещественный счётчик, 1 неиспользуемый объект. Выравнивание на этих 11 файлах молчит. `inline-is-static` в `embedded-cpp` не входит. Больше всего в `usb_task.cpp` (41) и `pump_task.cpp` (28). Живых чекеров 128 из 192. В `embedded-cpp` 114 из 157. В `embedded-c` 104 из 149. В `strict` 127 из 175.

Libc для строк и ctype сделан. Два правила. Сторож no-checker теперь `ss.type.fixed-width`.

- `ss.libc.memcmp-not-for-c-strings`: `memcmp` по буферу `char` или `signed char`. `unsigned char`, структура и `void *` остаются упакованной раскладкой. Приведение к `void *` тип буфера не прячет. Сообщение: «do not memcmp a C string».
- `ss.libc.ctype-uchar-or-eof`: одноаргументные `isdigit`, `toupper` и остальные функции `<ctype.h>`. Дефект — `signed char`, а при знаковом `char` ещё и обычный `char`. `int` молчит: там живёт EOF. Константа от 0 до `UCHAR_MAX` или значение `-1`, а также приведение к `unsigned char`, молчат. Приведение `signed char` к `int` отмечается. Сообщение: «pass unsigned char or EOF to ctype».

На pump дельта 0 относительно лога после объявлений, пропавших диагностик нет. Оба правила на этих 11 файлах молчат. `memcmp` структуры через `void *` в `settings.hpp` остаётся тихим. Живых чекеров 130 из 192. В `embedded-cpp` 116 из 157. В `embedded-c` 106 из 149. В `strict` 129 из 175.

Типы сделаны. Восемь правил. `no-union` и `no-fp-unless-needed` остаются advisory: сплошного запрета union нет. `explicit` и `no-restrict` только C и из `embedded-cpp` исключены. Сторож no-checker теперь `ss.emb.isr-marked`.

- `ss.type.fixed-width`: написанные `short` и `long`, включая `typedef long`. Имя вроде `int64_t` молчит, даже если на ARM оно раскрывается в `long`. Арифметика, сдвиг и инкремент обычного `char` — целое использование. `signed char`, `unsigned char`, строка и символьный литерал молчат. Сообщения: «use a fixed-width integer instead of short or long», «keep char for character data».
- `ss.type.bool`: явный каст в `bool` или `_Bool`. Неявное `if (n)` остаётся у `ss.ctrl.boolean-condition`. Сообщение: «use a comparison instead of a cast to bool».
- `ss.type.explicit`: только C. Пропущенный тип, включая функцию `static value(void)`. Сообщение: «specify the type explicitly».
- `ss.type.no-restrict`: только C. Квалификатор `restrict` на указателе, массиве или в явном касте. Сообщение: «do not use restrict».
- `ss.type.bitfield`: обычный `int` и знаковое поле шириной 1. `unsigned`, `bool` и явный `signed` шире одного бита молчат. Сообщения: «give this bit-field an explicit signed or unsigned type», «a single-bit field shall not be signed».
- `ss.type.enum-init`: ни одного инициализатора, только первый или все. Совпадающие значения отмечаются отдельно, даже если список инициализирован целиком. Сообщения: «initialize none, only the first, or every enumerator», «enumerator values collide».
- `ss.type.array-size`: массив без размера. Параметр `T a[]` и массив, чей размер задаёт инициализатор, молчат. Гибкий член тоже без размера. Сообщение: «specify the array size».
- `ss.type.no-bool-math`: `++`, `--` и арифметика, где операнд — `bool`. В C сравнение имеет тип `int` и молчит. В C++ сравнение — `bool`, и `n + (a < b)` отмечается. Сообщение: «do not do arithmetic on a boolean».

`--allow` называет символ, а у каста и оператора — функцию. На pump дельта +28 относительно лога после libc, пропавших диагностик нет. 26 — `ss.type.enum-init` (24 столкновения, 2 смешанных списка) и 2 — `ss.type.fixed-width`: `typedef long BaseType_t` и `typedef unsigned long UBaseType_t` в `portmacro.h`. `bool`, `bitfield`, `array-size` и `no-bool-math` на этих 11 файлах молчат. `explicit` и `no-restrict` в `embedded-cpp` не входят. Больше всего столкновений в `at32f403a_407_adc.h` (22): коды ADC12 и ADC3 повторяют одни числа. Живых чекеров 138 из 192. В `embedded-cpp` 122 из 157. В `embedded-c` 114 из 149. В `strict` 137 из 175.

Синтаксис сделан. Восемь правил, оба языка. Advisory (`max-if-nesting`, `named-loop-limit`, `max-length`, `max-params`, `parens`, `named-constants`, `no-time`, `no-tgmath-fenv`, `seq-cst-or-documented`) остаётся следом. Десять `ss.style.*` — в самом конце, только вместе с профилем `style`. Сторож no-checker теперь `ss.conv.no-assign-as-value`. В список этого среза оно не входило.

- `ss.emb.isr-marked`: функция в таблице векторов несёт атрибут прерывания или имя CMSIS. `*_IRQHandler` и `void(void)` с именем `*_Handler` уже отмечены: на Cortex-M это обычная функция, а имя и есть отметка. Домашнее `*_isr` без атрибута — дефект. Адрес функции в массиве, чьё имя или секция содержит `vector`, без такой отметки — тоже. Сообщение: «declare this handler with the interrupt ABI».
- `ss.emb.layout-static-assert`: упакованная структура имеет `static_assert` на `sizeof` и на `offsetof` каждого именованного поля, кроме битового. `__builtin_offsetof` считается. Размер вне `static_assert` не считается. Сообщения: «assert the size of this packed struct», «assert the offset of this field».
- `ss.emb.localize-extensions`: `#pragma`, `__attribute__`, `__declspec`, `__packed`, `__weak`, `__irq` и встроенный ассемблер ищутся в тексте главного файла. Включённый заголовок — слой железа и молчит. Комментарий и строка молчат. `[[fallthrough]]` сюда не входит. Сообщение: «keep this extension in the hardware layer».
- `ss.emb.no-errno-host`: вызов `open`, `read`, `setlocale` и остальных имён файла и локали, если у функции нет тела в этой единице, это не метод и связь не внутренняя. `static int read` и метод `read` молчат. Использование файлового `errno` тоже дефект. Сообщение: «do not use host errno, locale, or file APIs».
- `ss.expr.no-trigraphs`: триграф в коде, строке и комментарии. Диграф только вне комментария и строки. `--allow` нет. Сообщения: «do not use a trigraph», «do not use a digraph».
- `ss.expr.init-side-effect-free`: список инициализации и написанный инициализатор конструктора не делают `++`, `--`, присваивание, вызов, `new` или `delete`. Сам вызов конструктора молчит, `sizeof` молчит. Сообщение: «an initializer has no side effects».
- `ss.decl.no-reserved`: имя, которое начинается с `__` или с `_` и заглавной буквы. Встроенная функция компилятора молчит. Внешнее определение библиотечного имени — отдельное сообщение. Прототип определением не считается, метод тоже. Сообщения: «do not use a reserved identifier», «do not redefine a library name».
- `ss.decl.one-per-line`: два объявителя в одном операторе (`int a, b;`). Два оператора на одной строке и параметры функции молчат. Общий спецификатор типа берётся по месту в файле: два оператора с одним макросом типа не склеиваются. Сообщение: «declare one identifier per statement».

`--allow` называет символ, а у вызова, оператора и расширения внутри функции — функцию. У триграфа `--allow` нет. На pump дельта +23 относительно лога после типов, пропавших диагностик нет. 19 — два объявителя в одном операторе, 3 — зарезервированное имя (`_SEGGER_RTT`, параметры `_R1` и `_R2`), 1 — вызов внутри списка инициализации в `usb_task.cpp`. Отмеченный обработчик, `static_assert` упакованной структуры, расширение в главном файле, errno и файловый вызов, триграф на этих 11 файлах молчат. Живых чекеров 146 из 192. В `embedded-cpp` 130 из 157. В `embedded-c` 122 из 149. В `strict` 145 из 175.

Advisory syntax сделан. Девять правил. `parens` включён в `strict`. `no-tgmath-fenv` только C: на `.cpp` чекер выходит сразу, и `embedded-cpp` его не включает. Десять `ss.style.*` остаются в самом конце, только вместе с профилем `style`. Сторож no-checker теперь `ss.ctrl.for-well-formed`.

- `ss.ctrl.max-if-nesting`: вложенность `if` глубже двух уровней. Цепочка `else if` — один уровень: внутренний `if` сам является веткой `else`. `else { if }` уровень добавляет. `switch` молчит. Сообщение: «do not nest if-else more than two levels».
- `ss.ctrl.named-loop-limit`: граница `for`, `while` или `do` — голый целочисленный литерал, не 0 и не 1. Инициализатор, шаг и range-for молчат. Сравнение внутри `&&` тоже смотрится. `i < n`, `i < sizeof(buf)`, `i < (n + 64)` и `(flags & 4) != 0` молчат. Тело макроса — уже имя. `for (;;)` и `while (1)` сравнения не имеют и молчат. Сообщение: «name this loop limit».
- `ss.fn.max-length`: в теле больше 80 строк кода. Комментарий и пустая строка не считаются, строка с кодом и комментарием считается. Порог 80, отдельной настройки нет. Сообщение: «keep this function to 80 lines».
- `ss.fn.max-params`: больше восьми параметров. `this` не считается. Если есть определение, отмечается оно, иначе каноническое объявление. Сообщение: «do not pass more than eight parameters».
- `ss.expr.parens`: операнд `&&` или `||` — скобки или одно имя. Разный приоритет без скобок тоже дефект. Одинаковый приоритет, присваивание и запятая молчат. Сообщения: «parenthesize this && or || operand», «parenthesize this mixed-precedence expression».
- `ss.expr.named-constants`: число, кроме 0, 1 и -1, в условии `if`, `while`, `for`, `do` и тернарного оператора, в аргументе вызова, чьё имя содержит delay или sleep, в написанной границе константного массива, в поле протокола. Поле — обозначенный инициализатор поля или позиционный список у упакованной структуры. Обозначение читается с синтаксической формы списка. Обозначение массива молчит. Тело макроса — имя. `sizeof` молчит. Сообщение: «name this numeric literal».
- `ss.libc.no-time`: точный `#include` `time.h` и вызов `time`, `clock`, `nanosleep` и остальных имён часов, если у функции нет тела в этой единице, это не метод и связь не внутренняя. `sys/time.h` молчит. Сообщение: «do not use time.h».
- `ss.libc.no-tgmath-fenv`: только C. Точный `#include` `tgmath.h` или `fenv.h` и вызов `fesetround` и остальных имён `<fenv.h>`. Обычный `sinf` молчит: type-generic макросы ловит include. Сообщение: «do not use tgmath.h or fenv.h».
- `ss.conc.seq-cst-or-documented`: `memory_order_relaxed`, `consume`, `acquire`, `release`, `acq_rel` и парные `__ATOMIC_*`. Короткое имя только если объемлющий enum содержит `memory_order`. `memory_order_seq_cst` и `__ATOMIC_SEQ_CST` молчат. Непустой комментарий на этой строке или на предыдущей глушит. Пустой `//` не считается. Функция с именем `release` молчит. Сообщение: «document this non-sequential atomic order».

`--allow` называет символ, а у условия, цикла, вызова и оператора — функцию. У include — имя заголовка (`time.h`, `fenv.h`, `tgmath.h`). На pump дельта +376 относительно лога после синтаксиса, пропавших диагностик нет. 324 — `ss.expr.named-constants` (границы в заголовках AT32 и FreeRTOS и размеры буферов в своих `.cpp`), 30 — `ss.ctrl.max-if-nesting`, 18 — `ss.ctrl.named-loop-limit`, 4 — `ss.fn.max-length` в `pump_task.cpp` и `usb_task.cpp`. `max-params`, `parens`, `no-time`, `no-tgmath-fenv` и `seq-cst-or-documented` на этих 11 файлах молчат. Живых чекеров 155 из 192. В `embedded-cpp` 137 из 157. В `embedded-c` 130 из 149. В `strict` 154 из 176.

Дальше шаг 12, остаток cfg: `for-well-formed`, `no-dead-code`, `no-unused-label`, `cyclomatic`. `single-break-in-loop` остаётся позже.

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
