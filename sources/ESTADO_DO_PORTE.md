# Estado do porte Linux/OpenGL

Atualizado em 23/10/2026. Documento principal de diagnóstico e planejamento. Substitui os levantamentos e roteiros antigos removidos nesta revisão. Os arquivos MIGRATION_*.md descrevem etapas históricas, não certificam o estado atual.

## Escopo e conclusão

Comparação entre o executável C++17 offline compilado por sources/CMakeLists.txt, as fontes originais de cliente/engine em arquivo/fontes, os scripts históricos do GameServer e os recursos de Client/. O alvo considerado é a experiência de jogo offline, com aparência e regras equivalentes onde aplicáveis.

**Estimativa de planejamento: aproximadamente 55% do porte offline, com faixa de incerteza de 45–65%.** Há uma base jogável e uma infraestrutura significativa; ainda falta bastante fidelidade, cobertura de regras/conteúdo e validação em sessão real. A faixa não é estatística e não representa porcentagem de linhas de código, horas restantes nem aprovação de 55% de uma lista completa de comportamentos.

A rede do MMORPG, autenticação, grupos de jogadores, guildas online, comércio entre jogadores e infraestrutura de servidores ficam fora desse denominador. Incluí-los exigiria outro inventário e outro orçamento. Sistemas individuais como progressão, produção, pets e navegação precisam ser classificados dentro do escopo offline; ter sua janela ou seus itens no catálogo não comprova a mecânica.

## Como cheguei à estimativa

Pesos e notas abaixo são julgamento técnico explícito para priorização, baseados nos caminhos de implementação inspecionados. Não são cobertura medida por testes. A soma ponderada é 54,45 pontos de 100, arredondada para 55. Conteúdo não auditado integralmente reduz a confiança.

| Área | Peso no escopo | Nota estimada de conclusão | Evidência e principal lacuna |
|---|---:|---:|---|
| Plataforma, build e carga de recursos | 8 | 80% | Runtime Linux/OpenGL e leitores funcionais; distribuição e portabilidade ainda incompletas. |
| Mapas, colisão, população e deslocamento | 12 | 65% | Mapas, portais, pathfinding, spawns e memória de monstros; faltam recursos e equivalência da dinâmica/eventos. |
| Renderização do mundo, materiais e mar | 12 | 50% | Terreno, objetos, luz e água têm implementação; mar em seções vazias diverge e falta comparação visual sistemática. |
| Personagens, animações e efeitos | 15 | 40% | Skinning, poses e tempos existem; canais, pontos de ligação e eventos de efeitos estão incompletos. |
| Combate, atributos, skills e estados | 15 | 55% | Diversas fórmulas históricas portadas; permanecem aproximações e callbacks genéricos. |
| Itens, equipamento, economia e produção | 10 | 65% | Bolsa, UIDs, lojas, cofre, forja e parte da geração; uso de itens e sistemas avançados têm cobertura parcial. |
| Missões, diálogos e progressão de classes | 10 | 50% | Aceitar/progredir/entregar e condições suportadas; não há execução geral da semântica Lua. |
| Barcos e jogo naval | 8 | 25% | Modelos, movimento e canhão local; velocidade/dano e fluxo de embarque ainda simplificados. |
| Interface, seleção de personagens e áudio | 5 | 65% | Várias telas, perfis e áudio por área; faltam fluxos históricos, personalização e validação visual. |
| Persistência, qualidade e distribuição | 5 | 65% | Save com checksum/backup e testes; falta matriz de equivalência, pacote reproduzível e validação ampla. |

A infraestrutura está mais avançada que a fidelidade. Os 52 mapas válidos não significam 52 mapas integralmente portados; os 411 registros visuais de skills não significam 411 skills corretas.

## Evidência executada nesta revisão

