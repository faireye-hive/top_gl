# Tales of Pirates — Linux / OpenGL (Offline)

> **Projeto comunitário em desenvolvimento.** Porte do cliente clássico de **Tales of Pirates**, originalmente baseado em **DirectX 9**, para **Linux**, **C++17** e **OpenGL 3.3**, com foco em execução **offline** e preservação do comportamento histórico do jogo.
>
> Não é um cliente oficial. A migração ainda apresenta diferenças visuais e funcionais em relação ao jogo original.

**Tecnologias:** C++17 · OpenGL 3.3 · GLX/X11 · CMake · Linux  
**Estado de referência:** migrações até **SK2.17-A** (conforme a versão do código publicada no repositório).

## Sumário

1. [Visão geral](#visão-geral)
2. [Requisitos](#requisitos)
3. [Estrutura do projeto e arquivos do cliente](#estrutura-do-projeto-e-arquivos-do-cliente)
4. [Instalação e compilação](#instalação-e-compilação)
5. [Como abrir o jogo](#como-abrir-o-jogo)
6. [Controles](#controles)
7. [Parâmetros de execução](#parâmetros-de-execução)
8. [Modos gráficos e efeitos históricos](#modos-gráficos-e-efeitos-históricos)
9. [Testes automatizados e testes de GPU](#testes-automatizados-e-testes-de-gpu)
10. [Ferramentas de auditoria](#ferramentas-de-auditoria)
11. [Capturas e testes de habilidades](#capturas-e-testes-de-habilidades)
12. [Logs, desempenho e solução de problemas](#logs-desempenho-e-solução-de-problemas)
13. [Limitações e observações](#limitações-e-observações)

## Visão geral

O objetivo é recuperar gradualmente o cliente original de Tales of Pirates sem depender do DirectX 9 ou de um servidor online para as atividades offline implementadas.

Entre os sistemas já presentes no porte estão:

- Mundo 3D, terreno, mapas, objetos de cenário, água, iluminação e câmera.
- Personagens, equipamentos, animações, monstros, NPCs e portais.
- Combate, habilidades, inventário, itens, lojas, missões e salvamento offline.
- Interface modular: HUD, bolsa, status, habilidades, minimapa e outras janelas.
- Leitura e renderização parcial dos recursos históricos `.map`, `.lgo`, `.lab`, `.eff` e `.par`.
- Ferramentas de diagnóstico, auditoria de recursos, testes automatizados e perfil de desempenho.

**Atenção:** ter suporte para um formato ou habilidade não significa que todos os seus comportamentos estejam 100% iguais aos do DirectX original.

## Requisitos

### Sistema

- Linux com sessão gráfica **X11/GLX** (ex.: Ubuntu/Kubuntu).
- Placa gráfica e driver com suporte a **OpenGL 3.3**.
- Compilador com suporte a **C++17** (GCC/G++ ou compatível).
- **CMake 3.16+**.
- Bibliotecas de desenvolvimento de **OpenGL**, **X11** e **zlib**.

### Instalar dependências — Ubuntu / Kubuntu

```bash
sudo apt update
sudo apt install -y \
  git build-essential cmake pkg-config \
  libgl1-mesa-dev libx11-dev zlib1g-dev \
  mesa-utils
```

Para executar testes gráficos sem monitor/sessão X11, também é possível instalar:

```bash
sudo apt install -y xvfb xauth
```

Confira a versão do OpenGL disponível:

```bash
glxinfo -B
```

> `glxinfo` precisa de uma sessão gráfica funcional. Em sistemas Wayland, o jogo depende da disponibilidade do XWayland/GLX; o alvo nativo atual é X11.

## Estrutura do projeto e arquivos do cliente

A compilação é iniciada **na raiz do repositório**, onde se encontra o diretório `sources/`.

```text
TalesOfPirateDX9/
├── README.md
├── sources/
│   ├── CMakeLists.txt
│   ├── Engine/
│   ├── Platform/linux/
│   ├── Tools/
│   └── tests/
├── Client/                      # Recursos do jogo (fornecidos separadamente)
│   ├── map/
│   │   └── garner.map
│   ├── scripts/
│   │   └── table/
│   ├── effect/
│   ├── texture/
│   ├── model/                    # Quando presente no acervo utilizado
│   └── user/                     # Perfis e saves locais
└── build/                        # Gerado pelo CMake
```

O código-fonte **não substitui os recursos originais do cliente**. Para jogar, você precisa de uma cópia dos recursos de Tales of Pirates que esteja autorizado a utilizar, organizada de forma compatível com o porte. Algumas texturas podem precisar das versões PNG preparadas durante a migração.

Confira os recursos essenciais antes de iniciar:

```bash
ls sources/CMakeLists.txt
ls Client/map/garner.map
ls Client/scripts/table/terraininfo.txt
ls Client/scripts/table/areaset.txt
ls Client/scripts/table/skillinfo.txt
```

> Os nomes de arquivos no Linux diferenciam maiúsculas e minúsculas. O porte trata determinados nomes históricos, mas uma árvore `Client/` incompleta ainda pode impedir a execução.

## Instalação e compilação

### 1. Obter o código

Clone o **repositório que contém este porte OpenGL**, usando o endereço fornecido pelo botão **Code** no GitHub, e entre na pasta que contém `sources/`.

Se você já possui o projeto localmente:

```bash
cd ~/DEV/TOP2/TalesOfPirateDX9
```

**Não execute CMake nem `git apply` de dentro de `sources/`** nos exemplos deste README.

### 2. Compilar (recomendado para desenvolvimento)

```bash
cmake -S sources -B build \
  -DCMAKE_BUILD_TYPE=Debug \
  -DTOP_BUILD_UI_TESTS=ON

cmake --build build -j"$(nproc)"
```

O executável principal será criado em:

```text
build/top_gl
```

### 3. Verificar os recursos

```bash
./build/top_gl --check
```

Esse modo verifica mapas e diversos recursos, sem abrir a janela do jogo. Alguns diagnósticos exigem o conjunto de recursos históricos esperado pelo porte.

### 4. Compilar em Release (opcional)

Para manter a compilação Debug e criar uma Release separada:

```bash
cmake -S sources -B build-release \
  -DCMAKE_BUILD_TYPE=Release \
  -DTOP_BUILD_UI_TESTS=OFF

cmake --build build-release -j"$(nproc)"

./build-release/top_gl
```

> A versão Release usa um diretório de compilação diferente. Os comandos das seções seguintes usam `./build/`, da compilação Debug.

### 5. Recompilar depois de atualizar o código

```bash
cmake --build build -j"$(nproc)"
```

Se o CMake adicionar novos testes ou fontes:

```bash
cmake -S sources -B build -DCMAKE_BUILD_TYPE=Debug -DTOP_BUILD_UI_TESTS=ON
cmake --build build -j"$(nproc)"
```

## Como abrir o jogo

Execute **na raiz do repositório**:

```bash
./build/top_gl
```

Abrir em Argent City (`garner`):

```bash
./build/top_gl --map garner
```

Escolher a aparência/personagem inicial pela linha de comando:

```bash
./build/top_gl --character lance
./build/top_gl --character carsise
./build/top_gl --character phyllis
./build/top_gl --character ami
```

### Execução com os modos gráficos históricos usados nos testes recentes

```bash
TOP_HISTORICAL_BLEND=1 \
TOP_HISTORICAL_MODEL_TEXTURE=1 \
TOP_FROZEN_RESULT_GROUND=1 \
TOP_HISTORICAL_FILTERS=1 \
TOP_HISTORICAL_PARTICLE_POOL=1 \
TOP_HISTORICAL_PARTICLE_MOTION=1 \
./build/top_gl --profile
```

**Importante:** essas opções são úteis para comparar as migrações recentes, mas algumas continuam **experimentais**. O gelo do **Frozen Arrow** foi confirmado visualmente com `TOP_HISTORICAL_MODEL_TEXTURE=1` e `TOP_FROZEN_RESULT_GROUND=1`; não presuma o mesmo resultado se abrir o jogo sem essas variáveis.

## Controles

| Tecla / ação | Função |
|---|---|
| Clique esquerdo no chão | Caminhar até o ponto selecionado |
| Clique esquerdo em monstro | Selecionar/atacar |
| Botão direito + arrastar | Girar a câmera |
| `W`, `A`, `S`, `D` | Movimentação alternativa |
| `1`–`8` ou `F1`–`F8` | Utilizar habilidades nos atalhos |
| `I` | Inventário |
| `C` | Status do personagem (ou canhão no modo barco) |
| `K` | Habilidades |
| `E` | Interagir/conversar com NPC |
| `M` | Viajar para outro mapa, no modo de teste offline |
| `Insert` | Sentar/levantar |
| `B` | Embarcar/desembarcar no modo experimental |
| `N` | Janela do barco |
| `Alt + B` | Abrir bancada visual de forja |
| `Alt + F` | Preparar operação de forja, quando houver materiais |
| `Alt + T` | Combinar itens, quando aplicável |
| `Alt + M` | Operação de encaixe/perfuração, quando aplicável |

Na interface também é possível arrastar habilidades para a barra `F1`–`F8`, melhorar habilidades pelo botão `+` e interagir com itens e lojas.

### Atalhos de desenvolvimento / GM offline

> São ferramentas de teste, não parte da progressão normal do jogo.

| Tecla | Função |
|---|---|
| `Alt + G` | Adicionar experiência de teste |
| `Alt + H` | Recuperar vida/recursos |
| `Alt + J` | Invocar monstro para testes |
| `Alt + U` | Conceder item indicado em `--gm-item` |
| `Alt + O` | Abrir encaixes do equipamento de teste |
| `Alt + L` | Adicionar ouro de teste |

Parâmetros para testar essas ações:

```bash
./build/top_gl --gm-mob 201 --gm-item 890 --gm-xp 100000 --gm-gold 1000000
```

## Parâmetros de execução

| Parâmetro | Descrição |
|---|---|
| `--map NOME` | Abrir um mapa disponível (ex.: `garner`) |
| `--character NOME` | Selecionar `lance`, `carsise`, `phyllis` ou `ami` |
| `--check` | Verificação offline de recursos e sistemas |
| `--spawns` | Inspecionar spawns históricos sem abrir a janela |
| `--spawn-near` | Iniciar perto de uma região de spawn, para diagnóstico |
| `--perf` | Mostrar indicadores básicos de desempenho |
| `--profile` | Mostrar perfil detalhado de desempenho |
| `--startup-profile` | Registrar tempos de inicialização |
| `--startup-report ARQUIVO` | Definir CSV de inicialização e ativar seu registro |
| `--vsync on` / `--vsync off` | Solicitar ativação/desativação do VSync |
| `--no-gpu-cache` | Desativar cache de GPU para comparação |
| `--no-actor-cache` | Desativar cache de atores |
| `--no-frustum-culling` | Desativar descarte por campo de visão |
| `--shot ARQUIVO.ppm` | Capturar imagens de teste em formato PPM |
| `--skill-preview ID` | Prévia visual de habilidade; exige `--shot` |
| `--skill-preview-channel cast\|flight\|hit` | Canal da prévia |
| `--skill-preview-time SEGUNDOS` | Instante da prévia |

Exemplos:

```bash
# Diagnóstico geral
./build/top_gl --check

# Investigar spawns
./build/top_gl --map garner --spawns

# Ver desempenho
./build/top_gl --profile

# Comparar VSync
./build/top_gl --profile --vsync off

# Medir inicialização e salvar CSV
./build/top_gl --startup-report build/startup_profile.csv --profile

# Comparar renderização sem caches e descarte
./build/top_gl --profile --no-gpu-cache --no-actor-cache --no-frustum-culling
```

## Modos gráficos e efeitos históricos

Estas variáveis são definidas **antes** do comando do jogo. `1` ativa e `0` desativa a opção correspondente.

| Variável | Finalidade |
|---|---|
| `TOP_HISTORICAL_BLEND` | Fatores de transparência próximos aos estados DX9 |
| `TOP_HISTORICAL_FILTERS` | Filtros POINT/LINEAR conforme os efeitos históricos |
| `TOP_HISTORICAL_MODEL_TEXTURE` | Dá prioridade à textura declarada no `.eff` sobre o material `.lgo` |
| `TOP_FROZEN_RESULT_GROUND` | Ancora o resultado de Frozen Arrow ao terreno (**experimental**) |
| `TOP_HISTORICAL_PARTICLE_POOL` | Emissão e vida individuais de SNOW/FIRE/BLAST (**experimental**) |
| `TOP_HISTORICAL_PARTICLE_MOTION` | Movimento histórico aproximado; requer o pool ativo (**experimental**) |
| `TOP_HISTORICAL_DUMMY_BIND` | Teste de associação de efeitos aos dummies do personagem |
| `TOP_STRIP_NESTED_LOOP` | Controla a repetição de STRIP com modelos aninhados |

### Comparar o Frozen Arrow

Com a seleção de textura histórica e o posicionamento experimental:

```bash
TOP_HISTORICAL_BLEND=1 \
TOP_HISTORICAL_MODEL_TEXTURE=1 \
TOP_FROZEN_RESULT_GROUND=1 \
./build/top_gl --profile
```

Reproduzir o caminho antigo de textura, apenas para diagnóstico:

```bash
TOP_HISTORICAL_BLEND=1 \
TOP_HISTORICAL_MODEL_TEXTURE=0 \
TOP_FROZEN_RESULT_GROUND=1 \
./build/top_gl --profile
```

### Ativar logs dos efeitos

```bash
TOP_HISTORICAL_BLEND=1 \
TOP_HISTORICAL_MODEL_TEXTURE=1 \
TOP_FROZEN_RESULT_GROUND=1 \
TOP_TRACE_SKILL_FX=1 \
TOP_TRACE_FROZEN_GEOMETRY=1 \
./build/top_gl --profile 2>&1 | tee frozen.log
```

Filtrar as linhas úteis:

```bash
grep -E '\[frozen-geometry\]|\[skill-fx\]' frozen.log | head -60
```

Outros diagnósticos opcionais incluem `TOP_TRACE_EFFECT_FILTERS=1`, `TOP_TRACE_STRIP_SYSTEM=1`, `TOP_TRACE_DIRECT_STRIP=1` e `TOP_TRACE_FROZEN_RESULT=1`.

> Os modos experimentais não equivalem a uma reprodução exata das animações e simulações do DirectX 9.

## Testes automatizados e testes de GPU

### Testes CTest

Certifique-se de configurar o CMake com `-DTOP_BUILD_UI_TESTS=ON`.

```bash
# Compilar todos os alvos
cmake --build build -j"$(nproc)"

# Executar os testes registrados no CTest
ctest --test-dir build --output-on-failure

# Listar os testes disponíveis
ctest --test-dir build -N

# Rodar apenas testes ligados às migrações recentes
ctest --test-dir build -R 'ui05sk2(14|15|16|17)' --output-on-failure

# Reexecutar somente os que falharam na execução anterior
ctest --test-dir build --rerun-failed --output-on-failure
```

Os testes de CPU/integração não exigem necessariamente uma janela OpenGL. Já os testes gráficos precisam de um contexto GL válido.

### Testes gráficos (OpenGL)

Os executáveis gráficos da migração são **separados do conjunto CTest headless**:

```bash
# Validação de fatores de blend
./build/top_effect_blend_gpu_tests

# Validação de filtros de textura POINT/LINEAR
./build/top_effect_filter_gpu_tests
```

Com Xvfb (quando disponível):

```bash
xvfb-run -a ./build/top_effect_blend_gpu_tests
xvfb-run -a ./build/top_effect_filter_gpu_tests
```

Se algum executável não existir, confira se os testes foram ativados no CMake e se a versão do repositório inclui aquela migração.

## Ferramentas de auditoria

As ferramentas abaixo não iniciam o jogo completo. Sua disponibilidade depende da versão dos fontes.

### Integridade dos arquivos e mapas

```bash
./build/top_gl --audit-assets --client Client \
  --json build/assets.json --csv build/assets.csv
```

Para um único mapa:

```bash
./build/top_asset_audit --client Client --map garner \
  --json build/garner-assets.json --csv build/garner-assets.csv
```

O modo `--strict` transforma determinadas inconsistências em erro de execução:

```bash
./build/top_asset_audit --client Client --strict
```

### Auditoria de partículas `.par`

```bash
./build/top_par_audit --client Client --out build/par-audit
```

### Inspecionar um efeito `.eff`

```bash
./build/top_eff_inspect Client/effect/icearrowh.eff
```

### Auditoria do blend histórico

```bash
./build/top_blend_audit --client Client --out build/blend-audit
```

### Cobertura de efeitos das habilidades

```bash
./build/top_fx_coverage_audit \
  --client Client \
  --out build/fx-coverage \
  --deps sources/auditoria-historica/dependencies.csv
```

### Outros diagnósticos disponíveis

```bash
# Listar stats históricos de um monstro (ID de exemplo)
./build/top_stats_inspect Client 201

# Exportar canais declarados das skills para CSV
./build/top_skill_fx_probe Client build/skill_fx.csv

# Auditar a tabela histórica de execução de skills
./build/top_skill_execution_audit Client/scripts/table/skillinfo.txt --csv \
  > build/skill_execution.csv
```

As auditorias podem apontar dados faltantes ou mecanismos ainda aproximados. Isso é útil para desenvolver o porte e não significa necessariamente que a aplicação inteira esteja quebrada.

## Capturas e testes de habilidades

A opção `--shot` grava **PPM**, não PNG, e pode produzir uma segunda captura com o sufixo `-walk`.

```bash
# Capturar a cena
./build/top_gl --shot build/cena.ppm

# Frozen Arrow (ID 93): canal de impacto
TOP_HISTORICAL_MODEL_TEXTURE=1 \
TOP_FROZEN_RESULT_GROUND=1 \
./build/top_gl \
  --skill-preview 93 \
  --skill-preview-channel hit \
  --skill-preview-time 0.50 \
  --shot build/frozen-arrow.ppm

# Illusion Slash (ID 81): canal de voo
./build/top_gl \
  --skill-preview 81 \
  --skill-preview-channel flight \
  --skill-preview-time 0.50 \
  --shot build/illusion-slash.ppm
```

A prévia é **visual**; não substitui o teste de conjuração, colisão e impacto durante o combate real.

## Logs, desempenho e solução de problemas

### Salvar um log do jogo

```bash
./build/top_gl --profile 2>&1 | tee build/top_gl.log
```

### Erro: `GL/gl.h: No such file or directory`

Confira as dependências de desenvolvimento:

```bash
sudo apt install -y libgl1-mesa-dev libx11-dev zlib1g-dev
```

Depois reconfigure e recompile:

```bash
cmake -S sources -B build -DCMAKE_BUILD_TYPE=Debug -DTOP_BUILD_UI_TESTS=ON
cmake --build build -j"$(nproc)"
```

### Erro ao abrir janela gráfica

Verifique a sessão e os drivers:

```bash
echo "$DISPLAY"
glxinfo -B
```

O executável `top_gl` utiliza GLX/X11; não funciona como aplicativo gráfico puramente headless.

### Erro: mapa ou recursos ausentes

Confirme a localização da pasta `Client/` e execute:

```bash
ls Client/map/garner.map
./build/top_gl --check
```

### Erro: teste aparece como `Not Run`

Compile todos os alvos antes de executar o CTest:

```bash
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure
```

### Compilar novamente do zero

Em vez de apagar saves ou arquivos do cliente, elimine **somente** o diretório de build gerado:

```bash
rm -rf build
cmake -S sources -B build -DCMAKE_BUILD_TYPE=Debug -DTOP_BUILD_UI_TESTS=ON
cmake --build build -j"$(nproc)"
```

> **Atenção:** use `rm -rf build` apenas na raiz correta do projeto. Os recursos em `Client/` e os saves não precisam ser apagados.

## Atualizações por patch

Caso esteja recebendo migrações como arquivos `.patch`, aplique-as **na raiz**, respeitando a versão anterior exigida pela migração:

```bash
# Exemplo: troque NOME.patch pelo patch apropriado
# O patch deve conter caminhos começando por sources/
git apply --check NOME.patch
git apply NOME.patch

cmake -S sources -B build -DCMAKE_BUILD_TYPE=Debug -DTOP_BUILD_UI_TESTS=ON
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure
```

**Não** aplique todas as migrações antigas sobre uma cópia do repositório que já contém essas mudanças. Faça backup/commit das alterações locais antes de atualizar.

## Limitações e observações

- Este é um **porte em desenvolvimento**, não uma versão final ou uma reprodução 100% fiel do cliente original.
- Nem todos os estados de personagem, habilidades, animações, partículas, sistemas navais, missões ou interfaces foram validados integralmente.
- O **Frozen Arrow** teve a textura histórica recuperada; a ancoragem ao chão permanece experimental.
- O **Illusion Slash** tem melhorias nos efeitos, mas algumas transformações ainda diferem do jogo original.
- Recursos do cliente e direitos sobre Tales of Pirates pertencem aos respectivos titulares. Não publique arquivos proprietários sem autorização.
- O modo offline e as ferramentas de GM servem a testes locais; suporte a servidor/rede não é o objetivo imediato deste porte.

### Contribuições

Relatórios de bugs são especialmente úteis quando incluem **versão/commit**, comando de execução, configuração das variáveis `TOP_*`, log do terminal e, para problemas visuais, comparação com o cliente original.

---

**Objetivo:** tornar Tales of Pirates jogável no Linux, offline, recuperando o funcionamento histórico **sem trocar efeitos originais por aproximações genéricas quando os recursos e as regras reais podem ser reproduzidos**.
