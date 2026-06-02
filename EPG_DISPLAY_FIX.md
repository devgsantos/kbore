# Correção do EPG carregado mas não exibido

## Sintoma

A API/parser estava sendo consumida com sucesso, porém o app continuava mostrando:

- `EPG unavailable`
- `EPG not loaded`
- `EPG unavailable for this channel`

## Causa provável

O problema estava no trecho entre a resposta da API e a renderização da UI:

1. O parser nativo do app aceitava principalmente `programs`, `epg_listings` e `items`.
2. Alguns endpoints de EPG podem retornar `programmes`, `results`, `data[]` ou usar campos como `end_timestamp`, `rawStart` e `rawStop`.
3. Quando a API retornava programas, mas nenhum deles batia exatamente com o horário atual do console, `currentProgramIndex()` retornava `-1`.
4. Com `-1`, a UI descartava a programação recebida e mostrava `EPG unavailable`.
5. Em Xtream, quando `/api/xtream/epg/short` retornava vazio, o app não tentava automaticamente `/api/xtream/epg/programs`.

## Correções aplicadas

### 1. Parser de resposta EPG mais tolerante

Arquivo:

```txt
source/parser_api_client.cpp
```

Agora o app aceita listas de programas em:

```txt
programs
programmes
epg_listings
epgListings
items
results
data[]
```

Também aceita horários em:

```txt
start
start_timestamp
startTime
rawStart
stop
end
stop_timestamp
end_timestamp
endTime
rawStop
```

E aceita valores string ou numéricos.

### 2. Log da resposta parseada

Foi adicionado log para saber se o app realmente conseguiu transformar a resposta da API em programas internos:

```txt
[KBORE] parsed EPG response: programs=N total=N page=X/Y
[KBORE] first EPG program: channelId='...' title='...' start='...' stop='...'
```

Se a API responder mas `programs=0`, o problema ainda está no matching/filtro da API.

Se `programs>0`, o problema não é mais consumo da API.

### 3. Fallback Xtream automático

Quando o app chama:

```txt
/api/xtream/epg/short
```

mas recebe zero programas, agora ele tenta automaticamente:

```txt
/api/xtream/epg/programs
```

Log esperado:

```txt
[KBORE] Xtream short EPG returned no programs for streamId='123'; trying XMLTV fallback
```

### 4. UI não descarta mais EPG fora do horário exato

Arquivo:

```txt
source/app.cpp
```

Antes, se nenhum programa estivesse exatamente entre `start <= now < stop`, a UI mostrava `EPG unavailable`.

Agora, se a API retornou programas, o app mostra:

1. programa atual, se houver;
2. próximo programa futuro, se houver;
3. programa mais próximo do horário atual, se o relógio/timezone do console estiver deslocado;
4. primeiro programa, se não houver horários parseáveis.

Isso evita falso negativo quando o EPG existe, mas o horário do Switch ou do provedor está deslocado.

## Como validar

No log do app, procure por:

```txt
[KBORE] Parser API EPG POST ...
[KBORE] parsed EPG response: programs=N ...
[KBORE] first EPG program: ...
```

### Cenário esperado

Se aparecer:

```txt
programs=0
```

então a API foi chamada, mas não encontrou programação para aquele canal/filtro.

Se aparecer:

```txt
programs=3
```

ou qualquer valor maior que zero, a UI deve deixar de mostrar `EPG unavailable` e exibir ao menos o programa mais próximo.