- Build completo de build/: aprovado.
- CTest: **71/71 aprovados**.
- ./build/top_gl --check: aprovado no cenário inicial de Garner.
- Auditoria de recursos reexecutada: 53 entradas de mapas, 52 cabeçalhos válidos; 47 arquivos .obj examinados.
- 266.852 posicionamentos de objetos; 628 modelos distintos utilizados; **78 modelos utilizados ausentes**, afetando **1.944 posicionamentos em oito mapas**; 32 posicionamentos com ID não resolvido.
- Uma falta obrigatória, 87 opcionais e 13 diferenças de maiúsculas/minúsculas no inventário geral.
- O check carrega os 30 quadros de água e 411 registros de visuais de skills.
- Não houve sessão gráfica comparativa com o cliente original nesta análise. Os testes acima não certificam qualidade visual, controles em uso prolongado ou funcionamento integral de todos os mapas.
- A build examinada está sem CMAKE_BUILD_TYPE definido. Há testes que usam assert; builds Release com NDEBUG podem eliminar verificações. Isso precisa ser corrigido antes de usar resultados Release como garantia.

Resultados desta revisão estão em auditoria-2026-10-23/ com prefixo revisao-atual-. A auditoria de recursos comprova arquivos/cabeçalhos e referências, não execução completa de scripts.

## Achados prioritários, comparados com o original

### 1. Mar: implementação existente, mas tratamento de regiões vazias divergente — prioridade imediata

Original: arquivo/fontes/Engine/include/MPMap.h, GetTile, retorna um tile padrão quando a seção não tem dados. MPMap.cpp configura esse tile com UNDERWATER_HEIGHT=-2.0 e textura 22. RenderSea usa esse fundo e as quadras de 4 m.

Atual: Engine/src/top/MapFile.cpp retorna Tile inválido em seção sem dados; TerrainMesh.cpp ignora tiles inválidos antes de ativar seaNeeded e dá alpha zero aos vértices de água cujo tile é inválido. Existe geração de água em mode 7, shader e seleção das 30 texturas em Platform/linux/main.cpp.

**Consequência demonstrável pelo código:** uma janela só com seções sem dados não ativa a geração de mar; em janelas mistas, amostras inválidas ficam transparentes. É uma causa provável do mar ausente relatado, ainda sem confirmação gráfica do local específico. sailableAt também recusa tiles inválidos; desenho e navegação precisam de contratos coerentes.

Próxima tarefa: reproduzir a política histórica de fundo submerso dentro dos limites do mapa, distinguir seção legitimamente vazia de arquivo ilegível e revisar navegação sem liberar área corrompida/fora do mapa. Não inventar água moderna/reflexos como requisito: primeiro igualar o caminho DX9 realmente usado.

Aceite: costa, mar aberto sem seção, borda de mapa e chegada de barco; captura com mesma câmera/posição no original; teste de malha/alpha e regressão de colisão.

### 2. Skills: temporização melhorou, composição visual ainda parcial — prioridade imediata

Original: Client/src/STAttack.cpp dispara efeitos do personagem, item, voo e alvo em keyframes próprios, com pontos de ligação (dummies). Libraries/common/src/SkillRecord.cpp lê listas de efeitos, tipos, ligação à arma, modos de seleção de ação e quadros de disparo.

Atual:
- SkillVisual.h guarda apenas o primeiro ID de cada campo; castId escolhe actor OU item OU itemExtra; hitId escolhe target OU ground OU water. Isso não representa todos os canais nem a seleção correta pela superfície.
- EffectSystem::emit cria uma instância selecionada por evento. Há suporte real a .eff, partículas, malhas nativas e .lgo, mas também fallbacks procedurais.
- Malhas de efeitos usam, em caminhos inspecionados, posição no chão + 0,9 m. Isso não equivale à matriz de mão/arma/corpo do original.
- ActorSkin::PoseSpan retém um impacto por ação, enquanto o original possui contagem/lista de keyframes.
- choosePoseAction percorre a lista por sequência; o carregador não lê o modo histórico chActionPlayType da coluna 46.
- As correções recentes sincronizam duração, velocidade, início/impacto/voo e cancelamento de eventos. Elas não resolvem todos esses pontos de composição e seleção.

