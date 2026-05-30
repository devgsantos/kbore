# NSTV Native — Ambiente de desenvolvimento, build Switch e player NVTEGRA

Este README documenta o estado atual do projeto **NSTV Native** para Nintendo Switch homebrew, incluindo preparação do ambiente, build do app, build do FFmpeg custom com suporte `nvtegra`, aplicação dos patches, player nativo com vídeo via NVTEGRA e áudio via SDL.

> Estado atual do player:
>
> - UI nativa funcionando.
> - Controle nativo do Switch funcionando.
> - Cache/listagem de streams funcionando.
> - FFmpeg custom compilado localmente.
> - Backend `nvtegra` aplicado ao FFmpeg.
> - `Native HW Probe` detecta `pix_fmt=nvtegra` e `device=nvtegra`.
> - `AVHWDeviceContext` cria com sucesso.
> - Vídeo abre via hardware decode NVTEGRA.
> - Áudio funciona via SDL Audio.
> - SD está estável.
> - HD está funcional.
> - FHD ainda depende de otimização futura via renderer nativo/deko3d para evitar transferências para CPU.

---

## 1. Estrutura esperada do projeto

Este README assume que o projeto está nesta pasta:

```bash
~/nstv-native
```

Estrutura relevante:

```text
nstv-native/
├── Makefile
├── Makefile.switch
├── include/
│   └── nstv/
├── source/
├── scripts/
│   ├── build-ffmpeg-nx.sh
│   ├── check-ffmpeg-nx.sh
│   ├── patch-ffmpeg-nx-switch.sh
│   ├── apply-ffmpeg-nvtegra-0001-manual.sh
│   ├── apply-ffmpeg-nvtegra-0002-manual.sh
│   └── apply-ffmpeg-nvtegra-from-0003-safe.sh
├── patches/
│   └── ffmpeg-nvtegra/
├── external/
│   ├── ffmpeg-nx/
│   └── player-sdk/
└── data/
```

---

## 2. Dependências do ambiente

### 2.1. devkitPro / devkitA64 / libnx

O projeto precisa do ambiente padrão de homebrew do Nintendo Switch:

```bash
/opt/devkitpro
/opt/devkitpro/devkitA64
/opt/devkitpro/libnx
/opt/devkitpro/portlibs/switch
```

As variáveis esperadas são:

```bash
export DEVKITPRO=/opt/devkitpro
export DEVKITA64=$DEVKITPRO/devkitA64
export PATH=$DEVKITA64/bin:$PATH
```

Para tornar permanente, adicione ao `~/.bashrc` ou `~/.zshrc`:

```bash
export DEVKITPRO=/opt/devkitpro
export DEVKITA64=$DEVKITPRO/devkitA64
export PATH=$DEVKITA64/bin:$PATH
```

Depois:

```bash
source ~/.bashrc
```

Confirme:

```bash
echo $DEVKITPRO
echo $DEVKITA64
which aarch64-none-elf-gcc
```

---

## 3. Dependências via devkitPro

Instale os pacotes usados pelo projeto. Os nomes podem variar conforme o estado do repositório do devkitPro, mas a base é:

```bash
sudo dkp-pacman -Syu
sudo dkp-pacman -S switch-dev switch-sdl2 switch-sdl2_ttf switch-sdl2_image switch-curl switch-mbedtls switch-zlib switch-bzip2 switch-xz
```

Se algum pacote como `switch-xz` não existir na sua instalação, verifique os nomes disponíveis:

```bash
dkp-pacman -Ss switch | grep -i xz
dkp-pacman -Ss switch | grep -i lzma
```

O app pode exigir bibliotecas como:

```text
SDL2
SDL2_ttf
SDL2_image
curl
mbedtls
zlib
bzip2
lzma/xz
libnx
```

---

## 4. Configuração do Git e subpastas externas

### 4.1. Evitar versionar FFmpeg e SDK gerado

Adicione ao `.gitignore`:

```gitignore
# Build artifacts
build/
build-switch/
*.elf
*.nro
*.nacp
*.map
*.o
*.d

# Local FFmpeg source and generated SDK
external/ffmpeg-nx/
external/player-sdk/

# Local generated/debug assets
nstv-frame.bmp
*.log

# IDE
.vscode/
.idea/

# OS
.DS_Store
Thumbs.db
```

