# EPG debug/fix

Correções aplicadas para investigar e corrigir o problema em que o EPG parecia não chamar a API.

## 1. UI da lista de canais

A lista estava exibindo sempre o texto fixo:

```cpp
EPG unavailable
```

Mesmo quando havia cache, job pendente ou dados retornados. Agora a tela usa:

```cpp
epgLineForChannel(ch)
```

Assim o app mostra corretamente:

- `EPG not loaded`
- `EPG unavailable`
- horário + título do programa atual

## 2. Logs de chamada da API

Foram adicionados logs para confirmar se a API está sendo chamada:

```txt
[KBORE] queueing N EPG request(s) for visible live channels
[KBORE] loading EPG for channel='...' provider='...' key='...'
[KBORE] EPG request path=/api/...
[KBORE] Parser API EPG POST http://.../api/...
[KBORE] EPG loaded for channel='...': N program(s)
```

Se esses logs não aparecerem, o problema está antes do consumo HTTP, geralmente em uma dessas condições:

- nenhum canal live carregado;
- `state_.hasManifest == false`;
- `state_.loadedChannels` vazio;
- thread de EPG não iniciou;
- tela ainda não entrou na listagem de canais.

## 3. Player força carga do EPG selecionado

Ao abrir o player, o app agora chama:

```cpp
loadSelectedEpg(false, true);
```

Antes estava:

```cpp
loadSelectedEpg(false, false);
```

Com `false`, a função apenas lia cache/memória e nunca fazia request remoto direto para o canal selecionado.

## 4. Parsing de resposta mais defensivo

`epgPageFromJson` agora aceita resposta com wrapper:

```json
{
  "data": {
    "programs": []
  }
}
```

ou:

```json
{
  "programs": []
}
```

Isso evita falha se o backend retornar `data.programs`, `epg.programs`, `programs`, `epg_listings` ou `items`.

## Como validar

Rode o app e procure os logs no terminal/devkit:

```txt
[KBORE] EPG request path=/api/epg/programs
```

ou para Xtream:

```txt
[KBORE] EPG request path=/api/xtream/epg/short
```

Se aparecer `EPG loaded ...: 0 program(s)`, a API foi chamada, mas não encontrou programação para aquele canal. Nesse caso o próximo ponto é validar matching por `tvgId`, `tvgName`, `channelName` ou `streamId`.
