# Correção de crash ao abrir categoria

## Sintoma

O app crashava ao abrir uma categoria/item pai. O comportamento aparecia logo após a navegação tentar carregar ou validar EPG dos itens visíveis.

## Causa principal

A função `loadEpgPage(...)` em `source/storage.cpp` estava incompleta:

```cpp
bool loadEpgPage(...) {
  std::ifstream file(epgCachePath(playlistId, channel), std::ios::binary);
}
```

Ela retornava sem `return true/false`. Em C++, cair no fim de uma função `bool` sem retorno gera comportamento indefinido. Como `loadVisibleEpgForChannelList()` chama `loadEpgPage(...)` ao abrir uma categoria com itens live, isso podia corromper o fluxo e derrubar o app.

## Correção aplicada

`loadEpgPage(...)` agora:

- retorna `false` quando o arquivo de cache não existe;
- lê o JSON do cache;
- parseia para `EpgPage`;
- retorna `true` quando o cache é válido;
- retorna `false` em erro de parse.

Também foi corrigida a persistência de `MediaNode` em cache/manifest local para preservar campos de EPG:

- `tvgId`
- `tvgName`
- `streamId`

Isso evita perder os identificadores necessários para chamar `/api/epg/programs` ou `/api/xtream/epg/short` depois que o manifesto/nodes são recarregados do cache.

## Arquivos alterados

- `source/storage.cpp`

## Validação local

Foram validados com compilação C++17:

```bash
g++ -std=c++17 -Iinclude -c source/storage.cpp
g++ -std=c++17 -Iinclude -c source/app.cpp
g++ -std=c++17 -Iinclude -c source/parser_api_client.cpp
```
