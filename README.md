# nano-osc

A small, portable* OpenSoundControl core with optional client, server and transport abstractions.

### Goals

* Zero dependencies
* Cross platform
* Batteries included (optional)
* Single threaded runtime (async capable transports can be layered on top)

### Architecture

The library is split into a small OSC packet core and optional batteries:

* `nanoosc::core` / `#include <nanoosc/core.hpp>`
  * OSC values, messages, bundles, non-owning views, decode, validation, and encode APIs.
  * No sockets, no platform headers, no transport/runtime classes.
* `nanoosc::runtime` / `#include <nanoosc/runtime.hpp>`
  * Transport abstraction plus `OSCClient` and `OSCServer`.
  * Transport-agnostic and single threaded.
* `nanoosc::runtime_stats` / `#include <nanoosc/runtime_stats.hpp>` (optional, default off)
  * Stats-enabled runtime sibling with packet rates, top-level message/bundle rates, runtime drops, OS drops, and compact text snapshots.
* `nanoosc::udp` / `#include <nanoosc/udp_transport.hpp>`
  * POSIX UDP transport battery.
* `nanoosc::nanoosc` / `#include <nano-osc.hpp>`
  * Umbrella target/header for default builds.

## Supported Features

OpenSoundControl [Spec 1.0](https://opensoundcontrol.stanford.edu/spec-1_0.html)

- `Bundle`
    - [x] Decode
    - [x] Encode

- `Message`
    - [x] Decode
    - [x] Encode

- Types
    - [x] `i` : `int32`
    - [x] `f` : `float32`
    - [x] `s` : `string`
    - [x] `b` : `blob`
    - [x] `h` : `int64`
    - [x] `d` : `double`
    - [x] `S` : `string` (symbol)
    - [x] `t` : `timetag` (NTP 64-bit)
    - [x] `c` : `ascii` char 32bit
    - [x] `r` : `RGBA` colour 32bit
    - [x] `m` : `MIDI` 4 byte MIDI message
    - [x] `T` : `bool`
    - [x] `F` : `bool`
    - [x] `N` : `Nil`
    - [x] `I` : tbd (see spec 1.1)
    - [ ] `[` : Array start
    - [ ] `]` : Array end

### Examples

* [osc-client](./examples/osc-client/) with default `UDPTransport`
* [osc-server](./examples/osc-server/) with default `UDPTransport` and simple message handler lambda.
* [osc-stats-server](./examples/osc-stats-server/) with `OSCStatsServer`, enabled by `-DNANOOSC_BUILD_RUNTIME_STATS=ON`.

### Building & Testing

```sh
cmake -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Options:
* `-DNANOOSC_BUILD_RUNTIME=OFF` builds only the OSC packet core
* `-DNANOOSC_BUILD_RUNTIME_STATS=ON` builds the optional runtime stats battery
* `-DNANOOSC_BUILD_UDP=OFF` skips the POSIX UDP transport battery
* `-DBUILD_EXAMPLES=OFF` skips the examples
* `-DBUILD_TESTING=OFF` skips the unit tests

Runtime stats are opt-in and keep the default `OSCServer` and `UDPTransport` paths unchanged. See
[osc-stats-server](./examples/osc-stats-server/) for the intended `OSCStatsServer` usage. Its snapshot
output is shaped like:

```text
rx packets/sec      4500
top messages/sec    18000
bundles/sec         12.0

os drops            120 (per_socket)
runtime drops       0
```
