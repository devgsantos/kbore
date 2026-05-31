# Instruções para o Codex — Implementar renderer deko3d para player NVTEGRA no kboré

Estamos trabalhando em um homebrew Nintendo Switch chamado **kboré**, anteriormente `nstv-native`. O app já possui:

```text
1. UI SDL2 funcionando
2. RomFS funcionando
3. Fontes e logos carregados por romfs
4. Splash screen
5. Múltiplas playlists
6. Player nativo com FFmpeg custom
7. FFmpeg com patches nvtegra aplicados
8. Decode de vídeo via AV_PIX_FMT_NVTEGRA funcionando
9. Áudio funcionando via SDL Audio
10. SD e HD funcionando bem no pipeline atual
11. FHD ainda lento porque o frame hardware é transferido para CPU
```

O objetivo agora é implementar o caminho de renderização de vídeo com **deko3d**, evitando o pipeline atual:

```text
NVTEGRA decode
↓
av_hwframe_transfer_data()
↓
YUVFrame em RAM/CPU
↓
SDL_UpdateYUVTexture()
↓
SDL_RenderCopy()
```

O novo objetivo é:

```text
NVTEGRA decode
↓
AVFrame hardware / surface nvtegra
↓
renderer nativo/deko3d
↓
render sem transferir frame completo para CPU
```

Não remover o caminho atual. O pipeline atual SDL/YUV deve continuar existindo como **fallback**.

---

## 1. Antes de alterar, leia estes arquivos

Comece lendo os arquivos abaixo para entender o fluxo atual:

```text
include/nstv/player_backend.hpp
include/nstv/native_hw_player.hpp
source/native_hw_player.cpp

include/nstv/native_decoder.hpp
source/native_decoder.cpp

include/nstv/native_hw_device.hpp
source/native_hw_device.cpp

include/nstv/native_demuxer.hpp
source/native_demuxer.cpp

include/nstv/graphics.hpp
source/graphics_sdl.cpp

include/nstv/app.hpp
source/app.cpp

Makefile.switch
```

Prioridade de leitura:

```text
1. native_hw_device.*
   Entender como o AVHWDeviceContext nvtegra é criado.

2. native_decoder.*
   Entender como openVideoHardware() conecta:
     codecContext->hw_device_ctx
     get_format
     AV_PIX_FMT_NVTEGRA
     av_hwframe_transfer_data()

3. native_hw_player.*
   Entender como o player controla:
     open()
     update()
     pacing
     dropNextVideoFrame()
     áudio SDL
     yuvFrame atual

4. graphics_sdl.*
   Entender como drawYuvFrame() renderiza hoje via SDL.

5. app.cpp
   Entender como o player é chamado e como a UI mostra Loading video / Failed to load video.
```

---

## 2. Estado atual do decode NVTEGRA

O probe atual deve mostrar algo como:

```text
configs=1
pix_fmt=nvtegra
device=nvtegra
methods=none
usableDeviceConfig=yes
createdDevice=yes
selected=#0
```

Importante: o backend `nvtegra` aparece com `methods=none`, então o código atual já foi ajustado para aceitar `device=nvtegra` e `pix_fmt=nvtegra` mesmo sem `HW_DEVICE_CTX`.

O decode hardware já funciona. O gargalo não é mais o decode. O gargalo é:

```text
av_hwframe_transfer_data()
cópia/conversão para YuvFrame
SDL texture update
```

em FHD.

---

## 3. Não quebrar o player atual

O app já funciona com o player atual. Então a implementação deko3d deve seguir esta regra:

```text
Tentar deko3d/native surface primeiro.
Se falhar:
  usar caminho atual:
    av_hwframe_transfer_data()
    YuvFrame
    SDL render
```

Não remover:

```text
NativeHwPlayerBackend
NativeDecoder::decodeNextVideoFrame(..., outputFrame)
NativeDecoder::dropNextVideoFrame()
Graphics::drawYuvFrame()
SDL Audio
```

A etapa atual deve ser incremental.

---

## 4. Criar primeiro um probe de frame NVTEGRA

Antes de tentar renderizar direto, precisamos entender o conteúdo real do `AVFrame` retornado pelo decoder quando `frame->format == AV_PIX_FMT_NVTEGRA`.

