# Build fix: ParserApiClient::progress undefined reference

## Problem

The Switch build failed at link time with:

```txt
undefined reference to `nstv::ParserApiClient::progress(std::string const&) const`
```

`ParserApiClient::progress(...)` was declared in `include/nstv/parser_api_client.hpp` and used from `source/parser_api_client.cpp`, but the method implementation was missing.

## Fix applied

Added the implementation in `source/parser_api_client.cpp`:

```cpp
void ParserApiClient::progress(const std::string &message) const {
  if (progress_) {
    progress_(message);
  }
}
```

## Why this fixes it

The compiler accepted the source because the method was declared in the header, but the linker could not find the compiled symbol. Adding the implementation resolves the missing symbol while keeping progress reporting optional and safe when no callback is configured.

## Note

The warning about `ensureDir` being unused is not fatal. It can be cleaned later, but it is not the cause of the build failure.
