# Kboré — Métodos de EPG para o Frontend Consumir

Este documento descreve os métodos disponíveis para o frontend consumir EPG no Kboré, cobrindo listas **M3U**, **Xtream Codes**, EPG XMLTV completo, EPG curto por canal e fallback defensivo.

O objetivo é permitir que o app mostre programação de canais sem travar a navegação, sem bloquear o carregamento da lista e sem depender de uma única estrutura de playlist.

---

## 1. Conceito geral

O EPG deve ser tratado como um recurso complementar da playlist.

A playlist deve carregar primeiro:

1. categorias;
2. canais;
3. VOD;
4. séries;
5. árvore dinâmica de navegação.

Depois disso, o frontend deve carregar EPG sob demanda, preferencialmente quando:

- o usuário seleciona um canal;
- o canal aparece visível na lista;
- o player abre;
- o usuário acessa uma aba de programação.

O app **não deve travar** se o EPG falhar.

---

## 2. Tipos de EPG suportados

Existem três cenários principais.

### 2.1 M3U com EPG no cabeçalho

Algumas listas M3U trazem a URL do EPG no próprio cabeçalho:

```m3u
#EXTM3U url-tvg="https://servidor.com/epg.xml.gz"
```

ou:

```m3u
#EXTM3U x-tvg-url="https://servidor.com/xmltv.php?username=user&password=pass"
```

Nesse caso, o backend consegue descobrir a origem do EPG automaticamente.

---

### 2.2 M3U com EPG manual

Algumas listas M3U não informam a URL do EPG.

Nesse caso, o frontend pode permitir que o usuário informe manualmente uma URL de EPG:

```json
{
  "playlistUrl": "https://servidor.com/lista.m3u",
  "epgUrl": "https://servidor.com/epg.xml.gz"
}
```

---

### 2.3 Xtream Codes

Em listas Xtream, o EPG normalmente pode ser obtido por:

```txt
/xmltv.php?username=USER&password=PASS
```

Além disso, muitos servidores Xtream também oferecem EPG curto por canal via:

```txt
/player_api.php?action=get_short_epg&stream_id=STREAM_ID
```

Esse método é mais leve e deve ser preferido para o canal selecionado.

---

## 3. Base URL da API

Nos exemplos abaixo, será usado:

```txt
PARSER_API_BASE_URL=http://localhost:3000
```

No frontend, recomenda-se centralizar isso em uma variável de configuração:

```ts
export const environment = {
  parserApiBaseUrl: 'http://localhost:3000'
};
```

---

# 4. Métodos genéricos / M3U

## 4.1 Descobrir origem do EPG

Usado para descobrir se uma playlist M3U possui EPG declarado.

### Endpoint

```http
POST /api/epg/source
```

### Quando usar

Use quando:

- a playlist for M3U;
- o frontend ainda não sabe se existe EPG;
- o usuário adicionou uma nova playlist;
- o app quer validar uma URL de EPG antes de salvar.

### Request usando URL da playlist

```json
{
  "url": "https://servidor.com/playlist.m3u"
}
```

### Request usando conteúdo M3U já carregado

```json
{
  "content": "#EXTM3U url-tvg=\"https://servidor.com/epg.xml.gz\"\n#EXTINF:-1 tvg-id=\"canal1\",Canal 1\nhttp://...",
  "source": "inline"
}
```

### Response esperado

```json
{
  "success": true,
  "provider": "m3u",
  "available": true,
  "epgUrl": "https://servidor.com/epg.xml.gz",
  "format": "xmltv",
  "compressed": true
}
```

### Response sem EPG

```json
{
  "success": true,
  "provider": "m3u",
  "available": false,
  "epgUrl": null
}
```

### Como o frontend deve usar

Se `available === true`, salve `epgUrl` junto da playlist local.

Se `available === false`, mantenha o app funcionando e permita URL manual opcional.

---

## 4.2 Obter XMLTV bruto

Retorna o XML do EPG sem parsear.

### Endpoint

```http
POST /api/epg/raw
```

### Quando usar

Use apenas se o frontend precisar:

- depurar EPG;
- baixar XMLTV completo;
- salvar cache bruto local;
- inspecionar canais do XMLTV.

Para UI normal, prefira `/api/epg/programs`.

### Request usando `epgUrl` manual

```json
{
  "provider": "m3u",
  "epgUrl": "https://servidor.com/epg.xml.gz"
}
```

### Request usando URL da playlist