Criar novos arquivos:

```text
include/nstv/native_video_surface.hpp
source/native_video_surface.cpp
```

ou, se preferir separar:

```text
include/nstv/nvtegra_frame_probe.hpp
source/nvtegra_frame_probe.cpp
```

Estrutura sugerida:

```cpp
#pragma once

#include <cstdint>
#include <string>

#ifdef NSTV_USE_FFMPEG
extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
}
#endif

namespace nstv {

struct NativeVideoSurfaceInfo {
  bool valid = false;

  int width = 0;
  int height = 0;

  int format = -1;
  std::string formatName;

  bool hasHwFramesCtx = false;
  bool hasBuf0 = false;
  bool hasBuf1 = false;
  bool hasBuf2 = false;

  uintptr_t data0 = 0;
  uintptr_t data1 = 0;
  uintptr_t data2 = 0;

  int linesize0 = 0;
  int linesize1 = 0;
  int linesize2 = 0;

  std::string summary;
};

#ifdef NSTV_USE_FFMPEG
NativeVideoSurfaceInfo inspectNativeVideoSurface(const AVFrame *frame);
#endif

} // namespace nstv
```

A função deve preencher:

```text
frame->width
frame->height
frame->format
av_get_pix_fmt_name()
frame->hw_frames_ctx != nullptr
frame->data[0..2]
frame->linesize[0..2]
frame->buf[0..2] != nullptr
```

E gerar um resumo tipo:

```text
format=nvtegra width=1920 height=1080 hw_frames_ctx=yes data0=0x... linesize0=...
```

---

## 5. Alterar NativeDecoder para expor frame hardware sem transferir

Hoje o `NativeDecoder::decodeNextVideoFrame()` provavelmente faz algo assim:

```cpp
avcodec_receive_frame(videoCodec, videoFrame);

if (hardwareFrame || videoFrame->hw_frames_ctx) {
  av_hwframe_transfer_data(transferredFrame, videoFrame, 0);
  frameForProcessing = transferredFrame;
}
```

Precisamos adicionar uma forma de:

```text
1. receber o frame hardware
2. inspecionar o frame
3. opcionalmente não transferir para CPU
4. guardar informações do frame hardware
```

Sugestão: adicionar em `NativeFrameInfo`:

```cpp
bool nativeSurfaceAvailable = false;
std::string nativeSurfaceSummary;
```

E no `NativeDecoder`:

```cpp
const NativeVideoSurfaceInfo &latestNativeSurfaceInfo() const;
```

No `Impl`:

```cpp
NativeVideoSurfaceInfo latestNativeSurfaceInfo;
```

Quando receber um frame hardware:

```cpp
if (hardwareFrame || impl_->videoFrame->hw_frames_ctx) {
  impl_->latestNativeSurfaceInfo = inspectNativeVideoSurface(impl_->videoFrame);
  latestFrameInfo_.nativeSurfaceAvailable = impl_->latestNativeSurfaceInfo.valid;
  latestFrameInfo_.nativeSurfaceSummary = impl_->latestNativeSurfaceInfo.summary;
}
```

Importante: não alterar ainda o comportamento do fallback. Para esta primeira fase, ainda pode transferir para CPU depois de inspecionar, para manter vídeo funcionando.

---

## 6. Criar abstração de renderer nativo

Criar novos arquivos:

```text
include/nstv/native_video_renderer.hpp
source/native_video_renderer.cpp
```

Interface sugerida:

```cpp
#pragma once

#include "nstv/native_video_surface.hpp"

namespace nstv {

class INativeVideoRenderer {
public:
  virtual ~INativeVideoRenderer() = default;

  virtual bool initialize() = 0;
  virtual void shutdown() = 0;

#ifdef NSTV_USE_FFMPEG
  virtual bool canRender(const AVFrame *frame) const = 0;
  virtual bool renderFrame(const AVFrame *frame) = 0;
#endif

  virtual const std::string &error() const = 0;
  virtual const char *name() const = 0;
};

} // namespace nstv
```

Depois criar implementação Switch/deko3d:

```text
include/nstv/deko3d_video_renderer.hpp
source/deko3d_video_renderer.cpp
```