Próxima tarefa: selecionar algumas skills representativas e concluir o caminho inteiro: ação, todos os eventos, dummies, voo, alvo/solo/água, mistura de material e encerramento. Investigar transições/cancelamentos junto com a máquina de estados original. Não resolver adicionando partículas genéricas a toda skill.

Aceite: ataque corpo a corpo, arco/arma de fogo, Illusion Slash, magia com voo, buff e skill de área; quatro raças, armas compatíveis, 30/60 FPS, repetição, interrupção e alvo morto. Comparar vídeo em velocidade normal e quadro a quadro.

### 3. Combate e estados: regras históricas convivem com aproximações — criticidade alta

Há classificações explícitas para várias fórmulas, além de implementações recentes de Heal, Berserk, Harden, Will of Steel, Shadow Slash, Poison Dart, Enfeeble e callbacks de monstros. Os atributos e bônus de equipamento também avançaram.

Entretanto, PlayCombat.cpp ainda infere certos buffs/curas a partir de palavras como heal, recover, shield e protect; SkillRule começa em DamageFormula::Generic; algumas fórmulas ausentes usam alternativas locais. Isso pode permitir uma skill aparentemente funcional com números e efeitos diferentes.

Faltam cobertura e validação sistemáticas de passivas, resistências, estados, remoção/reaplicação/acúmulo, alvos e formatos de área, exceções de mapas/bosses, bônus especiais e interações entre equipamentos e skills. As classes avançadas precisam de seu fluxo completo de promoção e conteúdo; ler coeficientes de classe não o implementa.

Próxima tarefa: catálogo explícito de suporte por callback/ID, com origem histórica e casos de teste independentes. Diferenciar fiel, parcial, aproximado e indisponível na ferramenta de diagnóstico. Priorizar a classe que será jogada do nível inicial até a promoção seguinte.

### 4. Itens e economia: já há bastante coisa; geração histórica não está completa — criticidade alta

Implementados: inventário, pilhas, identidades de instância, equipamento/requisitos, lojas, cofre, várias caixas/tickets, gemas, sockets e operações de forja. Não devem voltar à lista de “não portados”.

O código atual de ItemInstantiationRules.cpp já possui geração de qualidade/afixos para os eventos 11 e 95 e tipos delimitados, usando UID como semente local. PlayItems.cpp prepara recompensas antes de consumir a caixa e rejeita geração não suportada. Portanto, os relatórios antigos que descreviam somente defaults e todas essas caixas bloqueadas foram superados.

Falta generalizar as origens/tipos realmente presentes em Creat_Item, verificar distribuição e atributos contra o Lua, ampliar callbacks de uso e auditar produção avançada, pets/fadas e bônus vinculados. Semente determinística local é uma decisão offline; não demonstra equivalência do RNG do servidor.

Aceite: comprar/ganhar/usar/equipar/salvar/reabrir sem perder UID ou atributos, bolsa cheia sem consumo parcial e probabilidades verificadas para cada família suportada.

### 5. Missões e NPCs: subconjunto funcional, não um motor completo de scripts — criticidade alta

MissionCatalog.cpp reconhece um conjunto de condições/ações e registra unsupported para outras. Existem eventos de abate/coleta, histórico, diário, requisitos e entrega. PlayMissions.cpp bloqueia condições não suportadas. A primeira promoção possui regra histórica de nível 9/2.500 ouro/restrições de raça.

NPCs ainda usam extração de scripts. Reconhecer a presença de OpenBank não reproduz arbitrariamente toda a árvore condicional do diálogo. Recompensas e eventos complexos precisam de inventário próprio; AddExp com intervalo ainda tem caminho de limite inferior determinístico.

Falta: cadeias multi-NPC, triggers adicionais, flags/ramificações, promoções avançadas e missões de sistemas especiais. Decidir explicitamente quais eventos temporários pertencem à versão offline. Não ativar todo CreateCha extraído como se todos os scripts de evento devessem rodar juntos.