### 4.2. Atenção a repositórios Git dentro de `external/`

Se fizer:

```bash
git add .
```

e aparecer:

```text
warning: adding embedded git repository: external/ffmpeg-nx
warning: adding embedded git repository: external/mpv-nx
```

não adicione essas pastas como arquivos normais do repo principal.

Remova do index se necessário:

```bash
git rm --cached external/ffmpeg-nx
git rm --cached external/mpv-nx
```

Para este projeto, por enquanto, `external/ffmpeg-nx/` e `external/player-sdk/` devem ficar ignorados.

---

## 5. Baixar FFmpeg custom

A partir da raiz do app:

```bash
cd ~/nstv-native

mkdir -p external
cd external

git clone https://github.com/FFmpeg/FFmpeg.git ffmpeg-nx
cd ffmpeg-nx
git checkout n7.0
git checkout -b nstv-nvtegra
```

Volte:

```bash
cd ~/nstv-native
```

---

## 6. Patches NVTEGRA

Os patches ficam em:

```bash
patches/ffmpeg-nvtegra/
```

Arquivos esperados:

```text
0001-avutil-buffer-add-helper-to-allocate-aligned-memory.patch
0002-configure-avutil-add-support-for-HorizonOS.patch
0003-avutil-add-ioctl-definitions-for-tegra-devices.patch
0004-avutil-add-hardware-definitions-for-NVDEC-NVJPG-and-VIC.patch
0005-avutil-add-common-code-for-nvtegra.patch
0006-avutil-add-nvtegra-hwcontext.patch
0007-hwcontext_nvtegra-add-dynamic-frequency-scaling-routines.patch
0008-nvtegra-add-common-hardware-decoding-code.patch
0009-nvtegra-add-mpeg1-2-hardware-decoding.patch
0010-nvtegra-add-mpeg4-hardware-decoding.patch
0011-nvtegra-add-vc1-hardware-decoding.patch
0012-nvtegra-add-h264-hardware-decoding.patch
0013-nvtegra-add-hevc-hardware-decoding.patch
0014-nvtegra-add-vp8-hardware-decoding.patch
0015-nvtegra-add-vp9-hardware-decoding.patch
0016-nvtegra-add-mjpeg-hardware-decoding.patch
```

> Importante: não use HTML renomeado para `.patch`. Os arquivos precisam conter patch puro, com linhas `diff --git`.

Teste:

```bash
grep -n "diff --git" patches/ffmpeg-nvtegra/*.patch
```

---

## 7. Scripts de patch do FFmpeg

### 7.1. `scripts/patch-ffmpeg-nx-switch.sh`

Este script corrige `TCP_MAXSEG` no FFmpeg, necessário para compilar `libavformat/tcp.c` no ambiente Switch/libnx.

```bash
#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
FFMPEG="$ROOT/external/ffmpeg-nx"
TCP_FILE="$FFMPEG/libavformat/tcp.c"

if [ ! -f "$TCP_FILE" ]; then
  echo "ERROR: tcp.c not found at:"
  echo "  $TCP_FILE"
  exit 1
fi

if grep -q "NSTV_SWITCH_TCP_MAXSEG_PATCH" "$TCP_FILE"; then
  echo "FFmpeg tcp.c already patched for Switch TCP_MAXSEG."
  exit 0
fi

python3 - <<PY
from pathlib import Path

path = Path("$TCP_FILE")
text = path.read_text()

marker = '#include "network.h"'

patch = '''
/*
 * NSTV_SWITCH_TCP_MAXSEG_PATCH
 *
 * devkitPro/libnx headers may not expose TCP_MAXSEG, but FFmpeg tcp.c
 * references it when compiling TCP support. Define the common TCP_MAXSEG
 * socket option value so the cross-build can compile.
 *
 * The option is only used when tcp_mss is explicitly set.
 */
#ifndef TCP_MAXSEG
#define TCP_MAXSEG 2
#endif
'''

if "NSTV_SWITCH_TCP_MAXSEG_PATCH" in text:
    print("Already patched.")
else:
    if marker not in text:
        raise SystemExit(f"Marker not found in {path}: {marker}")

    text = text.replace(marker, marker + patch, 1)
    path.write_text(text)
    print(f"Patched {path}")
PY
```

