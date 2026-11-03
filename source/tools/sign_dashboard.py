from pathlib import Path
import importlib.util
root=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('sign',root/'pico/tools/habibi_sign.py');sign=importlib.util.module_from_spec(spec);spec.loader.exec_module(sign)
data=sign.sign_xbe(sign.populate_empty_hashes((root/'Dashboard/bin/default.xbe').read_bytes()));assert sign.verify_habibi(data)
(root/'pico/assets/ENDGAME/payload.xbe').write_bytes(data)
print('Signed dashboard:',len(data),'bytes')