```json
{
  "provider": "m3u",
  "url": "https://servidor.com/playlist.m3u"
}
```

### Response esperado

```xml
<?xml version="1.0" encoding="UTF-8"?>
<tv>
  <channel id="canal1">
    <display-name>Canal 1</display-name>
  </channel>
  <programme channel="canal1" start="20260531120000 -0300" stop="20260531130000 -0300">
    <title>Programa Exemplo</title>
  </programme>
</tv>
```

### Observação

Esse endpoint deve retornar `Content-Type: application/xml`.

---

## 4.3 Obter programas parseados

Esse é o endpoint principal para frontend M3U.

### Endpoint

```http
POST /api/epg/programs
```

### Quando usar

Use para mostrar:

- programa atual;
- próximo programa;
- lista de programação do canal;
- grade por horário;
- informações no overlay do player.

### Request por `channelId`

```json
{
  "provider": "m3u",
  "url": "https://servidor.com/playlist.m3u",
  "channelId": "globo.br",
  "from": "2026-05-31T00:00:00-03:00",
  "to": "2026-06-01T00:00:00-03:00",
  "page": 1,
  "pageSize": 50
}
```

### Request por `tvgId`

```json
{
  "provider": "m3u",
  "epgUrl": "https://servidor.com/epg.xml.gz",
  "tvgId": "globo.br",
  "from": "2026-05-31T00:00:00-03:00",
  "to": "2026-06-01T00:00:00-03:00",
  "page": 1,
  "pageSize": 50
}
```

### Request por nome do canal

```json
{
  "provider": "m3u",
  "epgUrl": "https://servidor.com/epg.xml.gz",
  "channelName": "Globo",
  "from": "2026-05-31T00:00:00-03:00",
  "to": "2026-06-01T00:00:00-03:00",
  "page": 1,
  "pageSize": 50
}
```

### Response esperado

```json
{
  "success": true,
  "provider": "m3u",
  "channelId": "globo.br",
  "page": 1,
  "pageSize": 50,
  "total": 2,
  "programs": [
    {
      "id": "globo.br-20260531120000",
      "channelId": "globo.br",
      "channelName": "Globo",
      "title": "Jornal Hoje",
      "description": "Noticiário diário.",
      "start": "2026-05-31T12:00:00-03:00",
      "end": "2026-05-31T13:00:00-03:00",
      "category": "News",
      "icon": null,
      "raw": {}
    },
    {
      "id": "globo.br-20260531130000",
      "channelId": "globo.br",
      "channelName": "Globo",
      "title": "Esporte Espetacular",
      "description": "Programa esportivo.",
      "start": "2026-05-31T13:00:00-03:00",
      "end": "2026-05-31T14:30:00-03:00",
      "category": "Sports",
      "icon": null,
      "raw": {}
    }
  ]
}
```

---

# 5. Métodos Xtream Codes

## 5.1 Descobrir URL XMLTV do Xtream

### Endpoint

```http
POST /api/xtream/epg/source
```

### Quando usar

Use quando:

- a playlist for Xtream;
- o frontend quiser salvar a URL XMLTV inferida;
- o app precisar validar se o backend consegue montar o EPG.

### Request

```json
{
  "url": "http://servidor.com/player_api.php?username=user&password=pass"
}
```

Também pode ser enviado com `baseUrl`, `username` e `password` separados, se o backend suportar:

```json
{
  "baseUrl": "http://servidor.com",
  "username": "user",
  "password": "pass"
}
```

### Response esperado

```json
{
  "success": true,
  "provider": "xtream",
  "available": true,
  "epgUrl": "http://servidor.com/xmltv.php?username=user&password=pass",
  "format": "xmltv",
  "compressed": false
}
```

---

## 5.2 Obter XMLTV bruto do Xtream

### Endpoint

```http
POST /api/xtream/epg/raw
```

### Quando usar

Use para obter o XMLTV completo do Xtream.

Não é recomendado para cada troca de canal, pois pode ser pesado.

### Request

```json
{
  "url": "http://servidor.com/player_api.php?username=user&password=pass"
}
```

ou:

```json
{
  "baseUrl": "http://servidor.com",
  "username": "user",
  "password": "pass"
}
```

### Response esperado

```xml
<?xml version="1.0" encoding="UTF-8"?>
<tv>
  <channel id="12345">
    <display-name>Canal Exemplo</display-name>
  </channel>
  <programme channel="12345" start="20260531120000 -0300" stop="20260531130000 -0300">
    <title>Programa Exemplo</title>
  </programme>
</tv>
```

