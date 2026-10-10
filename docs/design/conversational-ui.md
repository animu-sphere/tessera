# Conversational and agent workspaces

## Proposed scope

Conversational applications (chat assistants, coding agents, AI workspaces) are a primary reference workload for Tessera. They are structured, asynchronous, streaming UI, not a text transcript in a scroll view. The same primitives serve IDEs, terminals, log viewers, documentation tools, dashboards, and agent control panels. This page proposes a design.

Chat is one workspace primitive, not the whole application. A workspace can combine conversation with code and diffs, files, tool activity, artifacts, terminals, [node graphs](graph-editor.md), and host-owned 2D/3D or OpenUSD viewports in one native runtime.

## Proposed layering and provider independence

```text
core: layout, text, rendering, input, virtualization, update/async boundaries
widgets: text editor, rich text/Markdown, code block, docking, attachments
optional agent UI library: conversation, tool call, approval, agent timeline, artifacts
application: provider adapters, networking, tool execution, persistence
```

Only broadly reusable primitives belong in core or widgets; AI-specific views live in an optional library. Package boundaries follow [module organization](architecture.md#module-organization) and are decided with consumers.

Tessera embeds no provider message schema, model name, token accounting, prompt format, or proprietary tool-call schema. Application adapters map each provider or local agent protocol into a generic conversation model. This keeps the UI independent of fast-changing model APIs, consistent with the agent-neutral [core constraints](architecture.md#purpose-and-constraints).

## Proposed content model

A message has a stable ID, a role, and an ordered list of content blocks: text, Markdown, code, image, file, tool call, tool result, citation, status, artifact, or a custom block. Messages keep parent links, so regeneration, edits, rewinds, checkpoints, and forks can form a conversation graph later while the first presentation is linear.

These are application view models. They lower to ordinary components under the [UI model](ui-model.md#common-representation) rather than adding conversation node types to the runtime. Textual content and interactive content (choice buttons, sliders, confirm/cancel prompts) can coexist in one message. Applications register renderers for custom block types (charts, forms, previews, domain inspectors) without modifying Tessera.

## Proposed list virtualization requirements

Long conversations need a virtualized list of variable-height items with stable keys: lazy realization, fast append and prepend, dynamic height correction, and anchor-based restoration of an item key plus local offset rather than a pixel offset. Follow-end mode keeps the newest content visible, stops when the user scrolls away, and resumes explicitly. Prepending history must not move visible content. Geometry rules are owned by [layout](layout.md#scrolling-and-overlay-geometry). Log viewers, terminals, and diff views reuse the same requirements.

## Proposed streaming and asynchronous updates

Incoming chunks append to application state and reach the runtime only at [update points](architecture.md#update-and-snapshot-rules). Many network events per second (for example 50-200) coalesce into at most one update generation per frame, so layout and GPU submission follow the display rate rather than the token rate. Rebuilding or diffing the whole conversation per chunk is excluded. Limiting invalidation to the affected message subtree is the intended optimization, introduced only with parity to full-tree results.

Asynchronous execution (network, coroutines, threads, subprocesses) is host-owned. A host-side or optional-library bridge delivers results at update points, propagates errors, and cancels or detaches tasks owned by a removed component; the core gains no coroutine runtime or multithreaded API. A request moves through application states such as idle, submitting, connecting, streaming, running tools, completed, failed, and cancelled, presented through the shared [async state](ui-model.md#proposed-async-state) vocabulary. Stop is available in every intermediate state, and cancellation propagates through agent, network, tool, and subprocess services where supported.

Markdown parsing, syntax highlighting, diff parsing, image decoding, attachment metadata, and search indexing may run off the UI thread in the host. Their results are immutable document patches applied at update points, preserving selection and scroll anchors.

## Proposed rich text, Markdown, and code

Markdown renders natively: source, parser, document AST, then ordinary components (heading, rich text paragraph, code block, table, quote, list) with native theming, selection, semantics, and inspection. Normal rendering needs no HTML conversion or WebView. The initial set covers paragraphs, headings, emphasis, inline code, fenced code, links, block quotes, ordered/unordered/nested lists, rules, and tables. Task lists, footnotes, math, diagrams, and custom interactive blocks come later. Read-only rich text is distinct from the deferred rich-text editor suite.

Rich text needs styled spans, hyperlinks, inline components, selection across read-only text, and copy. Text requirements (grapheme handling, emoji, fallback, bidirectional text, Japanese line breaking, word boundaries) are owned by [text](text.md); editing and IME by [input](input.md#proposed-editing-ime-and-clipboard-boundary).

A code block supports selection, copy, horizontal scrolling, optional line numbers and wrapping, a language label, collapse, search, and virtualization for very large sources. Syntax highlighting comes through an injected interface. Inline, unified, and side-by-side diffs, annotations, diagnostics, file references, and apply actions are later extensions.

## Proposed composer

The input area is a multiline editor with first-class Japanese IME, selection, clipboard, undo/redo, attachments from drag/drop and pasted images with previews, and completion for slash commands and mentions of files, workspaces, or tools. Submit and newline keys (for example Enter and Shift+Enter) are declared key-to-action mappings under [input](input.md#event-to-action-boundary), not hard-coded behavior.

## Proposed tool, approval, and agent views

Tool calls are structured objects with an ID, name, input, output, and state: pending, running, waiting for approval, completed, failed, or cancelled. Views offer a collapsed summary, expandable input/output, duration, progress, logs, retry, cancel, copy, and error details.

Approval requests describe a capability, description, risk level, scope, and the action to permit (allow once, always allow, deny). They are generic, not tied to one model API. An agent timeline presents nested, timed entries (reasoning status, tool calls, file reads/writes, commands, builds, tests, screenshots, approvals, errors, checkpoints) with collapse and progress.

Artifacts separate generated work from the conversation: documents, code, images, tables, terminals, diffs, node graphs, previews, USD stages, or custom types, shown in a docked panel. Attachments cover images, audio, files, documents, URLs, and metadata, with thumbnails, previews, save/open actions, upload progress, and error state.

## Proposed accessibility

Messages carry semantic roles, readable boundaries, heading levels, code and link semantics, focus navigation, and complete keyboard operation through [semantics](semantics.md). Streaming announcements use live regions sparingly. Reduced motion and high contrast apply through ordinary styling.

## Proposed reference application and fixtures

A reference application serves as both showcase and stress test, exercising text, Markdown, code, selection, IME, virtualization, streaming, async work, cancellation, attachments, tool calls, docking, artifacts, accessibility, and snapshots. Planning scenarios:

- Long conversation: 10,000 variable-height messages, smooth scrolling, immediate jump to latest.
- Streaming Markdown: continuous chunks with stable selection and no full-document relayout.
- Large code: 50,000 lines with highlighting, horizontal scrolling, selection, and search.
- Tool-heavy agent: hundreds of tool calls with live status, nesting, and logs.
- Mixed workspace: conversation, editor, terminal, node graph, and viewport together.

These are workload fixtures, not support claims; budgets follow recorded baselines under [inspection](inspection.md#proposed-diagnostics-and-comparisons). Automation uses the shared inspection boundary: open a conversation, inject a message, start and advance a stream, open a tool result, scroll to a message, capture, and inspect. Stream progress is declared fixture input, never wall-clock timing. Delivery scope is owned by [backlog](../roadmap/backlog.md).

## Verification

Test anchor preservation during prepend and height correction, follow-end cancellation and resume, chunk coalescing into update generations, incremental-versus-full parity, Markdown AST-to-component mapping, cancellation at each request state, and stale task results after component removal. Use structural assertions first and a few deterministic images under the [testing strategy](../guides/testing.md).