### 6. Barcos: uma das maiores lacunas de funcionalidade — dificuldade alta

Atual: cascos, altura de navegação, modo barco, caminho em água, esteira, seleção e canhão. Contudo, main.cpp usa velocidade de barco fixa em 10; embarque/desembarque tem atalho com procura de terreno próximo; fireBoatCannon usa dano 18 + nível*3 - defesa/3 e recarga fixa de 1,8 s.

Isso é uma base de teste, não o sistema naval completo de CharBoat.cpp e dos scripts/tabelas históricos. A fórmula portada para o callback de canhão de monstros não substitui esse disparo local do jogador.

Faltam verificar/implementar: identidade persistente da embarcação, construção/compra, partes e atributos, HP/recursos, reparo/abastecimento, carga, progressão naval, regras de portos e habilidades. Separar o estado do barco do estado do personagem antes de ampliar a economia naval.

### 7. Mundo, materiais, IA e conteúdo — dificuldade variável

A navegação usa regiões LAND/BRIDGE e grade de bloqueio; há portais, spawns e memória de HP/morte/respawn por região. A IA possui perseguição, fuga, retorno e linha de visão. São avanços reais.

Falta auditar a população e a lógica por mapa, eventos/bosses, transições longas e limites de entidades locais. Recursos ausentes exigem buscar correspondência no acervo antes de considerar modelagem ou substituição. O contador de modelos animados no check não prova que toda animação/material foi reproduzido.

Renderização: conferir transparência e ordem de desenho, misturas de material, luz, sombras, animação UV/texturas e efeitos anexados. Não assumir que neblina, reflexos ou outros recursos são obrigatórios apenas por existirem em algum trecho antigo: confirmar o caminho habilitado na versão alvo.

### 8. Interface, áudio, saves e entrega — dificuldade baixa a alta conforme o contrato

UI foi separada em top_ui. Há inventário, habilidades, missões, loja, cofre, forja, status, minimapa, barcos, sistema e seleção de personagens. Perfis têm identidade independente da raça; não se deve descrever a seleção como totalmente ausente.

Pendências: personalização completa, equivalência dos fluxos/telas, texto extenso, escalas/resoluções, foco e arraste, serviços antigos ainda sem mecânica correspondente. Música por área já existe; conferir gatilhos, volumes, sons de skills/impactos e sons ambientes.

SaveRecovery.cpp está integrado ao Play: checksum, backup, recuperação e gravação atômica já existem. Falta auditar o conjunto de estado que deve sobreviver a sair/reabrir, posição/mapa, efeitos, entidades e futuras extensões navais. Não confundir preservar monstros durante streaming com persistir todo o mundo entre execuções.

CMake instala apenas o executável. Faltam pacote de recursos/dependências, localização estável dos dados, instruções de distribuição, build limpa e testes em outra instalação. A árvore de trabalho tem muitas mudanças/arquivos ainda não versionados; uma referência reproduzível deve ser estabelecida antes de grandes frentes simultâneas, preservando todo o trabalho local.

## Sequência recomendada e modelo por tarefa