---

## 5.3 Obter programas Xtream via XMLTV completo

### Endpoint

```http
POST /api/xtream/epg/programs
```

### Quando usar

Use como fallback quando:

- `/api/xtream/epg/short` não retornar dados;
- o servidor Xtream não suportar short EPG;
- o canal não tiver `streamId` válido;
- o app quiser programação com janela maior.

### Request por `streamId`

```json
{
  "url": "http://servidor.com/player_api.php?username=user&password=pass",
  "streamId": 12345,
  "from": "2026-05-31T00:00:00-03:00",
  "to": "2026-06-01T00:00:00-03:00",
  "page": 1,
  "pageSize": 50
}
```

### Request por `tvgId`

```json
{
  "url": "http://servidor.com/player_api.php?username=user&password=pass",
  "tvgId": "12345",
  "from": "2026-05-31T00:00:00-03:00",
  "to": "2026-06-01T00:00:00-03:00",
  "page": 1,
  "pageSize": 50
}
```

### Response esperado

```json
{
  "success": true,
  "provider": "xtream",
  "channelId": "12345",
  "page": 1,
  "pageSize": 50,
  "total": 2,
  "programs": [
    {
      "id": "12345-20260531120000",
      "channelId": "12345",
      "channelName": "Canal Exemplo",
      "title": "Programa Atual",
      "description": "Descrição do programa.",
      "start": "2026-05-31T12:00:00-03:00",
      "end": "2026-05-31T13:00:00-03:00",
      "category": null,
      "icon": null,
      "raw": {}
    }
  ]
}
```

---

## 5.4 Obter short EPG Xtream por canal

Esse é o método preferencial para o canal selecionado em listas Xtream.

### Endpoint

```http
POST /api/xtream/epg/short
```

### Quando usar

Use quando:

- o canal possui `streamId`;
- o usuário selecionou um canal;
- o player abriu;
- o app quer apenas `NOW` e `NEXT`;
- o app quer reduzir processamento de XMLTV completo.

### Request

```json
{
  "url": "http://servidor.com/player_api.php?username=user&password=pass",
  "streamId": 12345,
  "limit": 10
}
```

ou:

```json
{
  "baseUrl": "http://servidor.com",
  "username": "user",
  "password": "pass",
  "streamId": 12345,
  "limit": 10
}
```

### Response esperado

```json
{
  "success": true,
  "provider": "xtream",
  "streamId": 12345,
  "programs": [
    {
      "id": "12345-20260531120000",
      "channelId": "12345",
      "title": "Programa Atual",
      "description": "Descrição do programa atual.",
      "start": "2026-05-31T12:00:00-03:00",
      "end": "2026-05-31T13:00:00-03:00",
      "raw": {}
    },
    {
      "id": "12345-20260531130000",
      "channelId": "12345",
      "title": "Próximo Programa",
      "description": "Descrição do próximo programa.",
      "start": "2026-05-31T13:00:00-03:00",
      "end": "2026-05-31T14:00:00-03:00",
      "raw": {}
    }
  ]
}
```

---

# 6. Ordem recomendada de consumo no frontend

## 6.1 Para playlist M3U

Ao adicionar ou atualizar a playlist:

1. Chamar `/api/epg/source`.
2. Se encontrar `epgUrl`, salvar no storage/config local.
3. Se não encontrar, permitir `epgUrl` manual.
4. Não baixar todo EPG imediatamente, a menos que o usuário abra a tela de programação.

Ao selecionar um canal:

1. Ler `channel.tvgId`.
2. Se não existir, tentar `channel.tvgName`.
3. Se não existir, tentar `channel.name`.
4. Chamar `/api/epg/programs`.
5. Salvar resultado em cache local por canal.
6. Exibir `NOW` e `NEXT`.

Fluxo:

```txt
M3U channel selected
  -> has tvgId?
      yes -> /api/epg/programs with tvgId
      no  -> has tvgName?
              yes -> /api/epg/programs with channelName
              no  -> /api/epg/programs with channelName = channel.name
```

---

## 6.2 Para playlist Xtream

Ao adicionar ou atualizar a playlist:

1. Chamar `/api/xtream/epg/source` opcionalmente.
2. Salvar `epgUrl` inferido, se disponível.
3. Não baixar XMLTV completo imediatamente.

Ao selecionar um canal:

1. Se existir `streamId`, chamar `/api/xtream/epg/short`.
2. Se retornar programas, usar esse resultado.
3. Se falhar ou vier vazio, chamar `/api/xtream/epg/programs`.
4. Se ainda falhar, mostrar `EPG unavailable`.

Fluxo:

```txt
Xtream channel selected
  -> has streamId?
      yes -> /api/xtream/epg/short
              -> has programs?
                    yes -> show EPG
                    no  -> /api/xtream/epg/programs
      no  -> /api/xtream/epg/programs using tvgId/channelName
```

---

# 7. Matching de canal

O frontend deve tentar casar o canal com EPG nesta ordem:

## 7.1 M3U

1. `channel.tvgId`
2. `channel.tvgName`
3. `channel.name`
4. nome normalizado

Exemplo de canal:

```ts
interface Channel {
  id: string;
  name: string;
  streamUrl: string;
  logo?: string;
  tvgId?: string;
  tvgName?: string;
  groupTitle?: string;
  type?: 'live' | 'vod' | 'series';
}
```

---

## 7.2 Xtream

1. `channel.streamId`
2. `channel.tvgId`
3. `channel.name`

Exemplo:

```ts
interface XtreamChannel {
  id: string;
  name: string;
  streamUrl: string;
  streamId?: number;
  tvgId?: string;
  tvgName?: string;
  logo?: string;
  categoryId?: string;
  type?: 'live' | 'vod' | 'series';
}
```

---

# 8. Interfaces TypeScript sugeridas

## 8.1 Programa EPG

```ts
export interface EpgProgram {
  id?: string;
  channelId?: string;
  channelName?: string;
  title: string;
  description?: string | null;
  start: string;
  end: string;
  category?: string | null;
  icon?: string | null;
  raw?: any;
}
```

---

## 8.2 Resposta paginada de EPG

```ts
export interface EpgProgramsResponse {
  success: boolean;
  provider: 'm3u' | 'xtream';
  channelId?: string;
  streamId?: number;
  page?: number;
  pageSize?: number;
  total?: number;
  programs: EpgProgram[];
  error?: string;
}
```

---

## 8.3 Fonte de EPG

```ts
export interface EpgSourceResponse {
  success: boolean;
  provider: 'm3u' | 'xtream';
  available: boolean;
  epgUrl?: string | null;
  format?: 'xmltv';
  compressed?: boolean;
  error?: string;
}
```

---

## 8.4 Configuração de playlist no frontend

```ts
export interface PlaylistConfig {
  id: string;
  name: string;
  type: 'm3u' | 'xtream';
  playlistUrl?: string;
  epgUrl?: string;

  // Xtream
  baseUrl?: string;
  username?: string;
  password?: string;
  playerApiUrl?: string;

  createdAt?: string;
  updatedAt?: string;
}
```

---

# 9. Serviço TypeScript sugerido

```ts
export class EpgService {
  constructor(private readonly baseUrl: string) {}

  async discoverM3uEpg(playlistUrl: string): Promise<EpgSourceResponse> {
    return this.post('/api/epg/source', { url: playlistUrl });
  }

  async discoverXtreamEpg(input: {
    url?: string;
    baseUrl?: string;
    username?: string;
    password?: string;
  }): Promise<EpgSourceResponse> {
    return this.post('/api/xtream/epg/source', input);
  }

  async getM3uPrograms(input: {
    playlistUrl?: string;
    epgUrl?: string;
    tvgId?: string;
    channelId?: string;
    channelName?: string;
    from?: string;
    to?: string;
    page?: number;
    pageSize?: number;
  }): Promise<EpgProgramsResponse> {
    return this.post('/api/epg/programs', {
      provider: 'm3u',
      url: input.playlistUrl,
      epgUrl: input.epgUrl,
      tvgId: input.tvgId,
      channelId: input.channelId,
      channelName: input.channelName,
      from: input.from,
      to: input.to,
      page: input.page ?? 1,
      pageSize: input.pageSize ?? 50
    });
  }

  async getXtreamShortEpg(input: {
    url?: string;
    baseUrl?: string;
    username?: string;
    password?: string;
    streamId: number;
    limit?: number;
  }): Promise<EpgProgramsResponse> {
    return this.post('/api/xtream/epg/short', {
      ...input,
      limit: input.limit ?? 10
    });
  }

  async getXtreamPrograms(input: {
    url?: string;
    baseUrl?: string;
    username?: string;
    password?: string;
    streamId?: number;
    tvgId?: string;
    channelName?: string;
    from?: string;
    to?: string;
    page?: number;
    pageSize?: number;
  }): Promise<EpgProgramsResponse> {
    return this.post('/api/xtream/epg/programs', {
      ...input,
      page: input.page ?? 1,
      pageSize: input.pageSize ?? 50
    });
  }

  private async post<T>(path: string, body: any): Promise<T> {
    const response = await fetch(`${this.baseUrl}${path}`, {
      method: 'POST',
      headers: {
        'Content-Type': 'application/json'
      },
      body: JSON.stringify(body)
    });

    if (!response.ok) {
      throw new Error(`EPG request failed: ${response.status} ${response.statusText}`);
    }

    return response.json() as Promise<T>;
  }
}
```

