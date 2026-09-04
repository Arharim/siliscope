# Idea coverage (Siliscope wording)

This is a checklist of **defect classes**, not a table of anyone else's rule
numbers. Each class is either a `ss.*` checker or an explicit "not in scope".

## Control flow and bounded execution

| Idea | Rule |
| --- | --- |
| No `goto` / `setjmp` / recursion | `ss.ctrl.no-goto`, `ss.ctrl.no-setjmp`, `ss.ctrl.no-recursion` |
| Loops cannot run away | `ss.ctrl.loop-bound`, `ss.ctrl.named-loop-limit` |
| Superloop vs hang | `ss.ctrl.loop-bound` exceptions |
| Braces, else-final, no assign-in-if | `ss.ctrl.braces`, `ss.ctrl.if-else-final`, `ss.ctrl.no-assignment-in-condition` |
| Boolean conditions, no invariant tests | `ss.ctrl.boolean-condition`, `ss.ctrl.no-invariant-condition` |
| Integer loop counters, well-formed `for` | `ss.ctrl.no-fp-loop-counter`, `ss.ctrl.for-well-formed` |
| Switch completeness / fall-through | `ss.ctrl.switch-well-formed`, `ss.ctrl.switch-fallthrough-comment` |
| Unreachable / dead / empty wait | `ss.ctrl.unreachable`, `ss.ctrl.no-dead-code`, `ss.ctrl.empty-loop-comment` |
| Nesting and ternary soup | `ss.ctrl.max-if-nesting`, `ss.ctrl.no-nested-ternary` |

## Memory, stack, lifetime

| Idea | Rule |
| --- | --- |
| No heap after init | `ss.mem.no-heap-after-init`, `ss.cpp.no-heap-stl` |
| No VLA / flexible array | `ss.mem.no-vla`, `ss.mem.no-flexible-array` |
| Bounds, null, one-past, dangling | `ss.mem.bounds`, `ss.mem.no-null-deref`, `ss.ptr.no-deref-one-past`, `ss.mem.no-dangling` |
| Use-after-free, overlap | `ss.mem.no-use-after-free`, `ss.mem.no-overlap-copy` |
| Stack frame size | `ss.mem.no-large-frame` |
| `sizeof` on a decayed array param | `ss.mem.sizeof-array-param` |
| String + NUL | `ss.mem.string-room-for-nul` |
| Copy length vs destination | `ss.libc.copy-fits-dest` |

## Integers, conversions, floating point

| Idea | Rule |
| --- | --- |
| Signed overflow / unsigned wrap in constants | `ss.conv.no-signed-overflow`, `ss.conv.no-unsigned-wrap-const` |
| Shift range, unary minus on unsigned | `ss.conv.shift-range`, `ss.conv.no-unary-minus-unsigned` |
| Signed/unsigned mix, signed bitwise | `ss.conv.signed-unsigned-mix`, `ss.conv.no-signed-bitwise` |
| Silent narrow, fp narrow, fp→int | `ss.conv.no-silent-narrow`, `ss.conv.no-fp-narrow`, `ss.conv.fp-to-int-explicit` |
| bool/char/enum as generic ints | `ss.conv.no-mixed-categories`, `ss.type.no-bool-math` |
| No `==` on floats; Inf/NaN | `ss.conv.no-fp-eq`, `ss.expr.fp-must-be-finite` |
| Prefer no FP on cores without FPU | `ss.type.no-fp-unless-needed`, `ss.expr.float-literal-suffix` |
| Divisor non-zero | `ss.conv.no-div-zero` |
| Pointer/integer and unrelated pointer casts | `ss.conv.no-ptr-int`, `ss.conv.no-object-ptr-cast`, `ss.conv.no-fn-ptr-cast`, `ss.conv.no-cv-away` |

## Expressions, init, preprocessor

| Idea | Rule |
| --- | --- |
| Uninit, unsequenced side effects | `ss.expr.uninit`, `ss.expr.no-unseq` |
| `++` / `sizeof` / `&&` side effects | `ss.expr.no-inc-in-expr`, `ss.expr.no-sizeof-side-effect`, `ss.expr.no-logical-rhs-side-effect` |
| Assignment as a value, comma operator | `ss.conv.no-assign-as-value`, `ss.expr.no-comma` |
| Octal, trigraphs, string literals | `ss.expr.no-octal`, `ss.expr.no-trigraphs`, `ss.expr.string-const` |
| Magic numbers | `ss.expr.named-constants` |
| Aggregate init, init side effects | `ss.expr.aggregate-init`, `ss.expr.init-side-effect-free` |
| Parentheses | `ss.expr.parens` |
| Limited preprocessor, guards, own header | `ss.pre.limited`, `ss.pre.include-guard`, `ss.pre.source-includes-own-header` |
| Macro parens, no keyword macros | `ss.pre.macro-parens`, `ss.pre.no-keyword-macro` |

