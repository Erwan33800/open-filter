# Assemble ui/openfilter_ui.html à partir de ui/src/ (en-tête HTML + CSS, puis app.js).
# La police Manrope (SIL OFL, sous-ensemble latin) est intégrée en base64 : le WebView est hors ligne.
import base64
from pathlib import Path

root = Path(__file__).parent
src = root / "src"
head = (src / "plugin-head.html").read_text(encoding="utf-8")
font = base64.b64encode((src / "manrope-latin.woff2").read_bytes()).decode("ascii")
assert "__MANROPE__" in head, "placeholder __MANROPE__ introuvable dans plugin-head.html"
head = head.replace("__MANROPE__", font)
html = head + (src / "app.js").read_text(encoding="utf-8") + "\n</script>\n</body>\n</html>\n"
(root / "openfilter_ui.html").write_text(html, encoding="utf-8", newline="\n")
print("ui/openfilter_ui.html :", len(html), "octets")
