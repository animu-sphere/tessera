# Tessera UI JSON v1

This page defines the implemented encoding contract, not a stable release format. Model and lifetime rules belong to [UI model](../../docs/design/ui-model.md); validation/configuration evidence belongs to [support](../../docs/reference/support-matrix.md).

## Envelope and nodes

```json
{
  "version": 1,
  "root": {
    "type": "Box",
    "id": "menu",
    "children": [
      {"type": "Text", "id": "title", "properties": {"text": "Menu / メニュー"}},
      {
        "type": "Box",
        "properties": {"focusable": true, "labelled_by": {"ref": "title"}},
        "events": {"activate": "start_game"}
      }
    ]
  },
  "extensions": {"path-finder:editor": {"selected": "menu"}}
}
```

The caller must supply `start_game` in `ValidationContext.actions` to accept this example. Bindings serialize names only; runtime [pointer dispatch](../../docs/design/input.md) returns host action requests after [layout](../../docs/design/layout.md). No callback is serialized.

| Field | Required | Type / rule |
| --- | --- | --- |
| Document `version` | Yes | Numeric integer 1; other versions fail without implicit migration |
| Document `root` | Yes | Exactly one node |
| Document/node `extensions` | No | Object of namespaced extension keys to JSON values; default empty |
| Node `type` | Yes | Case-sensitive `Box` or `Text` |
| Node `id` | No | Nonempty UTF-8 string without ASCII controls; unique across the document |
| Node `classes` | No | Ordered array of distinct nonempty UTF-8 identifiers; default empty |
| Node `properties` | No | Object following the model's property vocabulary; default empty |
| Node `events` | No | Object of `activate`/`cancel` to registered host-action identifiers; default empty |
| Node `children` | No | Ordered array of nodes; default empty; Text must be a leaf |

Identifiers reject U+0000–U+001F and U+007F; no other normalization is performed. Case and Unicode bytes are significant. A reference is an object containing exactly `ref`, whose value is an author ID. Forward references are allowed. Missing targets fail; this slice validates existence only, without relationship/focus behavior.

Unknown fields fail at every schema level. Unknown properties and bindings fail rather than being silently preserved. Extension keys must have a nonempty namespace and suffix separated by `:`. Extension values may contain nested objects/arrays, strings, finite numbers, booleans, or null. Metadata has no runtime behavior. It is semantically preserved; whitespace, object member order, numeric spelling, and escape spelling are normalized.

## Canonical output

- UTF-8 without BOM, compact JSON, exactly one trailing LF.
- Every object uses ascending string byte order, including properties and metadata.
- Child/class/metadata array order is preserved.
- Absent optional empty containers are omitted; `id` remains absent when unauthored.
- Quotation marks/backslashes are escaped; controls use lowercase `\u00xx`; other UTF-8 is emitted directly. Unicode escape input and valid surrogate pairs decode to UTF-8.
- All numbers use finite binary64 values. `std::to_chars` general formatting emits the shortest round-tripping representation; negative zero saves as `0`. Large integers in metadata follow binary64 precision and are not arbitrary-precision integers.

Repeated save/load/save is byte-stable on the validated toolchain. Child order is semantic; changing it does not create an equivalent document. Platform-independent runtime semantics are intended, while other toolchains remain unvalidated.

## Rejection and bounds

`load_document` and `save_document` return a value only on success. JSON parsing/schema conversion stops at its first error; semantic validation collects independent errors in deterministic traversal order. Duplicate JSON members, malformed escapes, invalid UTF-8, non-finite/unrepresentable numbers, trailing data, comments, and trailing commas fail.

Limits: 4 MiB of input/output UTF-8, 64 node levels (root = 1), 10,000 nodes, and 256 JSON value nesting levels (envelope = 0). The same node and metadata depth checks apply to builder-created documents. The output byte limit includes LF. Input and canonical output sizes can differ; a source within the input limit can exceed the output limit after normalization and fail saving. There is no unlimited-input guarantee. Callers remain responsible for memory used to construct their own values.

Diagnostics carry a stable code, error severity, an escaped JSON-pointer path, an actionable message, and a zero-based UTF-8 byte offset for loaded input. Schema/semantic diagnostics point to the offending value when present, otherwise the closest containing value. Syntax offsets identify the parser position; invalid raw UTF-8 is reported at the document boundary. Builder diagnostics have no source offset. Line/column spans and warning policies remain future work.

The canonical [fixture](../../tests/serialization/fixtures/menu.json) and [checks](../../tests/serialization/document_tests.cpp) exercise normalization, metadata, Unicode, numeric precision, limits, references, and failures. Core APIs consume/produce strings; filesystem/network loading is host-owned.