## Functions, modules, names

| Idea | Rule |
| --- | --- |
| Prototypes, return all paths, check return | `ss.fn.prototype`, `ss.fn.return-all-paths`, `ss.fn.check-return` |
| Size / complexity / param count | `ss.fn.max-length`, `ss.fn.cyclomatic`, `ss.fn.max-params` |
| One definition, header matches .c | `ss.fn.single-definition`, `ss.fn.header-decl`, `ss.fn.param-names-consistent` |
| static internals, C inline, unused | `ss.fn.static-internal`, `ss.fn.inline-is-static`, `ss.fn.no-unused-params`, `ss.decl.no-unused` |
| No stdarg, no block-scope fn, noreturn | `ss.fn.no-stdarg`, `ss.fn.no-block-scope`, `ss.fn.noreturn-does-not-return` |
| Distinct names, no shadow, no reserved | `ss.decl.distinct`, `ss.decl.no-shadow`, `ss.decl.no-reserved` |

## Libc that does not belong on a chip

| Idea | Rule |
| --- | --- |
| malloc family | `ss.mem.no-heap-after-init` |
| abort/exit/system, atoi, rand, signal, stdio | `ss.libc.no-abort-system`, `ss.libc.no-atoi`, `ss.libc.no-rand`, `ss.libc.no-signal`, `ss.libc.no-stdio` |
| Unbounded strings, ctype, memcmp-as-strcmp | `ss.libc.no-unbounded-string`, `ss.libc.ctype-uchar-or-eof`, `ss.libc.memcmp-not-for-c-strings` |
| locale / errno / host files | `ss.libc.no-setlocale`, `ss.emb.no-errno-host` |

## Firmware: ISR, HAL, concurrency

| Idea | Rule |
| --- | --- |
| ISR ABI, not called as a function, short body | `ss.emb.isr-marked`, `ss.emb.isr-not-called`, `ss.emb.isr-body` |
| No FP / no log in ISR | `ss.emb.isr-no-fp`, `ss.emb.no-log-in-isr` |
| Default vector, layout asserts | `ss.emb.default-isr`, `ss.emb.layout-static-assert` |
| Extensions stay in HAL | `ss.emb.localize-extensions` |
| Critical section / irq mask pairing | `ss.emb.cs-balanced`, `ss.emb.irq-mask-balanced` |
| Shared data, not volatile-as-lock | `ss.conc.no-data-race`, `ss.conc.no-volatile-mutex` |
| Lock order, no block in CS | `ss.conc.lock-order`, `ss.conc.no-block-in-cs` |
| Atomics | `ss.conc.seq-cst-or-documented` |

## C++ on a microcontroller

| Idea | Rule |
| --- | --- |
| No exceptions, no RTTI, no heap STL | `ss.cpp.no-exceptions`, `ss.cpp.no-rtti`, `ss.cpp.no-heap-stl` |
| Rule of five, init order, virtual dtor | `ss.cpp.special-members`, `ss.cpp.init-members`, `ss.cpp.virtual-dtor` |
| override, explicit conversions, nullptr | `ss.cpp.override`, `ss.cpp.no-implicit-conversion`, `ss.cpp.nullptr` |
| No C-style cast, no using in headers | `ss.cpp.no-cstyle-cast`, `ss.cpp.no-using-directive`, `ss.cpp.no-using-in-header` |

## Intentionally not in scope

These ideas showed up in the research docs and we **rejected** them for MCU
firmware, or they are not a source checker:

- Ban every `union` as an error (register maps need overlays) — advisory only
- Ban every function pointer (vector tables) — advisory only
- Force a single `return` at the bottom — early return is clearer
- Host POSIX/Win APIs, format-string exploits on a desktop libc
- Full language "essential type" algebra as a named foreign model
- Require C++ exceptions, RAII-with-heap, or "no static storage"
- Process/certification paperwork (which compiler flags a safety case needs)

## Profiles

| Profile | Coverage intent |
| --- | --- |
| `embedded-c` | Default firmware: UB + stack + heap + ISR; some advisory extras |
| `embedded-cpp` | Same plus no-EH / no-RTTI / no heap STL |
| `strict` | Everything above plus noisy advisory (magic numbers, complexity, mixed categories) |
| `style` | Formatting only |
