# Localization

## Proposed boundary and ownership

Localization is UI runtime input, coordinated with text and layout through ordinary reactive dependencies. It covers message resolution and locale-sensitive formatting, rather than adding translation logic to individual widgets. A literal UTF-8 value remains useful for user content and non-translatable text; an application message retains its semantic identity until resolution.

| Responsibility | Owner |
| --- | --- |
| Locale identity, catalog fallback, message references/arguments, formatting, missing-translation policy | This page |
| Provider scope and lifetime, component bindings, common representation | [UI model](ui-model.md#proposed-context) |
| Dependency tracking and coherent publication | [Reactive runtime](reactive-runtime.md#proposed-presentation-integration) |
| Unicode segmentation, BiDi, shaping, font selection, line breaking | [Text](text.md#proposed-international-text-layout) |
| Logical edges, alignment and direction-aware placement | [Layout](layout.md#proposed-logical-direction-and-edges) |
| Resource acquisition and readiness delivery | Host through the [asset boundary](rendering.md#assets) |
| Observation records, source mapping and snapshot comparisons | [Inspection](inspection.md#proposed-localization-observations) |

Keep locale identity, catalog/formatting services, and Unicode/font services separable behind Tessera-owned interfaces. Components do not import formatter or font-library types, access translation files, or choose fallback rules. Core-only consumption must not require a locale-data library. Concrete module/package names and public signatures require a consumer and the [dependency adoption process](../reference/dependencies.md#adoption-requirements).

## Proposed locale context

Locale is an explicitly supplied reactive [subtree context](ui-model.md#proposed-context), with host-owned preferences and the nearest provider winning. It is not read implicitly from a process global or OS locale. Independent subtrees can use different locales without changing application data ownership.

Locale identity preserves BCP 47 language, script, region, variants and extensions rather than treating language alone as sufficient. Examples include `ja-JP`, `zh-Hans-CN`, `zh-Hant-TW`, and `sr-Latn`. Parsing, normalization, supported extensions, and invalid/unsupported-tag diagnostics must be defined before accepting authored values. Formatting preferences such as numbering system, calendar, hour cycle and measurement system are explicit inputs. Time zone and the value being formatted are supplied by the host; locale does not imply a clock or time zone.

A provider change invalidates its dependent message/formatting values and the affected text, layout, semantic names, and paint. Catalog resolution and text/layout work settle before one generation is published under the [update rules](architecture.md#update-and-snapshot-rules). Preserve node/component keys, command IDs, focus and user edits across a locale change; translations are presentation values rather than identity. Incremental evaluation must agree with resolving and recomputing the full tree.

## Proposed semantic message values

Candidate concepts are `MessageId`, typed named `MessageArgs`, and `LocalizedText` containing an ID and arguments. These are design vocabulary, not public C++ signatures or serialized fields. Keep the reference available through frontend lowering and bindings, resolve it before text measurement, and use the same resolved content for shaping and semantic labels in a generation. Do not eagerly erase it into a string at widget construction. Literal text and message references must be explicitly distinguishable in the common IR; encoded forms require their own versioned schema decision under [UI model](ui-model.md#common-representation).

Message IDs express stable meaning with namespaces, for example `common.save` or `editor.file.open`, rather than translated wording or position in the UI. Catalogs declare argument names/types and translator context. Shared product terms and glossary entries belong in translation resources so terminology is consistent. Define bounded references and cycle diagnostics if messages or terms can refer to one another.

Keep plural/select branches, interpolation, and locale-sensitive number/date/time/unit formatting in the message layer. Widgets must not reconstruct grammar with language-specific conditionals or concatenated fragments. Arguments retain their types until formatting; an invalid argument or unsupported formatter produces a diagnostic identifying the message, argument and resource source. Treat inserted user text as data and apply a declared bidirectional isolation policy with the [text subsystem](text.md#proposed-international-text-layout).

[MessageFormat 2](https://www.unicode.org/reports/tr35/tr35-messageFormat.html) is the design reference for structured messages and locale-sensitive selection/formatting. The catalog container, file syntax, bounded execution policy, formatter implementation and conformance subset are separate decisions; illustrative `.mf2` filenames do not define a resource format. Backend choices belong in [dependencies](../reference/dependencies.md#proposed-localization-choices).

## Proposed catalogs and fallback

Catalogs are owned, revisioned resources separated from UI source. The host acquires bytes; a catalog service validates them and exposes a coherent immutable revision to consumers. Declare catalog/schema versions, input limits, duplicate IDs, malformed patterns, argument mismatch, unsupported functions, and source locations before accepting resources. Reject invalid candidates without replacing the accepted revision.

One resolver owns the configured default locale and ordered, bounded catalog fallback policy. Distinguish catalog lookup from formatter/Unicode data fallback: they may use different data and must both be reproducible. Preserve script information and use declared locale-data rules or explicit host configuration; blindly stripping tag subtags is not a complete fallback algorithm. A chain such as `zh-Hant-HK -> zh-Hant -> default` is an illustrative configured policy, not a universal rule. Reject cycles and report unavailable configured catalogs.

Resolution exposes requested locale, selected catalog locale, message ID and catalog revision. A fallback hit differs from a key absent throughout the chain. The selected message's locale supplies its grammar/plural rules; any separate formatting-locale override must be explicit and observable. No widget implements its own lookup chain.

Development policy can show a conspicuous marker such as `⟦editor.node.delete⟧` and a located missing-message diagnostic. Release policy uses the configured fallback chain, then a declared safe terminal value if every catalog misses; never silently emit an empty label. Missing keys, fallback hits and formatting failures are separately reportable through a diagnostic sink/build report. Do not require network telemetry or expose argument contents by default. Exact terminal values and severity/strict-build controls are open policy decisions.

## Proposed catalog reload

Translation reload follows the candidate-validation and atomic-publication pattern of the [live reload transaction](path-finder-integration.md#live-reload-transaction), with catalog validation owned here and readiness delivered by the host. Parse and validate a complete candidate before an update point; publish its revision atomically and invalidate consumers of replaced messages. A rejected catalog keeps the last valid UI and reports resource/message source locations. A locale change or resource completion cannot mix catalog revisions within a published generation. Removed providers/owners drop late completions under the [async lifetime](ui-model.md#proposed-async-state) rules.

Reload does not rebuild the UI tree solely to change language, reset editing state, or bypass shaping/layout. The host owns file watching and loading; the runtime owns resolution and dependency effects.

## Proposed open decisions and verification

- Define concrete locale/message value types, provider defaults, normalization and validation bounds in the common IR before frontend convenience syntax.
- Choose a formatter and locale-data provider through a bounded consumer fixture, including C++ integration, data size/provenance, missing-data behavior and deterministic identity.
- Specify resource/schema encoding, argument/term reference rules, fallback policy and strict development/build diagnostics.
- Validate runtime locale switching and catalog replacement with stable identity, one coherent semantic/layout/paint generation, and last-valid-state preservation on failure.

The [testing guide](../guides/testing.md#localization-fixtures) owns resolver/formatting, pseudo-locale and selective visual procedures. [Inspection](inspection.md#proposed-localization-observations) owns agent-readable reports; [replay](replay.md#proposed-localization-inputs) owns reproducible locale/catalog transitions. Scheduling belongs in [backlog](../roadmap/backlog.md#later--localization-and-international-layout); live capability evidence belongs in [support](../reference/support-matrix.md).
