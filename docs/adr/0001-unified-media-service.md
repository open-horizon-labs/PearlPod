# ADR 0001: One media service with adapters and an in-memory bus

Date: October 19, 2026. Status: proposed for implementation; runtime cutover remains gated on acceptance. Decision owner: the user, who requested the unified service, multiple adapters and an in-memory bus architecture.

## Context

PearlPod has a working NAS-side Plex preparation/cache/publication and FTP delivery pipeline. Book Reader Thingee has ABS identity/progress, native audio and buffered HTTP downloads; reader-triggered preparation inside an ABS source patch introduced waiting and unnecessary peak-driven re-encodes. The required workflow prepares selections before devices connect and supports music, podcasts and books across device capabilities without upstream forks.

## Decision

Extend the existing Python host service into one application with explicit source, preparation, device, delivery and progress adapters. Use typed bounded in-memory command/event queues for coordination. Persist desired selections, rendition jobs, dirty delivery plans, receipts and accepted pending progress in a separate local SQLite state store; reconstruct work independently of event delivery. Keep large byte streams outside the bus. Retain the existing filesystem object cache and legacy device transports during migration. Use a standard reverse-proxy front end for stock ABS pass-through and explicit prepared-media routes.

Do not implement an external message broker, generic event store or plugin discovery framework. Source/account/item/episode identity remains separate from rendition and transport addresses. Preparation happens on selection changes/reconciliation, with fixed versioned recipes and no gain-search loop. Device capabilities gate the entire user workflow, not codec decoding alone.

## Alternatives

Extending the current source/device conditionals retains the global-lock and coupling problem. A lazy proxy leaves devices waiting. Distributed services plus a broker add operating cost without a present multi-host requirement. These alternatives and their falsifying tests are recorded in [the solution-space analysis](../UNIFIED-MEDIA-SERVICE-SOLUTION.md).

## Consequences

One deployment/application can reuse preparation across devices while preserving native upstream state. The durable store, supervision and adapter contracts add implementation work; in return restart loss, duplicate work and cross-pipeline blocking have explicit ownership and tests. A single process remains a shared failure domain. Existing PearlPod music and reader offline behavior must survive a staged migration. Podcast policy and the full device/media matrix require explicit capability work; they are not automatically solved by a shared cache. Revisit the deployment boundary if work must span hosts.

## Dissent disposition and review trigger

ADJUST: keep transient coordination separate from durable obligations, isolate adapter failures and retain source-specific authority. Prove restart boundaries, per-account access, upstream pass-through, concurrent music/book behavior and actual device capabilities before cutover. Stop on lost accepted work, degraded upstream behavior, false firmware capability claims or an implementation that again waits for cold conversion on the device.
