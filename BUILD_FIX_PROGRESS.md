# Correção de linkedição: ParserApiClient::progress

## Problema

O build do Switch falhava no link com:

```txt
undefined reference to `nstv::ParserApiClient::progress(std::string const&) const`
```

## Causa

O método estava declarado em:

```cpp
include/nstv/parser_api_client.hpp
```

mas a implementação não estava presente no arquivo compilado:

```cpp
source/parser_api_client.cpp
```

Como `loadManifestEndpointWithCacheText(...)` chama `progress(...)` em vários pontos, o compilador gerava o objeto, mas o linker não encontrava a definição final do método.

## Correção aplicada

Foi adicionada a implementação em `source/parser_api_client.cpp`:

```cpp
void ParserApiClient::progress(const std::string &message) const {
  if (progress_) {
    progress_(message);
  }
}
```

## Validação local

Validação feita com:

```bash
g++ -std=c++17 -Iinclude -c source/parser_api_client.cpp -o /tmp/parser_api_client.o
nm -C /tmp/parser_api_client.o | grep 'ParserApiClient::progress'
```

Resultado esperado:

```txt
T nstv::ParserApiClient::progress(std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const&) const
```

