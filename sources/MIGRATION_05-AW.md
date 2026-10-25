# Tales of Pirates OFFLINE — migração 05-AW

## Origem

Incremento sobre **05-AV**, sem recomeçar nem alterar recursos de `Client/` ou arquivos de save. Usuário validou a 05-AV no Ubuntu (65/65 testes, renderização e transição entre mapas). Esta etapa inicia a auditoria **itens → stats → skills e efeitos**.

## Alterações

- Nova biblioteca **`top_item`** em `Engine/src/item/ItemUseRules.cpp`, com API pública `Engine/include/top/ItemUseRules.h`. Compila sem motor gráfico, NPCs ou UI.
- Extração de 17 métodos de bolsa, uso, UID, transferência, equipar, venda e descarte para `Engine/src/item/PlayItems.cpp`. `Play` continua fachada de integração, e os tipos `ItemInfo` e `Stack` permanecem temporariamente no `Play.h` por compatibilidade. Esta migração **não representa** uma extração completa do estado dos itens.
- `JobNames.h` centraliza o nome de profissão, usado tanto por `Play.cpp` quanto por `PlayItems.cpp`.
- `ItemInfo::scriptAction` passa a preservar o texto da coluna 86 de `iteminfo.txt`; funções Lua desconhecidas nunca são executadas automaticamente, nem consomem o item ao clicar. O usuário vê a identificação do callback indisponível. `0` e itens de missão sem ação permanecem guardados; não inventamos comportamento.
- Corrigido o arraste de equipamento multi-slot: uma espada com slots `9,6` agora respeita o destino indicado no arraste, em vez de escolher automaticamente o primeiro slot disponível. Equipar/desequipar conserva o UID e recalcula atributos como antes.
- Novo auditor read-only `Tools/item_action_audit.py`, aceitando `Client/scripts` como entrada e gerando CSV/Markdown. O resultado de execução sobre o acervo histórico acompanha a migração em `ITEM_ACTIONS_05-AW.csv` (todos os registros) (registro histórico; o resumo Markdown superado foi removido, e o estado atual está em [ESTADO_DO_PORTE.md](ESTADO_DO_PORTE.md)). **Nenhum script Lua é executado.**

## Resultados da auditoria

Dos **6.343** registros legíveis de `iteminfo.txt`: **2.816** com metadados de equipamento, **2.619** sem callback de clique e sem slot (itens de missão, materiais e outros sistemas), **748** com callback Lua ainda não implementado diretamente ao usar, **68** medicamentos com valores HP/SP históricos encontrados, **72** livros mapeados por `variable.lua`, **13** livros sem correspondência localizada, **6** expansões reconhecidas e **1** medicamento sem fórmula localizada.

Essas classes são **diagnóstico do clique**, não prova de que 748 itens estão completamente inúteis: alguns são consumidos por serviços ou NPCs, e nem todos os 2.619 sem callback deveriam ser ativados por clique. Mesmo itens marcados `heal_amount_supported` ainda não reproduzem automaticamente os cooldowns, percentuais ou estados históricos. Não afirmar funcionalidades além das implementadas.

## Testes

- CMake configurado com `-DTOP_BUILD_UI_TESTS=ON`; alvo `top_regression_tests` compilado (headless).
- **66/66 testes CTest passaram**, inclusive `ui05v_play_state_regression` com dois novos casos: callback não suportado **não consome o item**, e arraste `9,6` equipa no slot solicitado preservando UID.
- Novo `ui05aw_modular_item_use_rules` verifica regras puras de slots, objetos sem slot, tipos de callbacks suportados e ainda não implementados.
- Checagem de sintaxe C++17 passou para `Play.cpp`, `PlayItems.cpp`, `ItemUseRules.cpp` e `PlayMissions.cpp`.
- `top_gl` integral **não compilou neste ambiente** porque faltam `GL/gl.h` e dependências OpenGL. Os arquivos C++ modificados compilaram; validar renderização e interações no Ubuntu.

## Limites conhecidos / 05-AX

- `Play::loadItems`, `loadHeals` e `loadSkillBooks` ainda vivem em `top/Play.cpp` e poderão ser migrados para parser/catálogo independentes em etapas, sem duplicar o estado do jogo.
- Comparar callbacks `GiveRandomItems`, `Ticket`, `NewbieChest`, livros de profissões e consumíveis especiais com o GameServer e Lua históricos. Implementar apenas casos com condições e resultados comprovados, em módulos `item/`.
- Revisar **percentuais de cura, cooldowns, scripts com efeitos de estado e objetivos de missão**: não consumir nem premiar um item sem semântica verificada.
- Depois de ampliar o módulo de itens, revisar stats, e apenas depois skills e animações conforme roteiro acordado.

## Testes no Ubuntu

**Backup obrigatório de `Client/user/` antes de testar.** Substituir somente `sources/`.

```bash
cmake -S sources -B build -DTOP_BUILD_UI_TESTS=ON
cmake --build build -j4
ctest --test-dir build --output-on-failure
./build/top_gl --check
./build/top_gl
```

Testar inventário: clicar em consumível conhecido, clicar em item com callback não implementado (não deve sumir), equipar espada multi-slot no slot 6 e 9 via arraste, salvar e carregar, vender, descartar, guardar na bolsa temporária e continuar missões com `TE_GETITEM`. Não alterar `Client/user/` manualmente.
