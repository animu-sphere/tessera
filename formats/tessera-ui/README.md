# Tessera UI JSON v1

This page defines the implemented encoding contract, not a stable release format. Model and lifetime rules belong to [UI model](../../docs/architecture/ui-model.md); validation/configuration evidence belongs to [support](../../docs/reference/support-matrix.md).

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

The caller must supply `start_game` in `ValidationContext.actions` to accept this example. Bindings serialize names only; runtime [pointer dispatch](../../docs/architecture/input.md) returns host action requests after [layout](../../docs/architecture/layout.md). No callback is serialized.

| Field | Required | Type / rule |
| --- | --- | --- |
| Document `version` | Yes | Numeric integer 1; other versions fail without implicit migration |
| Document `root` | Yes | Exactly one node |
| Document/node `extensions` | No | Object of namespaced extension keys to JSON values; default empty |
| Node `type` | Yes | Case-sensitive `Box` or `Text` |
| Node `id` | No | Nonempty UTF-8 string without ASCII controls; unique across the document |
| Node `classes` | No | Ordered array of distinct nonempty UTF-8 identifiers; default empty |
| Node `properties` | No | Object following the model's property vocabulary; default empty |
| Node `events` | No | Object of `activate`/`cancel` and tree-branch `expand`/`collapse` to registered host-action identifiers; default empty |
| Node `children` | No | Ordered array of nodes; default empty; Text must be a leaf |

Identifiers reject U+0000–U+001F and U+007F; no other normalization is performed. Case and Unicode bytes are significant. A reference is an object containing exactly `ref`, whose value is an author ID. Forward references are allowed. Missing targets fail; this slice validates existence only, without relationship/focus behavior.

The optional Box-only boolean `properties.separator` defaults to false without being materialized. When true it projects a separator role through [semantics](../../docs/architecture/semantics.md); its registered `activate` action, when eligible, requests host adjustment. This additive vocabulary introduces no widget node kind or callback. Older builds that do not recognize the marker reject it as an unknown property; migrate explicitly when exchanging documents with those builds.

The Box-only string `properties.toggle_role` is additive v1 vocabulary: `checkbox` or `switch`, absent meaning none. A toggle requires a boolean `selected` (its checked state) and an activate action, and cannot combine with a collection role, `modal`, `separator` or `scrollbar`. It projects the [controlled Checkbox/Switch](../../docs/guides/checkbox-switch.md) roles; it does not mutate host state. Existing documents retain their encoding, and older readers reject the unknown property.

The Box-only strings `properties.edit_role` and `properties.edit_value` are additive v1 vocabulary. `edit_role` is `text_input` or `number_input`; `edit_value` is the input's presented text, a combobox's selected label or a color picker's `#RRGGBB` color. Each requires the other and an activate action, except that a `combobox` or `color_picker` carries `edit_value` without `edit_role`, and an edit role cannot combine with `toggle_role`, a collection role, `modal`, `separator` or `scrollbar`. They project the [TextInput/NumberInput](../../docs/guides/text-number-input.md) roles and `value`; they do not encode editing state or mutate host state. Existing documents retain their encoding, and older readers reject the unknown properties.

The Box-only boolean `properties.scrollbar` is likewise additive v1 vocabulary, absent meaning false without materialization. A true marker cannot combine with `separator`, `modal` or a collection role. It projects the [controlled scrollbar](../../docs/guides/scrollbar.md) role and ordinary eligible activate action; it does not encode or mutate host scroll state. Existing documents retain their encoding, and older readers reject the unknown marker.

The additive popup vocabulary also remains version 1:

- Box-only `popup_anchor` is a nonempty UTF-8 author-ID **string**, not a strict reference. Missing targets are legal lifecycle state and suppress the popup's layout. Popup nodes must be direct root children after all ordinary content; no popup or its descendants may be an anchor.
- Box-only `popup_side` is exactly `bottom`, `top`, `left` or `right` (absent: `bottom`); `popup_gap` is a finite nonnegative number no greater than the maximum finite float (absent: 4). Box-only boolean `popup_match_width` (absent: false) makes a bottom/top popup at least as wide as its anchor. All three are inert without `popup_anchor`; `popup_match_width` is also inert for left/right sides and point placement.
- Box-only `popup_x` and `popup_y` are optional paired finite numbers within float range. Both require `popup_anchor`. They specify a root-logical point, overriding side/gap, with the same viewport shift and anchor-visibility rules.
- Box/Text `pointer_events` is boolean (absent: true). False excludes the entire subtree from pointer hits, but does not disable semantic text or keyboard focus.

