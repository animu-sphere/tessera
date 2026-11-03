# Generation history

> Proposed design, not yet implemented. Active scope is in [current work](../roadmap/current.md).

## Proposed generation history

Replay already describes progression between generations; time-travel inspection keeps selected observations per generation so tools can step through them. A history entry can hold the document generation, declared state snapshot, semantic tree, layout, focus, the command or action that produced it, paint counters, and resource readiness. DevTools then show differences between generations, focus changes, layout changes, command history, and resource transitions. History is bounded by count or bytes with explicit eviction, observes only owned data, and stays a development facility. A failure reproduction bundle packages the replay recording with the observations needed to reproduce it; a performance regression bundle adds the measurement conditions.

## Promotion criteria

Promote a retained history only when a concrete debugging or reproduction fixture needs observations from more than the accepted generation. Record the retention budget and required comparison; do not add a general time-travel system for completeness.
