# Schema of a canonical rule

Each document in `rules/*.yaml` is a list named `rules`. One object — one checker.

```yaml
rules:
  - id: ss.ctrl.no-goto
    title: Do not use goto
    severity: warning          # error | warning | advisory | style
    languages: [c, cpp]        # c, cpp, or both
    check: syntax              # how a checker can exist
    default: "on"              # YAML must quote on/off (booleans otherwise)
    summary: >
      Short requirement in our wording.
    rationale: >
      Why this matters on a microcontroller.
    exceptions:
      - When a documented deviation exists at the call site.
    notes: >
      Implementation hints, conflicts with other rules.
```

Do not add a `sources` / `maps_to` field that points at another vendor's catalog.

## Fields

| Field | Required | Meaning |
| --- | --- | --- |
| `id` | yes | Stable checker id. Never reuse. |
| `title` | yes | One-line English title for UI and SARIF. |
| `severity` | yes | Default severity. Profiles may override. |
| `languages` | yes | Apply only when the TU is that language. |
| `check` | yes | Expected analysis kind (see below). |
| `default` | yes | `"on"` / `"off"` / `"advisory"` for the embedded-c profile. |
| `summary` | yes | The actual requirement, our wording. |
| `rationale` | no | Why, in embedded terms. |
| `exceptions` | no | Legal deviations a checker should recognize. |
| `notes` | no | Conflicts, phased implementation, HW reality. |

## `check` values

| Value | Typical implementation |
| --- | --- |
| `syntax` | AST / token pattern (goto, octal, braces). |
| `types` | Needs a type checker (signed bitwise, mixed categories). |
| `cfg` | Control-flow graph (unreachable, loop bound). |
| `dataflow` | Values / lifetime (uninit, dangling, overlap). |
| `callgraph` | Whole-program (recursion, heap after init). |
| `review` | Not reliably mechanical; emit as a review hint. |
| `process` | Build/CI policy, not a source checker. |

## `severity`

| Value | Use |
| --- | --- |
| `error` | Defect class or undefined behaviour; fail the build in embedded-c. |
| `warning` | High-value restriction; default fail, suppressible. |
| `advisory` | Good default, noisy on real HALs (unions, function pointers). |
| `style` | Formatting/naming; only in the `style` profile. |
