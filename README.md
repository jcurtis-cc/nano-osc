# nano-osc

A small, portable* OpenSoundControl implementation with client, server and transport abstractions.

### Goals

* Zero dependencies
* Cross platform
* Batteries included (optional)
* Single threaded (async supported)

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
    - [ ] `c` : `ascii` char 32bit
    - [ ] `r` : `RGBA` colour 32bit
    - [ ] `m` : `MIDI` 4 byte MIDI message
    - [ ] `T` : `bool`
    - [ ] `F` : `bool`
    - [ ] `N` : `Nil`
    - [ ] `I` : tbd (see spec 1.1)
    - [ ] `[` : Array start
    - [ ] `]` : Array end

### Examples

* [osc-client](./examples/osc-client/) with default `UDPTransport`
* [osc-server](./examples/osc-server/) with default `UDPTransport` and simple message handler lambda.

### Building & Testing

```sh
cmake -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Options:
* `-DBUILD_EXAMPLES=OFF` skips the client/server examples
* `-DBUILD_TESTING=OFF` skips the unit tests
