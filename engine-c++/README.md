# Engine C++

This directory contains a C++ port of the matching-engine logic from `engine/`.

## What is included

- Orderbook matching, cancellation, and depth aggregation.
- Balance locking and settlement logic.
- A typed C++ message model for API, DB, and websocket events.
- Snapshot persistence in a simple text format for the C++ version.

## Build

```bash
cmake -S . -B build
cmake --build build
```

Or use the bundled Makefile:

```bash
make build
make test
make run
```

## Run With Redis

```bash
./build/engine_app
```

The engine uses `127.0.0.1:6379` by default. You can still override it when needed:

```bash
REDIS_HOST=127.0.0.1 REDIS_PORT=6379 ./build/engine_app
```

The engine consumes JSON messages from the shared `messages` list, pushes DB work to `db_processor`, and publishes API/websocket payloads back onto Redis channels.

## Notes

- The TypeScript Redis transport is not copied one-to-one; the engine core is exposed through typed callbacks so it can be wired to Redis or another transport later.
- The snapshot format here is C++-native and not JSON-compatible with the TypeScript engine snapshot.