Permissão:

```bash
chmod +x scripts/patch-ffmpeg-nx-switch.sh
```

---

### 7.2. Patches manuais 0001 e 0002

Os patches `0001` e `0002` podem falhar por diferença de contexto no FFmpeg `n7.0`. Use os scripts manuais já criados:

```bash
chmod +x scripts/apply-ffmpeg-nvtegra-0001-manual.sh
chmod +x scripts/apply-ffmpeg-nvtegra-0002-manual.sh
```

### 7.3. Aplicar patches `0003` até `0016`

Use o script seguro:

```bash
chmod +x scripts/apply-ffmpeg-nvtegra-from-0003-safe.sh
```

Esse script usa `git apply --check` antes de aplicar e não fecha o terminal se algum patch falhar.

---

## 8. Aplicar patches NVTEGRA no FFmpeg

A partir da raiz do app:

```bash
cd ~/nstv-native
```

Limpe estado parcial:

```bash
cd external/ffmpeg-nx
git am --abort 2>/dev/null || true
git reset --hard
git clean -fd
cd ../..
```

Aplique:

```bash
./scripts/apply-ffmpeg-nvtegra-0001-manual.sh
./scripts/apply-ffmpeg-nvtegra-0002-manual.sh
./scripts/apply-ffmpeg-nvtegra-from-0003-safe.sh
```

Verifique:

```bash
cd external/ffmpeg-nx

grep -R "enable-nvtegra\|nvtegra\|AV_HWDEVICE_TYPE_NVTEGRA" configure libavcodec libavutil -n | head -80
```

Se aparecerem resultados com `nvtegra`, os patches entraram.

---

## 9. Build do FFmpeg custom com NVTEGRA

### 9.1. `scripts/build-ffmpeg-nx.sh`

O configure deve conter:

```bash
--target-os=horizon
--enable-gpl
--enable-nvtegra
--enable-pic
```

Também use flags de PIE/PIC:

```bash
COMMON_CFLAGS="-march=armv8-a -mtune=cortex-a57 -mtp=soft -O3 -fPIC -fPIE -ffunction-sections -fdata-sections -DNX -D__SWITCH__ -I$DEVKITPRO/libnx/include -I$DEVKITPRO/portlibs/switch/include"
COMMON_LDFLAGS="-fPIE -specs=$DEVKITPRO/libnx/switch.specs -L$DEVKITPRO/libnx/lib -L$DEVKITPRO/portlibs/switch/lib"
```

Decoders/parsers recomendados:

```bash
--enable-parser=h264,hevc,aac,ac3,mpeg4video,mpegaudio,vp8,vp9
--enable-decoder=h264,hevc,aac,mp3,ac3,eac3,mpeg2video,mpeg4,vp8,vp9
```

### 9.2. Rodar build

```bash
cd ~/nstv-native

rm -rf external/player-sdk
./scripts/build-ffmpeg-nx.sh
```

---

## 10. Verificar SDK gerado

Rode:

```bash
./scripts/check-ffmpeg-nx.sh
```

O ideal é aparecer algo contendo:

```text
h264_nvtegra
hevc_nvtegra
vp8_nvtegra
vp9_nvtegra
hwcontext_nvtegra
```

Também pode verificar manualmente:

```bash
aarch64-none-elf-nm external/player-sdk/lib/libavcodec.a | grep -Ei 'nvtegra|h264_nvtegra|hevc_nvtegra'
aarch64-none-elf-nm external/player-sdk/lib/libavutil.a | grep -Ei 'nvtegra|hwcontext_nvtegra'
```

---

## 11. Configurar `Makefile.switch` para usar `external/player-sdk`

No `Makefile.switch`, inclua o SDK local antes do portlibs:

```makefile
PLAYER_SDK := $(CURDIR)/external/player-sdk
PLAYER_SDK_INC := $(PLAYER_SDK)/include
PLAYER_SDK_LIB := $(PLAYER_SDK)/lib
```

Garanta que o include venha antes:

```makefile
export INCLUDE := -I$(PLAYER_SDK_INC) \
                  $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
                  $(foreach dir,$(LIBDIRS),-I$(dir)/include) \
                  -I$(CURDIR)/$(BUILD)
```

Garanta que o lib path venha antes:

```makefile
export LIBPATHS := -L$(PLAYER_SDK_LIB) \
                   $(foreach dir,$(LIBDIRS),-L$(dir)/lib)
```

Use libs em ordem parecida com:

```makefile
LIBS := -lavformat -lavcodec -lswscale -lswresample -lavutil \
        -lSDL2_image -lSDL2_ttf -lSDL2 \
        -lfreetype -lpng -ljpeg -lwebp \
        -lcurl -lmbedtls -lmbedx509 -lmbedcrypto \
        -lz -lbz2 -llzma -lnx
```

Ative temporariamente o backend nativo:

```makefile
-DNSTV_ENABLE_NATIVE_HW_PLAYER
```

A linha `CFLAGS` pode conter:

```makefile
CFLAGS := -g -Wall -O2 -ffunction-sections $(ARCH) $(DEFINES) -D__SWITCH__ -DNSTV_ENABLE_NATIVE_HW_PLAYER
```

---

## 12. Build do NSTV

```bash
cd ~/nstv-native

find . -exec touch {} \;
make clean
make switch
```

Saídas esperadas:

```text
nstv-native.elf
nstv-native.nro
nstv-native.nacp
```

---

## 13. Copiar para o Switch

Copie o `.nro` para o SD card:

```text
/switch/nstv-native/nstv-native.nro
```

Sugestão de estrutura no SD:

```text
sdmc:/switch/nstv-native/
├── nstv-native.nro
├── config.json
├── fonts/
│   └── OpenSans-Regular.ttf
└── cache/
```

---

## 14. Configuração `config.json`

Exemplo:

```json
{
  "defaultXtreamUrl": "http://servidor:porta",
  "username": "usuario",
  "password": "senha",
  "cacheEnabled": true
}
```

Se aparecer:

```text
defaultXtreamUrl is empty in config.json
```

confira:

```text
- nome exato da chave
- caminho do config.json
- se o JSON está válido
- se não há aspas erradas ou vírgulas sobrando
```

---

## 15. Fontes

Para a fonte Open Sans funcionar no Switch:

```text
sdmc:/switch/kbore/fonts/OpenSans-Regular.ttf
```

ou:

```text
sdmc:/switch/nstv-native/fonts/OpenSans-Regular.ttf
```

Se aparecerem quadrados no lugar das letras:

```text
- arquivo de fonte não encontrado
- TTF não inicializou
- string tem emoji/caracteres não suportados
```

Para nomes de canais/categorias com emojis, a estratégia atual recomendada é normalizar/remover ícones.

---

## 16. Fluxo atual do player

### Vídeo

```text
NativeDemuxer
↓
FFmpeg custom + nvtegra
↓
AVHWDeviceContext nvtegra
↓
NativeDecoder::openVideoHardware()
↓
AVFrame pix_fmt=nvtegra
↓
av_hwframe_transfer_data()
↓
YUVFrame
↓
SDL renderer
```

### Áudio

```text
NativeDemuxer
↓
audio packets
↓
FFmpeg audio decoder
↓
SwrContext
↓
S16 stereo 48kHz
↓
SDL_QueueAudio()
```

---

## 17. Estado de performance conhecido

### SD

```text
OK
vídeo estável
som funcionando
sem aceleração 2x após ajuste de pacing/drop
```

### HD

```text
OK/usável
som funcionando
framerate melhor após drop sem transferir frames descartados
```

### FHD

```text
vídeo abre via nvtegra
som funciona
framerate ainda baixo
```

Motivo:

```text
O decode é por hardware, mas ainda há transferência/cópia de 1920x1080 para CPU:
av_hwframe_transfer_data + YUVFrame + SDL texture
```

Solução futura:

```text
render direto via deko3d, sem av_hwframe_transfer_data()
```

---

## 18. Testes do Native HW Probe

Resultados esperados após FFmpeg nvtegra:

```text
configs=1
pix_fmt=nvtegra
device=nvtegra
methods=none
usableDeviceConfig=yes
createdDevice=yes
selected=#0
```