As escolhas abaixo são recomendações para este repositório, não resultado de um benchmark local entre modelos. A documentação atual recomenda GPT-6.1 Sol para trabalho complexo recorrente, Astra para os problemas mais exigentes e Luna para tarefas claras/repetíveis. Fonte: [modelos e esforço de raciocínio](https://learn.chatgpt.com/docs/models), consultada em 23/10/2026.

| Ordem | Entrega concreta | Dificuldade | Modelo / esforço sugerido | Critério de conclusão |
|---|---|---|---|---|
| 1 | Corrigir mar/seções vazias e verificar costa | Média; sobe se envolver render state | GPT-6.1 Sol High; Astra High se a divergência persistir | Mar aberto e costa comparados, navegação e limites testados. |
| 2 | Uma família de skills fiel de ponta a ponta | Muito alta | GPT-6 Astra High/Extra High | Mesmos eventos, poses, pontos de ligação e efeitos em vídeo comparativo. |
| 3 | Remover aproximações de uma classe/estado por vez | Alta | GPT-6.1 Sol High; Astra para interações/fórmulas ambíguas | Casos numéricos derivados do original e ciclo completo de uso. |
| 4 | Instâncias de itens e serviços econômicos restantes | Alta | GPT-6.1 Sol High; Astra para Creat_Item e save transacional | Recompensas/UID/probabilidades/salvamento consistentes. |
| 5 | Missões e segunda promoção de uma classe | Alta | Astra para contrato Lua; Sol High para casos delimitados | Cadeia completa, ramificações e rejeições corretas. |
| 6 | Estado e ciclo completo de uma embarcação | Muito alta | Astra High/Extra High na arquitetura; Sol High por operação | Comprar/construir, navegar, lutar, reparar e persistir. |
| 7 | Auditar/recuperar recursos por mapa | Baixa a média | Luna High para inventário/mapeamento; Sol para integração | Referências resolvidas com origem comprovada e cena inspecionada. |
| 8 | Fluxos de UI e áudio já especificados | Baixa a média | Sol Medium; Luna High para ajustes bem delimitados | Teste de interação e captura na resolução alvo. |
| 9 | Empacotamento e checks de documentação/build | Baixa a média | Luna High ou Sol Medium | Instalação limpa reproduzível com recursos disponíveis. |
| Contínua | Revisar mudanças de alto risco | Alta | Astra High em revisão independente | Comparação com fonte antiga e testes; sem depender só da explicação da implementação. |

Não é necessário usar esforço máximo em tudo. Um modelo mais forte não recupera sozinho referência visual ausente. O melhor investimento é dar fontes exatas, comportamento esperado e um caso de aceite pequeno. Use Sol como padrão; reserve Astra para incerteza e interações profundas. Luna rende mais quando a regra já está decidida.

## Organização recomendada para manutenção

Manter C++17 e extração incremental. Já existem top_ui, top_item, mission/, npc/ e a divisão de Play em play/; aproveitar essa direção. Evitar uma reescrita geral do motor neste momento.

Responsabilidades sugeridas (estrutura alvo, não descrição de pastas já criadas):

```text
Engine/
  include/top/          contratos públicos pequenos
  src/assets/          leitores e validação dos formatos históricos
  src/world/           mapas, colisão, portais, população
  src/combat/          regras numéricas, estados, resolução de golpes
  src/animation/       poses, relógio e eventos de keyframe
  src/effects/         recursos, instâncias e pontos de ligação
  src/item/            inventário, instâncias, geração, economia
  src/mission/         catálogo, condições, eventos e recompensas
  src/npc/             diálogos e serviços
  src/naval/           barcos e suas regras próprias
  src/persistence/     serialização, versões, validação e recuperação
  src/gl/              desenho e recursos da GPU
  ui/                  apresentação e comandos do jogador
Platform/linux/        janela, entrada e composição da aplicação
tests/                 regressões por domínio e cenários integrados
docs/                  fontes históricas, decisões e matriz de equivalência
```

- **Regra pura separada da orquestração.** Fórmula de dano recebe atributos/estado e retorna resultado; não lê arquivo, não chama GL e não altera UI.
- **Catálogo imutável separado de estado vivo.** SkillDefinition descreve a habilidade; SkillInstance/StatusInstance guarda nível, alvo, tempo e origem de uma execução.
- **Eventos explícitos de animação e combate.** ActionStarted, KeyframeReached, ProjectileLaunched, ImpactResolved e ActionCancelled devem compartilhar identidade/ciclo. A apresentação consome esses dados; não recalcula o dano.
- **A hierarquia de eventos deve respeitar o original.** Alguns golpes resolvem no quadro de ação; outros após o voo. Não impor um único instante de dano a todas as skills.
- **Pontos de ligação como contrato.** O renderer resolve a matriz de corpo/arma/dummy para efeitos. Evitar posições mágicas como chão+0,9 espalhadas pelo sistema.
- **Estado com nomes corretos.** PlayWorld.cpp ainda usa player_.homeX/homeY para máximo/valor de SP. Separar ResourcePool de posição/home antes de expandir o domínio.
- **Adapters nas bordas.** UI emite comandos; gameplay devolve estado para apresentação; save traduz esse estado para formato versionado. Play pode permanecer uma fachada temporária.
- **Uma fonte de verdade para regras.** Guardar ID/callback, arquivo/função original, versão dos dados, suporte e teste. Não replicar fórmulas em UI, combate e animação.
- **Extração de Lua por subconjuntos explícitos.** Ampliar apenas os comandos comprovados e diagnosticar os desconhecidos. Um interpretador Lua embutido seria uma decisão arquitetural separada, com API local bem delimitada; não é pré-requisito imediato.
- **Tratar operações econômicas como transações.** Validar, simular, confirmar e salvar; nenhuma recompensa parcial por falta de espaço.
- **Refatorar junto da funcionalidade.** ActorSkin.cpp e main.cpp ainda concentram muitas responsabilidades. Extrair catálogo/relógio/skinning/preview e aplicação/input/renderização em etapas pequenas, mantendo equivalência.
- **CMake por bibliotecas e função de testes.** Reduzir listas repetidas de fontes nos alvos de teste, manter verificação ativa em Release e tirar testes grandes do executável de produção gradualmente.
- **Distinguir código ativo de cópia antiga.** Engine/src/top/NpcSystem.cpp não é o NpcSystem compilado por CMake; o alvo usa src/npc/NpcSystem.cpp e src/mission/MissionCatalog.cpp. Revisar/remover a duplicação em tarefa própria. Esta limpeza de documentação não remove código.
- **Nomes por domínio, não por migração.** Novos testes devem explicar o comportamento; manter os registros MIGRATION como histórico. Evitar novas camadas genéricas sem necessidade concreta.

## Como transformar a porcentagem em acompanhamento confiável

Criar uma matriz por comportamento, com: domínio, ID/callback, referência original, versão dos dados, status, teste automatizado, evidência visual, risco e próxima ação.

Status recomendados: não inventariado; ausente; aproximado; parcial; fiel em teste; fiel validado em jogo. “Carregado” deve ser uma coluna de recursos, não sinônimo de “portado”.

Para cada tarefa, entregar:
1. Regra original e exemplo reproduzível.
2. Alteração delimitada em um domínio.
3. Teste com expectativa derivada da fonte histórica, inclusive rejeição/erro.
4. Validação visual/interativa quando houver efeito na tela.
5. Atualização da matriz e deste documento, sem criar outro diagnóstico concorrente.

O próximo marco útil é uma experiência pequena completa: criar personagem, jogar uma classe, usar skills fiéis, concluir missões, comprar/equipar, sair e voltar, chegar à costa e navegar. Consolidar essa experiência dará uma medida melhor do que acrescentar centenas de registros parcialmente suportados.

## Limpeza documental desta revisão

Removidos os diagnósticos/roteiros superados: ANALISE_PORTE_GL_2026-10-23, LEVANTAMENTO_05-S, ambos ROADMAP_POST_MISSOES, PROMPT_PROXIMA_MIGRACAO, os resumos antigos ITEM_ACTION_AUDIT_05-AW e ITEM_CHEST_AUDIT_05-AX; e, em offline/docs, auditoria-porte, falta-porte, levantamento, decisao-grafico, escopo-reescrito, skill-spiritual-bolt e opengl-port.

O guia foi atualizado para apontar para este documento. A especificação de formato de mapa e a tabela de tickets foram preservadas como referências específicas. Os MIGRATION_*.md e CSVs antigos permanecem como registros datados; não usar suas contagens/status como cobertura atual. Fontes originais, assets e código de gameplay não foram removidos nesta revisão.

