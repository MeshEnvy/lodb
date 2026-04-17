# Changelog

All notable changes to LoDB will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- **PlatformIO** `library.json` with dependencies on LoFS, nanopb (source zip), and rweather/Crypto.
- **Shipped nanopb codegen** for `lodb.DiagnosticsTest`: `include/lodb/diagnostics.pb.h` plus `src/diagnostics.pb.c`; maintainer regen via `scripts/regen_nanopb.sh`.
- **`LODB_VERSION`**, default no-op **`LODB_LOG_*`**, and weak **`lodb_now_ms()`** (default `millis()`).

### Removed

- Alternate public header **`lodb.h`** (case-only alias of **`LoDB.h`**). Use **`#include <lodb/LoDB.h>`** only.

### Changed

- Public headers under **`include/lodb/`** — use **`#include <lodb/LoDB.h>`**. Shipped **`diagnostics.pb.h`** lives there; **`diagnostics.pb.c`** stays in **`src/`** for PlatformIO compilation.
- Core uses **LoFS** only (no direct `FSCom` / outer `spiLock` in LoDB). **`LoFS::FSType::AUTO`** uses legacy **`/lodb/...`** for existing on-flash data; **`INTERNAL`** / **`SD`** use **`/internal/lodb/...`** and **`/sd/lodb/...`**.
- **`truncate()`** / **`drop()`** table lifecycle APIs (unchanged API surface from the LoFS migration branch).
- **`lodb_new_uuid`** auto branch uses **`lodb_now_ms()`** instead of Meshtastic **`getTime()`** in the library default.
- **`diagnostics.proto`** moved to **`package lodb`** (was `meshtastic`); diagnostics code uses **`lodb_DiagnosticsTest_*`**.

### Removed

- Meshtastic **module** surface (`LoDBModule`, `plugin.h`, legacy module registration macros).
- **`examples/log-messages/`** demo module.
- **`.gitignore` `*.pb.*` rule** so shipped `diagnostics.pb.{h,c}` stay tracked.

## [1.2.0] - 2025-12-09

### Minor
- `freeRecords()` helper method to free records returned by `select()`
- `count()` method to count records in a table with optional filtering
- Logo assets with logo.pxd and logo.webp

## [1.1.0] - 2025-12-05

### Patch
- Version bump

## [1.0.2] - 2025-12-05

### Patch
- Updated installation documentation for the plugin distribution model

## [1.0.0] - 2025-11-28

### Minor
- Protobuf generation is now automatic (no manual `gen_proto.py` script needed)
- Example code demonstrating LoDB usage

## [Initial Release] - 2025-11-06

### Major
- Synchronous, protobuf-based database for Meshtastic
- CRUD operations (Create, Read, Update, Delete)
- SELECT queries with filtering, sorting, and limiting
- Deterministic UUID generation from strings
- Auto-generated UUID support
- Thread-safe filesystem-based storage
- Protocol Buffers integration with nanopb

[Unreleased]: https://github.com/MeshEnvy/lodb/compare/v1.2.0...HEAD
[1.2.0]: https://github.com/MeshEnvy/lodb/compare/v1.1.0...v1.2.0
[1.1.0]: https://github.com/MeshEnvy/lodb/compare/v1.0.2...v1.1.0
[1.0.2]: https://github.com/MeshEnvy/lodb/compare/v1.0.0...v1.0.2
[1.0.0]: https://github.com/MeshEnvy/lodb/compare/93496d6...v1.0.0
