"""Test du pont interface <-> plugin, sans compiler le plugin.

Charge ui/openfilter_ui.html dans Chrome (sans interface) avec un faux window.__JUCE__ qui joue le rôle du plugin :
il envoie un état, des changements de l'hôte, et enregistre tout ce que l'interface lui renvoie
(voir make_bridge_harness.py pour la liste des vérifications).

    python tests/ui/bridge_test.py            # code de sortie 0 si tout passe
Chrome est cherché dans les emplacements habituels (Windows, Linux) ou via la variable CHROME.
"""
import html
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[2]


def find_chrome():
    if os.environ.get("CHROME"):
        return os.environ["CHROME"]
    for name in ("google-chrome", "google-chrome-stable", "chromium", "chromium-browser", "chrome"):
        found = shutil.which(name)
        if found:
            return found
    for path in (r"C:\Program Files\Google\Chrome\Application\chrome.exe",
                 r"C:\Program Files (x86)\Google\Chrome\Application\chrome.exe",
                 r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe"):
        if Path(path).exists():
            return path
    return None


def main():
    chrome = find_chrome()
    if chrome is None:
        print("Chrome introuvable : indique son chemin dans la variable CHROME.")
        return 2

    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        harness = tmp / "bridge_test.html"
        subprocess.run([sys.executable, "-I", str(Path(__file__).with_name("make_bridge_harness.py")),
                        str(root / "ui" / "openfilter_ui.html"), str(harness)], check=True, stdout=subprocess.DEVNULL)

        result = subprocess.run([chrome, "--headless", "--disable-gpu", "--no-sandbox", "--virtual-time-budget=4000",
                                 f"--user-data-dir={tmp / 'profile'}", "--dump-dom", harness.as_uri()],
                                capture_output=True, text=True, timeout=120)

    match = re.search(r'<pre id="bridge-results">(.*?)</pre>', result.stdout, re.S)
    if not match:
        print("Aucun résultat : la page de test ne s'est pas exécutée jusqu'au bout.")
        print(result.stdout[:500])
        return 2

    text = html.unescape(match.group(1))
    print(text)
    return 1 if "FAIL" in text else 0


if __name__ == "__main__":
    sys.exit(main())
