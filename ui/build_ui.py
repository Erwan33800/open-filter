# Assemble ui/openfilter_ui.html à partir de ui/src/ et du socle d'interface d'open-core
# (CSS commun, police Manrope, pont générique), puis app.js, bridge.js, init.js.
#
# open-core est cherché dans la variable d'environnement OPEN_CORE_DIR, sinon dans le dossier open-core voisin.
import os
import sys
from pathlib import Path

root = Path(__file__).parent


def find_core():
    candidates = [os.environ.get("OPEN_CORE_DIR"), root.parent.parent / "open-core"]
    for c in candidates:
        if c and (Path(c) / "ui" / "assemble_ui.py").exists():
            return Path(c)
    sys.exit("open-core introuvable : définis OPEN_CORE_DIR ou place le dossier open-core à côté de open-filter.")


sys.path.insert(0, str(find_core() / "ui"))
from assemble_ui import assemble  # noqa: E402

assemble(root, slug="openfilter", scripts=["app.js", "bridge.js", "init.js"])
