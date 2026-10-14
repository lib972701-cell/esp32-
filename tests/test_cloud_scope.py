"""保持云端接入范围：应用代码和新增组件不得引入显示、摄像头或图像栈。"""
from pathlib import Path
import os
import re
import unittest

PROJECT=Path(__file__).resolve().parents[1]
SOURCE=Path(os.environ.get('CLOUD_SCOPE_SOURCE',PROJECT/'components/CloudAI'))

class CloudScopeTests(unittest.TestCase):
    def test_voice_code_has_no_visual_modules(self):
        forbidden=re.compile(r'camera|display|lvgl|lcd_|oled|font_awesome',re.I)
        hits=[]
        for file in SOURCE.rglob('*'):
            if file.suffix not in {'.c','.cc','.cpp','.h'}:continue
            if forbidden.search(file.name):hits.append(str(file.relative_to(SOURCE)))
            for line in file.read_text(encoding='utf-8-sig').splitlines():
                if line.strip().startswith('#include') and forbidden.search(line):
                    hits.append(str(file.relative_to(SOURCE))+': '+line)
        self.assertEqual(hits,[], 'Only cloud voice modules may be included')

    def test_only_audio_and_network_vendor_components_remain(self):
        actual={d.name for d in (PROJECT/'managed_components').iterdir() if d.is_dir()}
        self.assertEqual(actual,{'78__esp-ml307','78__esp-wifi-connect','78__esp-opus',
                                 '78__esp-opus-encoder','espressif__esp_codec_dev'})

if __name__=='__main__':unittest.main()
