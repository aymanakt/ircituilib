import subprocess, tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
GEN=ROOT/'tools/gen_ui.py'
GOOD='''app:\n  id: root\n  type: radio_group\n  values: [a,b]\n  default_selection: 1\n'''
BAD='''app:\n  id: root\n  type: radio_group\n  values: [a,b]\n  default_selection: 9\n'''
with tempfile.TemporaryDirectory() as td:
    td=Path(td); (td/'good.yaml').write_text(GOOD); (td/'bad.yaml').write_text(BAD)
    r=subprocess.run(['python3',str(GEN),str(td/'good.yaml'),str(td/'good.c')],capture_output=True,text=True)
    assert r.returncode==0, r.stderr
    r=subprocess.run(['python3',str(GEN),str(td/'bad.yaml'),str(td/'bad.c')],capture_output=True,text=True)
    assert r.returncode!=0 and 'out of range' in (r.stderr+r.stdout)
print('generator ok')