---

# 10. Função para obter EPG do canal selecionado

```ts
async function loadEpgForChannel(params: {
  playlist: PlaylistConfig;
  channel: Channel & { streamId?: number };
  epgService: EpgService;
}): Promise<EpgProgram[]> {
  const { playlist, channel, epgService } = params;

  const now = new Date();
  const from = new Date(now.getTime() - 60 * 60 * 1000).toISOString();
  const to = new Date(now.getTime() + 6 * 60 * 60 * 1000).toISOString();

  if (playlist.type === 'xtream') {
    if (channel.streamId) {
      try {
        const shortResponse = await epgService.getXtreamShortEpg({
          url: playlist.playerApiUrl,
          baseUrl: playlist.baseUrl,
          username: playlist.username,
          password: playlist.password,
          streamId: channel.streamId,
          limit: 10
        });

        if (shortResponse.programs?.length) {
          return shortResponse.programs;
        }
      } catch (error) {
        console.warn('Short EPG failed, trying XMLTV fallback', error);
      }
    }

    const fallbackResponse = await epgService.getXtreamPrograms({
      url: playlist.playerApiUrl,
      baseUrl: playlist.baseUrl,
      username: playlist.username,
      password: playlist.password,
      streamId: channel.streamId,
      tvgId: channel.tvgId,
      channelName: channel.tvgName || channel.name,
      from,
      to,
      page: 1,
      pageSize: 50
    });

    return fallbackResponse.programs || [];
  }

  const response = await epgService.getM3uPrograms({
    playlistUrl: playlist.playlistUrl,
    epgUrl: playlist.epgUrl,
    tvgId: channel.tvgId,
    channelId: channel.tvgId,
    channelName: channel.tvgName || channel.name,
    from,
    to,
    page: 1,
    pageSize: 50
  });

  return response.programs || [];
}
```

---

# 11. Cache recomendado no frontend

O frontend deve fazer cache por playlist e canal.

Chave sugerida:

```ts
function getEpgCacheKey(playlistId: string, channel: Channel & { streamId?: number }): string {
  return [
    'epg',
    playlistId,
    channel.streamId || channel.tvgId || channel.name
  ].join(':');
}
```

Estrutura:

```ts
interface EpgCacheEntry {
  cachedAt: number;
  expiresAt: number;
  programs: EpgProgram[];
}
```

TTL recomendado:

```ts
const EPG_CACHE_TTL_MS = 30 * 60 * 1000;
```

Ou seja: 30 minutos.

---

# 12. Identificar programa atual e próximo

```ts
export function getNowAndNext(programs: EpgProgram[], now = new Date()) {
  const time = now.getTime();

  const sorted = [...programs].sort((a, b) => {
    return new Date(a.start).getTime() - new Date(b.start).getTime();
  });

  const current = sorted.find(program => {
    const start = new Date(program.start).getTime();
    const end = new Date(program.end).getTime();
    return start <= time && time < end;
  });

  const next = sorted.find(program => {
    return new Date(program.start).getTime() > time;
  });

  return { current, next };
}
```

---

# 13. Renderização sugerida

## 13.1 Na lista de canais

Mostrar algo leve:

```txt
Canal Exemplo
Agora: Jornal Hoje
A seguir: Esporte Espetacular
```

Evite carregar EPG de todos os canais ao mesmo tempo.

Use carregamento preguiçoso:

- canais visíveis na tela;
- canal selecionado;
- favoritos;
- últimos assistidos.

---

## 13.2 No player

Mostrar overlay:

```txt
NOW  12:00 - 13:00  Jornal Hoje
NEXT 13:00 - 14:30  Esporte Espetacular
```

---

## 13.3 Tela de programação

Mostrar lista cronológica:

```txt
12:00 Jornal Hoje
13:00 Esporte Espetacular
14:30 Sessão da Tarde
16:00 Vale a Pena Ver de Novo
```

