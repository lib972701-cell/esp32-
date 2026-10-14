"""One-off relocation of local Markdown links; firmware and evidence are untouched."""
from pathlib import Path
import json
import os
import re

ROOT = Path(__file__).resolve().parents[2]
moves = json.loads((Path(__file__).parent / 'moves.json').read_text(encoding='utf-8-sig'))
moves['docs/系统框图与工作原理.md'] = 'docs/history/20261012-mail/系统框图与工作原理.md'
origins = {value: key for key, value in moves.items()}
origins['docs/history/20261014-before-organization/根目录说明.md'] = 'README.md'
origins['docs/history/20261014-before-organization/工程分层.md'] = 'README_工程分层.md'
origins['docs/history/20261014-before-organization/独立上电邮件.md'] = 'README_独立上电邮件.md'
by_name = {Path(key).name: value for key, value in moves.items() if key.endswith('.md')}
link = re.compile(r'(!?\[[^\]\n]*\]\()([^\)\n]+)(\))')

for doc in [ROOT / 'README.md', *(ROOT / 'docs').rglob('*.md')]:
    relative = doc.relative_to(ROOT).as_posix()
    old_parent = Path(origins.get(relative, relative)).parent
    text = doc.read_text(encoding='utf-8-sig')
    def relocate(match):
        target = match[2]
        if re.match(r'(?:[a-zA-Z]+:|#|/)', target):
            return match[0]
        filename, marker, anchor = target.partition('#')
        if (doc.parent / filename).exists():
            return match[0]
        old_target = os.path.normpath(str(old_parent / filename)).replace('\\', '/')
        new_target = moves.get(old_target, by_name.get(Path(filename).name))
        if not new_target and (ROOT / old_target).exists():
            new_target = old_target
        if not new_target:
            return match[0]
        destination = os.path.relpath(ROOT / new_target, doc.parent).replace('\\', '/')
        return match[1] + destination + (marker + anchor if marker else '') + match[3]
    text = link.sub(relocate, text)
    # Old plain filename mentions become the current filename; navigation lives in docs/README.
    for old_name, new_path in by_name.items():
        if old_name.startswith('README_'):
            text = text.replace(old_name, Path(new_path).name)
    text = text.replace('scripts/refactor_layers.py', 'scripts/history/refactor_layers.py')
    text = text.replace('python docs/generate_system_diagrams.py', 'python scripts/docs/generate_system_diagrams.py')
    doc.write_text(text, encoding='utf-8')

for doc in (ROOT / 'docs' / 'history').rglob('*.md'):
    text = doc.read_text(encoding='utf-8')
    if '历史资料，保留用于追溯' not in text:
        text = '> 历史资料，保留用于追溯。当前行为与目录以 docs/README.md 和现有源码为准。\n\n' + text
        doc.write_text(text, encoding='utf-8')