These are ordinary typed properties; defaults are never materialized by serialization. Their placement and interaction meanings belong to [layout](../../docs/architecture/layout.md#implemented-anchored-popups) and [input](../../docs/architecture/input.md#implemented-popup-and-tooltip-interaction). Older builds reject the new properties; the envelope version is not a backward-reader compatibility claim.

Unknown fields fail at every schema level. Unknown properties and bindings fail rather than being silently preserved. Extension keys must have a nonempty namespace and suffix separated by `:`. Extension values may contain nested objects/arrays, strings, finite numbers, booleans, or null. Metadata has no runtime behavior. It is semantically preserved; whitespace, object member order, numeric spelling, and escape spelling are normalized.

Box-only boolean `modal` is another additive v1 property (absent: false). At most one true marker is allowed; it requires a stable ID and must be the last direct root child, after Popup/Tooltip siblings. It cannot also carry `popup_anchor` or a layer-level `activate` binding. Popups cannot anchor to it or its descendants. Closure uses ordinary `display: none` styles, not a serialized host callback/open-state mechanism. [Layout](../../docs/architecture/layout.md#implemented-modal-placement), [input](../../docs/architecture/input.md#implemented-modal-interaction) and [semantics](../../docs/architecture/semantics.md#implemented-prototype-projection) own its bounded placement, eligibility and `dialog` role. Older readers reject the unknown marker; ordinary Box/Text identity and canonical encoding remain unchanged.

## Canonical output

The additive collection vocabulary remains version 1. Box-only `collection_role` is `list`, `tree`, `menu`, `radio_group`, `tab_list`, `listbox`, `toolbar`, `combobox`, `group`, `color_picker`, `list_item`, `tree_item`, `menu_item`, `radio`, `tab` or `option`. Items are direct children of their matching collection. Optional boolean `selected` belongs only to list/tree items, radios, tabs and options, with at most one selected item per collection; optional boolean `expanded` belongs only to tree items (absence denotes a leaf), comboboxes and color pickers (required). Optional `tree_parent` is a strict reference to an earlier logical ancestor item in the same flat tree. Logical subtrees must be contiguous in preorder and at most 64 levels deep. Expansion bindings require a tree item with expanded state. A menu requires a stable ID and `popup_anchor`; every menu item, radio and tab requires an activate action, while collections, toolbars and groups themselves take none. A group names related ordinary controls, such as [VectorInput](../../docs/guides/vector-input.md) components. Menus contain items and noninteractive Text labels; item children are noninteractive Text, not nested controls. Tab lists likewise contain tabs and noninteractive Text, and tabs contain only noninteractive Text. No collection role, including another toolbar, may appear inside a toolbar; a combobox or color picker may. A combobox requires an activate action, `expanded` and `edit_value`, and contains only noninteractive Text. A listbox requires a stable ID and `popup_anchor` and contains options and noninteractive Text; every option requires an activate action and contains only noninteractive Text. A color picker requires an activate action, `expanded` and `edit_value`, and contains only noninteractive Text or Boxes without a collection role; its popup panel is an ordinary anchored group. [Collection authoring](../../docs/guides/tree-list.md), [menu authoring](../../docs/guides/menu-context.md), [RadioGroup authoring](../../docs/guides/radio-group.md), [Tabs/Toolbar authoring](../../docs/guides/tabs-toolbar.md), [ComboBox authoring](../../docs/guides/combobox.md) and [ColorPicker authoring](../../docs/guides/color-picker.md) own controlled application state; properties do not mutate host state or automatically change document styles. Older readers reject this new vocabulary. Existing documents keep their canonical encoding.

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