Inicialmente, essa classe pode ser um stub compilável:

```cpp
class Deko3dVideoRenderer : public INativeVideoRenderer {
public:
  bool initialize() override;
  void shutdown() override;

#ifdef NSTV_USE_FFMPEG
  bool canRender(const AVFrame *frame) const override;
  bool renderFrame(const AVFrame *frame) override;
#endif

  const std::string &error() const override { return error_; }
  const char *name() const override { return "deko3d"; }

private:
  bool initialized_ = false;
  std::string error_;
};
```

No Switch:

```cpp
#ifdef __SWITCH__
#include <switch.h>
#include <deko3d.hpp>
#endif
```

Se `deko3d.hpp` não existir no ambiente, verificar os includes instalados no devkitPro. Não assumir API sem testar compilação.

---

## 7. Inicialização de deko3d

O `Deko3dVideoRenderer::initialize()` deve criar o contexto mínimo do deko3d.

O Codex deve consultar exemplos instalados localmente antes de escrever a implementação final. Procurar:

```bash
find $DEVKITPRO -iname "*deko*" | head -100
find $DEVKITPRO/examples -iname "*deko*" -o -iname "*dk*" | head -100
grep -R "dk::DeviceMaker\|DkDevice\|dk::QueueMaker\|DkSwapchain" $DEVKITPRO/examples $DEVKITPRO/portlibs $DEVKITPRO/libnx 2>/dev/null | head -100
```

Ler exemplos de inicialização de:

```text
dk::Device
dk::Queue
swapchain
command buffer
framebuffer
```

Não inventar chamadas se a API local for diferente.

A implementação inicial pode fazer apenas:

```text
initialize deko3d
criar device
criar queue
preparar command buffer básico
não renderizar ainda
return true
```

Se inicializar com sucesso, já é avanço.

---

## 8. Integração com NativeHwPlayerBackend

Adicionar em `NativeHwPlayerBackend`:

```cpp
std::unique_ptr<INativeVideoRenderer> nativeRenderer_;
bool nativeRendererReady_ = false;
bool preferNativeRenderer_ = true;
bool nativeRendererFailed_ = false;
std::string nativeRendererStatus_;
```

No `open()`:

```cpp
#ifdef __SWITCH__
nativeRenderer_ = createDeko3dVideoRenderer();
if (nativeRenderer_ && nativeRenderer_->initialize()) {
  nativeRendererReady_ = true;
} else {
  nativeRendererReady_ = false;
  nativeRendererFailed_ = true;
}
#endif
```

Mas atenção: se SDL também está usando renderer acelerado, pode haver conflito com deko3d no mesmo framebuffer/contexto. Então a primeira implementação de `deko3d` pode ser apenas probe/init, sem desenhar no framebuffer, para validar integração.

---

## 9. Como fazer o decoder devolver frame hardware para o renderer

O problema atual: `decodeNextVideoFrame()` retorna um `YuvFrame`, não o `AVFrame` original. Para deko3d precisamos acessar o frame hardware antes de `av_hwframe_transfer_data()`.

Adicionar método no `NativeDecoder`:

```cpp
#ifdef NSTV_USE_FFMPEG
const AVFrame *latestHardwareFrame() const;
#endif
```

Mas cuidado com lifetime:

```text
AVFrame interno é reutilizado no próximo decode.
O renderer precisa consumir o frame antes de av_frame_unref().
```

Melhor opção inicial:

Criar novo método:

```cpp
bool decodeNextHardwareFrame(NativeDemuxer &demuxer);
```

que:

```text
1. lê pacotes
2. decodifica até receber frame de vídeo
3. se frame é hardware:
   - guarda metadata
   - NÃO chama av_hwframe_transfer_data
   - NÃO faz av_frame_unref imediatamente
   - deixa frame disponível até o renderer consumir
4. se não for hardware:
   - fallback para decodeNextVideoFrame normal
```

Adicionar também:

```cpp
void releaseLatestHardwareFrame();
```

Fluxo futuro:

```cpp
if (decoder_.decodeNextHardwareFrame(demuxer_)) {
  const AVFrame *frame = decoder_.latestHardwareFrame();

  if (nativeRendererReady_ && nativeRenderer_->canRender(frame)) {
    if (nativeRenderer_->renderFrame(frame)) {
      decoder_.releaseLatestHardwareFrame();
      return true;
    }
  }

  decoder_.releaseLatestHardwareFrame();

  // fallback: decodeNextVideoFrame(... true)
}
```

Mas para evitar grande refactor agora, a primeira fase pode apenas inspecionar e logar o frame hardware antes de transferir.

---

## 10. Etapas obrigatórias de implementação

Implementar em fases, não tentar fazer tudo de uma vez.

### Fase 1 — Probe do frame NVTEGRA

Objetivo:

```text
Compilar
Abrir vídeo
Continuar tocando pelo pipeline atual
Logar/mostrar resumo do frame nvtegra
```

Alterações:

```text
native_video_surface.hpp/cpp
native_decoder.hpp/cpp
native_hw_player.cpp, se necessário para exibir summary
```

Resultado esperado no log:

```text
NVTEGRA frame: format=nvtegra width=1920 height=1080 hw_frames_ctx=yes data0=...
```

Não alterar render ainda.

### Fase 2 — Stub de Deko3D renderer

Objetivo:

```text
Compilar com deko3d
Inicializar renderer nativo
Não desenhar ainda
Não quebrar SDL
```

Arquivos:

```text
native_video_renderer.hpp/cpp
deko3d_video_renderer.hpp/cpp
Makefile.switch
```

Makefile:

Adicionar source automaticamente se o Makefile já pega `source/*.cpp`.

Adicionar libs se necessário:

```makefile
-ldeko3d
```

ou o nome correto conforme devkitPro. Verificar com:

```bash
ls $DEVKITPRO/portlibs/switch/lib | grep -i deko
ls $DEVKITPRO/libnx/lib | grep -i deko
```

Se deko3d já vier via libnx ou header-only, não adicionar lib inexistente.

### Fase 3 — Decodificar frame hardware sem transfer

Objetivo:

```text
Criar caminho no NativeDecoder para receber AVFrame nvtegra sem av_hwframe_transfer_data
Gerenciar lifetime corretamente
Fallback intacto
```

Não tentar renderizar ainda.

### Fase 4 — Render nativo experimental

Objetivo:

```text
Tentar importar/usar surface nvtegra no deko3d
Se falhar, fallback para SDL/YUV
```

Antes de implementar, ler os arquivos dos patches nvtegra dentro de:

```text
external/ffmpeg-nx/libavutil/hwcontext_nvtegra.c
external/ffmpeg-nx/libavcodec/nvtegra*.c
```

Procurar por:

```bash
grep -R "typedef.*NvTegra\|struct.*nvtegra\|AV_PIX_FMT_NVTEGRA\|data\[\|hwctx\|surface\|frame" external/ffmpeg-nx/libavutil external/ffmpeg-nx/libavcodec -n | head -200
```

O objetivo é descobrir:

```text
- que tipo interno está em frame->data[0]
- se é handle para surface
- se existe objeto com fd/handle/mem
- se precisa mapear via nvmap
- se a surface está em block-linear/tiled
- se existe suporte VIC para conversão
```

Não assumir que `frame->data[0]` é ponteiro para pixels.

---

## 11. Atenção importante: AV_PIX_FMT_NVTEGRA não é YUV comum

Não tratar `AV_PIX_FMT_NVTEGRA` como:

```text
NV12
YUV420P
RGBA
```

Ele provavelmente representa uma surface hardware ou estrutura interna. Portanto:

```text
Não usar SDL_UpdateYUVTexture com data[] de frame nvtegra.
Não fazer memcpy de frame->data[0].
Não assumir linesize como pitch linear.
```

Para render deko3d real, será necessário entender o layout do patch `hwcontext_nvtegra`.

---

## 12. Fallback obrigatório

Em qualquer erro do novo renderer:

```text
nativeRendererFailed_ = true
usar decodeNextVideoFrame(demuxer_, true)
usar YuvFrame SDL atual
não impedir o vídeo de tocar
```

Nunca deixar o usuário sem vídeo se o fallback já funcionava.

---

## 13. Logs recomendados

Adicionar logs com prefixo:

```text
[KBORE][NVTEGRA]
[KBORE][DEKO3D]
```

Exemplos:

```cpp
std::printf("[KBORE][NVTEGRA] %s\n", info.summary.c_str());
std::printf("[KBORE][DEKO3D] initialized\n");
std::printf("[KBORE][DEKO3D] render failed: %s\n", error_.c_str());
std::printf("[KBORE][DEKO3D] fallback to SDL/YUV\n");
```

Evitar mostrar logs técnicos na UI final. A UI deve continuar mostrando:

```text
Loading video
Failed to load video
```

---

## 14. Makefile.switch

Verificar se o Makefile atual usa:

```makefile
SOURCES := source
CPPFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
```

Se sim, novos `.cpp` em `source/` serão compilados automaticamente.

Se adicionar `deko3d`, verificar se precisa de lib. Não adicionar `-ldeko3d` sem confirmar que existe.

Comandos úteis:

```bash
find /opt/devkitpro -iname "*deko*"
find /opt/devkitpro -iname "*.a" | grep -i deko
grep -R "namespace dk\|dk::" /opt/devkitpro -n 2>/dev/null | head -80
```

---

## 15. Critério de sucesso por fase

### Fase 1 sucesso

```text
make clean
make switch
app abre
player abre vídeo
SD/HD continuam tocando
log mostra informações do frame nvtegra
```

### Fase 2 sucesso

```text
app compila com novos arquivos
Deko3D renderer inicializa ou falha graciosamente
fallback continua funcionando
```

### Fase 3 sucesso

```text
decoder consegue receber frame nvtegra sem transfer
lifetime controlado
sem crash
fallback ainda funciona
```

### Fase 4 sucesso

```text
algum frame renderiza via deko3d
ou falha sem quebrar o player
```

---

## 16. Não alterar nesta tarefa

Não mexer agora em:

```text
playlist management
config.json
parser API
RomFS
splash
fontes
input de texto
UI dashboard
áudio SDL
```

Exceto se for necessário adicionar uma flag/debug pequena para o renderer.

---

## 17. Observação sobre SDL + deko3d

O app hoje usa SDL renderer acelerado para a UI e para o YUV fallback.

Pode haver conflito entre:

```text
SDL_Renderer acelerado
deko3d direto
```

Então não tentar misturar desenho de UI SDL e frame deko3d no mesmo framebuffer de forma definitiva na primeira fase.

Estratégia segura:

```text
Fase 1: probe sem render
Fase 2: init deko3d sem render
Fase 3: render experimental em tela cheia apenas durante player
Fase 4: se necessário, UI overlay do player continua SDL apenas no fallback
```

Para o futuro, se deko3d assumir o framebuffer durante o player, o overlay do player talvez precise ser redesenhado em deko3d ou desativado/minimalista.

---

## 18. Resultado final esperado

No fim desta etapa, queremos estar mais próximos deste fluxo:

```text
Dashboard SDL
↓
Usuário abre canal/VOD
↓
Player screen
↓
NVTEGRA decode
↓
Deko3D render direto em fullscreen
↓
Áudio SDL continua
↓
B volta para dashboard SDL
```

Se o render direto ainda não for possível, pelo menos queremos ter:

```text
- frame nvtegra inspecionado corretamente
- deko3d inicializado
- caminho preparado
- fallback SDL/YUV intacto
```

---

## 19. Resumo executivo para o Codex

Implementar incrementalmente:

```text
1. Criar NativeVideoSurfaceInfo e probe de AVFrame nvtegra.
2. Integrar probe no NativeDecoder sem mudar o fallback.
3. Criar interface INativeVideoRenderer.
4. Criar Deko3dVideoRenderer inicializável no Switch.
5. Integrar Deko3dVideoRenderer no NativeHwPlayerBackend como tentativa opcional.
6. Não renderizar direto até entender frame->data/hw_frames_ctx do AV_PIX_FMT_NVTEGRA.
7. Ler hwcontext_nvtegra.c e nvtegra decoder patches antes de tentar importar surface.
8. Preservar fallback atual SDL/YUV.
```

Essa é a base segura para evoluir para FHD real sem voltar o frame inteiro para CPU.