---

# 14. Tratamento de erro

O frontend nunca deve impedir a reprodução por falha de EPG.

Erros comuns:

- EPG ausente;
- XMLTV inválido;
- servidor bloqueando acesso;
- canal sem `tvgId`;
- Xtream sem `streamId`;
- timeout;
- EPG vazio;
- diferença entre nome do canal e ID do XMLTV.

Comportamento recomendado:

```txt
Se EPG falhar:
  - logar warning;
  - manter canal funcionando;
  - mostrar "EPG unavailable";
  - não repetir request em loop infinito;
  - tentar novamente após TTL curto, exemplo 5 minutos.
```

---

# 15. Timezone

O frontend deve tratar `start` e `end` como datas ISO.

Exemplo:

```ts
const start = new Date(program.start);
const end = new Date(program.end);
```

Para exibir horário local:

```ts
function formatProgramTime(value: string): string {
  return new Intl.DateTimeFormat('pt-BR', {
    hour: '2-digit',
    minute: '2-digit'
  }).format(new Date(value));
}
```

---

# 16. Paginação

Para programação curta de canal, use:

```json
{
  "page": 1,
  "pageSize": 50
}
```

Para grade maior, use paginação incremental:

```json
{
  "page": 2,
  "pageSize": 100
}
```

Evite `pageSize` muito alto no frontend.

Recomendação:

- `10` para overlay `NOW/NEXT`;
- `50` para canal selecionado;
- `100` para tela de programação;
- evitar acima de `500` no frontend.

---

# 17. Estratégia anti-travamento

O EPG deve ser carregado de forma assíncrona e cancelável.

Recomendações:

- usar `AbortController` no browser;
- cancelar request se o usuário trocar de canal rapidamente;
- não atualizar UI com resposta antiga;
- usar cache;
- usar debounce em navegação rápida.

Exemplo:

```ts
let currentEpgAbortController: AbortController | null = null;

async function fetchWithCancel(url: string, body: any) {
  if (currentEpgAbortController) {
    currentEpgAbortController.abort();
  }

  currentEpgAbortController = new AbortController();

  const response = await fetch(url, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(body),
    signal: currentEpgAbortController.signal
  });

  return response.json();
}
```

---

# 18. Checklist de implementação no frontend

## Playlist

- [ ] Salvar `type`: `m3u` ou `xtream`.
- [ ] Salvar `playlistUrl` para M3U.
- [ ] Salvar `epgUrl` opcional para M3U.
- [ ] Salvar `baseUrl`, `username`, `password` ou `playerApiUrl` para Xtream.
- [ ] Descobrir EPG ao adicionar playlist.

## Canal

- [ ] Manter `tvgId`.
- [ ] Manter `tvgName`.
- [ ] Manter `streamId` para Xtream.
- [ ] Não depender apenas do nome do canal.

## Consumo

- [ ] M3U usa `/api/epg/programs`.
- [ ] Xtream tenta `/api/xtream/epg/short` primeiro.
- [ ] Xtream usa `/api/xtream/epg/programs` como fallback.
- [ ] Usar cache por playlist/canal.
- [ ] Usar janela de tempo limitada.
- [ ] Evitar carregar EPG de todos os canais de uma vez.

## UI

- [ ] Mostrar `NOW`.
- [ ] Mostrar `NEXT`.
- [ ] Mostrar grade do canal selecionado.
- [ ] Mostrar `EPG unavailable` quando não houver dados.
- [ ] Não bloquear player se EPG falhar.

---

# 19. Resumo rápido dos endpoints

| Método | Endpoint | Uso principal |
|---|---|---|
| POST | `/api/epg/source` | Descobrir EPG em M3U |
| POST | `/api/epg/raw` | Baixar XMLTV bruto M3U/genérico |
| POST | `/api/epg/programs` | Obter programas M3U/genérico em JSON |
| POST | `/api/xtream/epg/source` | Inferir XMLTV do Xtream |
| POST | `/api/xtream/epg/raw` | Baixar XMLTV bruto Xtream |
| POST | `/api/xtream/epg/programs` | Obter programas Xtream via XMLTV completo |
| POST | `/api/xtream/epg/short` | Obter EPG curto por `streamId` |

---

# 20. Regra principal

O EPG nunca deve ser requisito obrigatório para a playlist funcionar.

A regra do frontend deve ser:

```txt
Canais primeiro.
EPG depois.
Se EPG falhar, o player continua funcionando.
```
