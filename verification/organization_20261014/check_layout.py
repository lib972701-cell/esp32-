"""Check the real relocation result, protected sources and local documentation links."""
from pathlib import Path
import hashlib
import json
import re

ROOT = Path(__file__).resolve().parents[2]
EVIDENCE = Path(__file__).parent
def read_json(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))
def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest().upper()

moves = read_json(EVIDENCE / 'moves.json')
sources = []
for before in read_json(EVIDENCE / 'sources-before.json'):
    current = before['Path']
    for old, new in moves.items():
        if current == old or current.startswith(old + '/'):
            current = new + current[len(old):]
    path = ROOT / current
    assert path.is_file(), f'Missing source: {current}'
    changed = digest(path) != before['Hash']
    assert not changed or current.startswith('components/CloudAI/'), f'Unexpected logic change: {current}'
    sources.append(dict(original=before['Path'], current=current, changed=changed))

protected = read_json(ROOT / 'verification/cloud_ai_20261013/protected-source.before.json')
assert all(digest(Path(item['Path'])) == item['Hash'] for item in protected)

broken = []
links = 0
for doc in [ROOT / 'README.md', *(ROOT / 'docs').rglob('*.md')]:
    for target in re.findall(r'!?\[[^\]\n]*\]\(([^\)\n]+)\)', doc.read_text(encoding='utf-8-sig')):
        if re.match(r'(?:[a-zA-Z]+:|#|/)', target):
            continue
        target = target.partition('#')[0]
        links += 1
        if target and not (doc.parent / target).exists():
            broken.append(f'{doc.relative_to(ROOT)} -> {target}')
assert not broken, '\n'.join(broken)

for build in ['build', 'build-cloud']:
    description = read_json(ROOT / build / 'project_description.json')
    forbidden = [name for name in description['build_components']
                 if re.search(r'lvgl|camera|esp_lcd|image_player|emote|esp_driver_cam', name, re.I)]
    assert not forbidden, f'{build}: {forbidden}'

result = dict(source_count=len(sources), sources=sources, protected_unchanged=len(protected),
              local_links_checked=links, broken_links=broken,
              root_markdown=[p.name for p in ROOT.glob('*.md')],
              visual_components_in_both_profiles=[])
(EVIDENCE / 'layout-check.json').write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
print(json.dumps({k:v for k,v in result.items() if k != 'sources'}, ensure_ascii=False))
