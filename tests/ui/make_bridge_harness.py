import sys
from pathlib import Path

page = Path(sys.argv[1]).read_text(encoding="utf-8")
out = Path(sys.argv[2])

fake = r'''
window.__LOG = [];
window.__JUCE__ = { backend: {
  emitEvent: (id, a) => { window.__LOG.push(a); },
  addEventListener: (id, cb) => { window.__CB = cb; } } };
'''

test = r'''
(function () {
  const results = [];
  const check = (name, cond, info) => results.push((cond ? "PASS " : "FAIL ") + name + (cond ? "" : "  -> " + JSON.stringify(info)));
  const defaults = () => {
    const v = {};
    for (let n = 1; n <= 24; n++) {
      const p = "b" + n + "_";
      Object.assign(v, { [p+"used"]:0, [p+"on"]:1, [p+"type"]:0, [p+"freq"]:1000, [p+"gain"]:0, [p+"q"]:1, [p+"slope"]:1, [p+"place"]:0,
        [p+"dynRange"]:0, [p+"thresh"]:-30, [p+"threshAuto"]:1, [p+"ratio"]:4, [p+"attack"]:10, [p+"attackAuto"]:1,
        [p+"release"]:200, [p+"releaseAuto"]:1, [p+"dynSrc"]:0 });
    }
    Object.assign(v, { out_gain: 0, bypass: 0, phase_invert: 0 });
    for (let m = 1; m <= 8; m++) v["macro" + m] = 0;
    return v;
  };
  const log = () => window.__LOG;
  const clear = () => { window.__LOG.length = 0; };
  const sets = () => log().filter(a => a[0] === "set").map(a => a[1] + "=" + a[2]);
  const has = (id, v) => log().some(a => a[0] === "set" && a[1] === id && (v === undefined || a[2] === v));

  check("la page annonce ready", log().length === 1 && log()[0][0] === "ready", log());
  clear();

  // Avant l'état du plugin : rien ne doit être poussé (on écraserait l'état sauvegardé).
  S.outGain = 5; refresh(true);
  check("rien n'est envoyé avant le snapshot", log().length === 0, log());
  S.outGain = 0;

  // 1. Le plugin envoie un état : bande 2 cloche +6 dB à 440 Hz, bande 5 coupe-bas Side 24 dB/oct, macro1 = 0
  const snap = defaults();
  Object.assign(snap, { b2_used:1, b2_freq:440, b2_gain:6, b2_q:2, b5_used:1, b5_type:3, b5_freq:90, b5_slope:3, b5_place:4, out_gain:-3, bypass:1 });
  window.__CB(["snapshot", snap, JSON.stringify({ v:1, range:6, spec:"post", fall:"lente", dbRange:60, tilt:3, presetName:"Bass · Dub sub", dirty:true,
    macros:[{ name:"Filtre dub", asg:[{ id:5, p:"freq", a:90, b:400 }] }] })]);
  check("snapshot : 2 bandes aux bonnes cases", S.bands.length === 2 && S.bands[0].id === 2 && S.bands[1].id === 5, S.bands.map(b => b.id));
  check("snapshot : réglages de la bande 2", S.bands[0].type === "bell" && S.bands[0].freq === 440 && S.bands[0].gain === 6 && S.bands[0].q === 2, S.bands[0]);
  check("snapshot : coupe-bas Side 24 dB/oct", S.bands[1].type === "lowcut" && S.bands[1].place === "side" && S.bands[1].slope === 24, S.bands[1]);
  check("snapshot : globaux", S.outGain === -3 && S.bypass === true, [S.outGain, S.bypass]);
  check("snapshot : état d'interface", S.range === 6 && S.spec === "post" && S.fall === "lente" && S.dbRange === 60 && S.presetName === "Bass · Dub sub" && S.macros[0].name === "Filtre dub" && S.macros[0].asg.length === 1, [S.range, S.spec, S.fall, S.presetName, S.macros[0]]);
  check("snapshot : aucun écho vers le plugin", log().length === 0, log());

  // 2. L'utilisateur crée une bande : elle prend la plus petite case libre (1)
  addBand(2000, 4);
  check("nouvelle bande dans la case 1", S.bands[0].id === 1 && S.sel === 1, S.bands.map(b => b.id));
  check("création : used, freq, gain envoyés en begin/set/end", has("b1_used", 1) && has("b1_freq", 2000) && has("b1_gain", 4)
        && log().filter(a => a[0] === "begin").length === log().filter(a => a[0] === "end").length, sets());
  check("création : aucune autre bande touchée", !sets().some(s => /^b(2|5)_/.test(s)), sets());
  clear();

  // 3. Geste continu : bouton enfoncé, deux déplacements, relâchement
  document.dispatchEvent(new PointerEvent("pointerdown", { bubbles: true }));
  S.bands[0].freq = 2100; refresh(true);
  S.bands[0].freq = 2200; refresh(true);
  check("geste : un seul begin, deux set, pas encore de end", log().filter(a => a[0] === "begin" && a[1] === "b1_freq").length === 1
        && log().filter(a => a[0] === "set" && a[1] === "b1_freq").length === 2 && !log().some(a => a[0] === "end"), log());
  window.dispatchEvent(new PointerEvent("pointerup"));
  check("geste : end envoyé au relâchement", log().filter(a => a[0] === "end" && a[1] === "b1_freq").length === 1, log());
  clear();

  // 4. L'hôte change des valeurs (automation) : l'interface suit, sans renvoyer d'écho
  window.__CB(["params", [["b1_gain", 7.5], ["out_gain", 2]]]);
  check("automation : la bande 1 suit", S.bands[0].gain === 7.5 && S.outGain === 2, [S.bands[0].gain, S.outGain]);
  check("automation : aucun écho", log().length === 0, log());

  // 5. L'hôte crée/supprime une bande
  window.__CB(["params", [["b9_used", 1], ["b9_freq", 5000]]]);
  check("l'hôte crée la bande 9", S.bands.some(b => b.id === 9 && b.freq === 5000), S.bands.map(b => b.id));
  window.__CB(["params", [["b9_used", 0]]]);
  check("l'hôte supprime la bande 9", !S.bands.some(b => b.id === 9), S.bands.map(b => b.id));
  check("aucun écho", log().length === 0, log());

  // 6. Suppression par l'utilisateur
  removeBand(1);
  check("suppression : b1_used = 0", has("b1_used", 0) && S.bands.every(b => b.id !== 1), sets());
  clear();

  // 7. Macro automatisée par l'hôte : le coupe-bas de la bande 5 bouge de 90 à 400 Hz
  window.__CB(["params", [["macro1", 0.5]]]);
  const expected = 90 * Math.pow(400 / 90, 0.5);
  check("macro : la fréquence de la bande 5 suit", Math.abs(S.bands.find(b => b.id === 5).freq - expected) < 1e-9, S.bands.find(b => b.id === 5).freq);
  check("macro : la nouvelle fréquence est envoyée au plugin", sets().some(s => s.startsWith("b5_freq=")), sets());
  clear();

  // 8. Réglages de dynamique : seuil manuel puis retour en Auto
  const b2 = S.bands.find(b => b.id === 2);
  b2.dyn = true; b2.range = -6; b2.thresh = -20; b2.ratio = 6; b2.attack = 5;
  refresh(true);
  check("dynamique : range, seuil, ratio, attaque", has("b2_dynRange", -6) && has("b2_thresh", -20) && has("b2_threshAuto", 0)
        && has("b2_ratio", 6) && has("b2_attack", 5) && has("b2_attackAuto", 0), sets());
  clear();
  b2.thresh = null; refresh(true);
  check("dynamique : Auto n'envoie que le drapeau", has("b2_threshAuto", 1) && !sets().some(s => s.startsWith("b2_thresh=")), sets());
  clear();

  // 9. Preset : cases 1 à n, les autres libérées
  applyPreset("Kick", 0);
  check("preset : 5 bandes en cases 1 à 5", S.bands.length === 5 && S.bands.map(b => b.id).join() === "1,2,3,4,5", S.bands.map(b => b.id));
  check("preset : la case 9 n'est pas touchée (déjà libre)", !sets().some(s => s.startsWith("b9_")), sets());
  clear();
  applyPreset("Drums", 3); // 4 bandes : la case 5 doit être libérée
  check("preset : la case 5 est libérée", has("b5_used", 0), sets());
  clear();

  // 10. Globaux
  document.getElementById("bypass").click();
  check("bypass envoyé", has("bypass"), sets());
  clear();

  // 11. État d'interface persisté
  setRange(3); persistUi();
  const ui = log().find(a => a[0] === "ui");
  check("l'état d'interface part vers le plugin", ui && JSON.parse(ui[1]).range === 3 && JSON.parse(ui[1]).macros.length === 8, ui);

  const pre = document.createElement("pre");
  pre.id = "bridge-results";
  pre.textContent = results.join("\n") + "\n\n" + results.filter(r => r.startsWith("FAIL")).length + " échec(s) sur " + results.length;
  document.body.innerHTML = ""; document.body.appendChild(pre);
})();
'''

i = page.index("<script>") + len("<script>")
j = page.rindex("</script>")
page = page[:i] + "\n" + fake + page[i:j] + "\nwindow.addEventListener('load', () => setTimeout(() => {" + test + "}, 50));\n" + page[j:]
out.write_text(page, encoding="utf-8")
print("ok")