O `methods=none` apareceu no backend `nvtegra`, então o código do probe precisa aceitar `device=nvtegra` e `pix_fmt=nvtegra` mesmo sem `HW_DEVICE_CTX`.

---

## 19. Erros comuns e correções

### 19.1. `TCP_MAXSEG undeclared`

Erro:

```text
libavformat/tcp.c: error: 'TCP_MAXSEG' undeclared
```

Correção:

```bash
./scripts/patch-ffmpeg-nx-switch.sh
```

### 19.2. `read-only segment has dynamic relocations`

Erro ao linkar:

```text
read-only segment has dynamic relocations
```

Causa:

```text
libs estáticas FFmpeg sem -fPIC/-fPIE
```

Correção no FFmpeg:

```bash
--enable-pic
-fPIC
-fPIE
```

Se persistir, testar somente para validação:

```bash
--disable-asm
--disable-neon
```

### 19.3. `configs=0`

Significa que o app ainda está usando FFmpeg sem backend `nvtegra`, ou os patches não entraram.

Verifique:

```bash
./scripts/check-ffmpeg-nx.sh
```

E confira `Makefile.switch`:

```makefile
-I$(PLAYER_SDK_INC)
-L$(PLAYER_SDK_LIB)
```

devem vir antes do portlibs.

### 19.4. `usableDeviceConfig=no` com `pix_fmt=nvtegra`

Se aparecer:

```text
configs=1
pix_fmt=nvtegra
device=nvtegra
methods=none
usableDeviceConfig=no
```

Ajuste o `NativeHwDeviceProbe` para aceitar `nvtegra` mesmo com `methods=none`.

### 19.5. `could not initialize SwrContext: Invalid argument`

Causa provável:

```text
stream de áudio com sample rate/layout/sample format ausente ou diferente
```

Correção:

```text
safeAudioSampleRate()
safeAudioSampleFormat()
safeInputLayout()
makeDefaultLayout()
```

Também habilite codecs:

```bash
--enable-decoder=h264,hevc,aac,mp3,ac3,eac3,mpeg2video,mpeg4,vp8,vp9
--enable-parser=h264,hevc,aac,ac3,mpeg4video,mpegaudio,vp8,vp9
```

---

## 20. Próximas etapas técnicas

### 20.1. Caminho curto

Manter o pipeline atual e melhorar:

```text
- buffer de áudio
- sync por áudio
- controle de queue SDL
- ajuste fino de frame pacing
```

### 20.2. Caminho correto para FHD real

Criar renderer nativo:

```text
NativeNvtegraFrameProbe
↓
NativeVideoSurface
↓
Deko3D renderer
↓
render sem transferir frame para CPU
```

Objetivo:

```text
nvtegra frame
↓
surface/texture nativa
↓
deko3d
↓
FHD original
```

---

## 21. Comandos rápidos

### Rebuild FFmpeg

```bash
cd ~/nstv-native

rm -rf external/player-sdk
./scripts/build-ffmpeg-nx.sh
./scripts/check-ffmpeg-nx.sh
```

### Rebuild NSTV

```bash
make clean
make switch
```

### Verificar símbolos nvtegra

```bash
aarch64-none-elf-nm external/player-sdk/lib/libavcodec.a | grep -Ei 'nvtegra|h264_nvtegra|hevc_nvtegra'
aarch64-none-elf-nm external/player-sdk/lib/libavutil.a | grep -Ei 'nvtegra|hwcontext_nvtegra'
```

### Verificar se app usa SDK local

```bash
grep -n "PLAYER_SDK\|LIBPATHS\|INCLUDE\|LIBS" Makefile.switch
```

### Verificar patches aplicados

```bash
cd external/ffmpeg-nx

grep -R "enable-nvtegra\|nvtegra\|AV_HWDEVICE_TYPE_NVTEGRA" configure libavcodec libavutil -n | head -80
```

---

## 22. Licença

O backend `nvtegra` exige build com:

```bash
--enable-gpl
```

Portanto, se o app distribuir binário linkado com essa build GPL do FFmpeg, trate o projeto como compatível com GPL e disponibilize o código-fonte correspondente conforme a licença.
