#!/usr/bin/env python3
"""05-AW: Audita TODOS os registros historicos (sem executar Lua).
Uso: python3 sources/Tools/item_action_audit.py Client/scripts /tmp/item-audit.csv /tmp/item-audit.md
Usa apenas a biblioteca padrao. Os dados do cliente NAO sao incorporados ao ZIP sources/.
"""
import csv
import re
import sys
from collections import Counter, defaultdict
from pathlib import Path


def read_latin(path):
    return Path(path).read_bytes().decode('latin1')


def audit(root, csv_output, md_output):
    table = Path(root)/'table/iteminfo.txt'
    var_file = Path(root)/'offline/calculate/variable.lua'
    rows = read_latin(table).splitlines()
    lua = read_latin(var_file)
    restore = {int(n) for n in re.findall(r'Restore\.Life\[\s*(\d+)\s*\]', lua)}
    book_map = {int(n) for n in re.findall(r'Server\.Player\.Skill\[\s*(\d+)\s*\]\s*=\s*\{\s*Skill\s*=', lua)}
    records = []
    by_status = Counter()
    by_callback = Counter()
    by_type = Counter()
    unresolved = defaultdict(list)
    for row in rows:
        cols = row.split('\t')
        if len(cols) < 87 or not cols[0].strip().isdigit():
            continue
        item_id = int(cols[0])
        cb = cols[86].strip()
        typ = cols[10].strip()
        slots = [int(p) for p in re.findall(r'-?\d+', cols[28])]
        equip = any(slot >= 0 for slot in slots)
        # Nao assumir que funcao Lua desconhecida possui efeito implementado.
        if cb == 'ItemMedKit':
            status = 'heal_amount_supported' if item_id in restore else 'heal_no_formula'
        elif cb == 'InventorySlot' and 3088 <= item_id <= 3093:
            status = 'bag_upgrade_supported'
        elif cb == 'AddPlayerSkill':
            status = 'skill_book_mapped' if item_id in book_map else 'skill_book_no_mapping'
        elif cb and cb != '0':
            status = 'lua_callback_not_implemented'
        elif equip:
            status = 'equipable_data'
        else:
            status = 'non_click_item_or_other_system'
        name = cols[1].strip().replace('\x00', '')
        records.append((item_id, name, typ, cb or '0', status, cols[28].strip()))
        by_status[status] += 1
        by_callback[cb or '0'] += 1
        by_type[typ] += 1
        if len(unresolved[cb]) < 3 and status in ('lua_callback_not_implemented','heal_no_formula','skill_book_no_mapping'):
            unresolved[cb].append(f'{item_id}: {name}')
    records.sort(key=lambda r: r[0])
    with Path(csv_output).open('w', encoding='utf-8', newline='') as f:
        writer = csv.writer(f)
        writer.writerow(['id','name_latin1','type','iteminfo_callback','offline_status','equip_slots'])
        writer.writerows(records)
    body = ['# Auditoria historica de itens — 05-AW','',
      f'Base: `{table.as_posix()}`; {len(records)} registros validos. A coluna de status e conservadora, nao executa Lua.',
      '', '## Status por categoria', '', '| Status | Registros |', '|---|---:|']
    for label,count in by_status.most_common(): body.append(f'| `{label}` | {count} |')
    body += ['', '## Tipos mais comuns', '', '| Tipo de iteminfo | Registros |', '|---|---:|']
    for label,count in by_type.most_common(15): body.append(f'| `{label}` | {count} |')
    body += ['', '## Callbacks mais comuns', '', '| Funcao iteminfo coluna 86 | Registros | Exemplos pendentes |', '|---|---:|---|']
    for cb,count in by_callback.most_common(32):
        body.append(f'| `{cb}` | {count} | {", ".join(unresolved.get(cb,[])).replace("|","/")} |')
    body += ['', '## Cuidados', '',
      '- `equipable_data` indica apenas metadados de slot; nao significa que todos os atributos foram validados.',
      '- `non_click_item_or_other_system` inclui itens de missao, materiais, crafting e outros sistemas. Nao e uma falha por si so.',
      '- `lua_callback_not_implemented` significa somente que nao ha equivalente deste callback em clique de inventario; verificar uso por outros servicos separadamente.',
      '- `skill_book_mapped` confirma apenas o mapeamento historico. Aprendizado exige classe, nivel, prerequisitos e TP.',
      '- `heal_amount_supported` cobre restauracao HP/SP por valor absoluto. Percentuais, estados e cooldown nao foram reproduzidos.',
      '- Texto original de iteminfo usa encoding legado; CSV conserva bytes decodificados como latin1 para nao descartar registros.',
      '- Ferramenta nao modifica saves, nao converte recursos e nao executa scripts Lua.', '']
    Path(md_output).write_text('\n'.join(body),encoding='utf-8')
    return len(records), by_status

if __name__=='__main__':
    if len(sys.argv)!=4:
        raise SystemExit('Usage: item_action_audit.py Client/scripts report.csv report.md')
    n,c=audit(*sys.argv[1:]); print('Audited',n,'items:',dict(c))
