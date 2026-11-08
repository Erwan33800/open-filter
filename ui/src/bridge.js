/* ---------- pont avec le plugin : partie propre à Open Filter ----------
   Le mécanisme (gestes d'automation, snapshot, état d'interface) est dans open-core/ui/bridge-core.js, qui décrit le
   protocole. Ici : le nom de l'événement et la traduction entre le modèle de l'interface (bandes, macros...) et les
   paramètres du plugin (b{n}_freq, out_gain, macro1...). */
const BRIDGE_EVENT = "openfilter"; // identique à Config::eventId dans Source/PluginEditor.cpp
const TYPE_KEYS = ["bell", "lowshelf", "highshelf", "lowcut", "highcut", "notch", "bandpass"]; // index du paramètre « type »
const TYPE_IDX = { bell: 0, lowshelf: 1, highshelf: 2, lowcut: 3, highcut: 4, notch: 5, bandpass: 6 };
const PLACE_IDX = { st: 0, mid: 3, side: 4 };               // index du paramètre « place » (1 et 2 = gauche et droite, réservés)
const PLACE_KEYS = { 0: "st", 1: "st", 2: "st", 3: "mid", 4: "side" };

/* Modèle de l'interface -> valeurs de paramètres. Les réglages « Auto » ne poussent pas leur valeur manuelle. */
function bandToParams(b, out) {
  const p = "b" + b.id + "_", t = TYPES[b.type];
  out[p + "used"] = 1;
  out[p + "on"] = b.on ? 1 : 0;
  out[p + "type"] = TYPE_IDX[b.type];
  out[p + "freq"] = b.freq;
  out[p + "gain"] = t.gain ? b.gain : 0;
  out[p + "q"] = b.q;
  out[p + "slope"] = Math.round(b.slope / 6) - 1;
  out[p + "place"] = PLACE_IDX[b.place] || 0;
  out[p + "dynRange"] = b.dyn && t.dyn ? b.range : 0;     // 0 = bande statique
  if (b.ratio !== undefined) {                              // champs de dynamique, créés à l'ouverture du panneau
    out[p + "ratio"] = b.ratio;
    out[p + "dynSrc"] = b.src === "sc" ? 1 : 0;
    out[p + "threshAuto"] = b.thresh == null ? 1 : 0;
    if (b.thresh != null) out[p + "thresh"] = b.thresh;
    out[p + "attackAuto"] = b.attack == null ? 1 : 0;
    if (b.attack != null) out[p + "attack"] = b.attack;
    out[p + "releaseAuto"] = b.release == null ? 1 : 0;
    if (b.release != null) out[p + "release"] = b.release;
  }
}

function modelToParams() {
  const out = {};
  for (let n = 1; n <= 24; n++) {
    const b = S.bands.find(x => x.id === n);
    if (b) bandToParams(b, out); else out["b" + n + "_used"] = 0;
  }
  out.out_gain = S.outGain;
  out.bypass = S.bypass ? 1 : 0;
  out.phase_invert = S.phase ? 1 : 0;
  S.macros.forEach((m, i) => { out["macro" + (i + 1)] = m.v; });
  return out;
}

/* Valeurs de paramètres -> modèle de l'interface (état restauré, automation de l'hôte). */
function bandFromKnown(n, old) {
  const g = k => known["b" + n + "_" + k];
  if (!(g("used") >= 0.5)) return null;

  const type = TYPE_KEYS[Math.max(0, Math.min(6, Math.round(g("type"))))]; // types réservés (Tilt, All Pass) : affichés en cloche
  const range = g("dynRange");
  const b = old || {};
  Object.assign(b, {
    id: n, type, freq: g("freq"), gain: g("gain"), q: g("q"),
    slope: Math.min(96, (Math.round(g("slope")) + 1) * 6),   // « Brickwall » (réservé) : affiché 96 dB/oct
    place: PLACE_KEYS[Math.round(g("place"))] || "st",
    on: g("on") >= 0.5,
    dyn: range !== 0 && TYPES[type].dyn === true,
    range: range !== 0 ? range : (old && old.range ? old.range : -6),
    ratio: g("ratio"), src: g("dynSrc") >= 0.5 ? "sc" : "in",
    thresh: g("threshAuto") >= 0.5 ? null : g("thresh"),
    attack: g("attackAuto") >= 0.5 ? null : g("attack"),
    release: g("releaseAuto") >= 0.5 ? null : g("release"),
    listen: old ? !!old.listen : false
  });
  return b;
}

function applyParams(values) {
  const macroChanged = [];
  for (const id in values) {
    const v = values[id];
    if (known[id] === v) continue;
    known[id] = v;
    const m = /^macro(\d)$/.exec(id);
    if (m) macroChanged.push(+m[1] - 1);
  }

  // Les bandes sont reconstruites depuis les valeurs connues, en gardant les objets existants (champs propres à l'interface).
  const next = [];
  for (let n = 1; n <= 24; n++) {
    const b = bandFromKnown(n, S.bands.find(x => x.id === n));
    if (b) next.push(b);
  }
  S.bands = next;
  if (!S.bands.some(b => b.id === S.sel)) S.sel = -1;

  if ("out_gain" in values) S.outGain = known.out_gain;
  if ("bypass" in values) S.bypass = known.bypass >= 0.5;
  if ("phase_invert" in values) S.phase = known.phase_invert >= 0.5;

  macroChanged.forEach(i => {   // une macro automatisée déplace les réglages qui lui sont assignés
    S.macros[i].v = known["macro" + (i + 1)];
    applyMacro(S.macros[i]);
  });

  syncGlobalsUI();
  refresh();
  if (S.view === "macros") renderMacros();
}

/* Réglages d'interface sauvegardés dans le set (nœud séparé de l'état) : échelle, analyseur, nom du preset, macros. */
function uiStateJson() {
  return JSON.stringify({
    v: 1, range: S.range, spec: S.spec, fall: S.fall, dbRange: S.dbRange, tilt: S.tilt,
    presetName: S.presetName, dirty: S.dirty,
    macros: S.macros.map(m => ({ name: m.name, asg: m.asg }))
  });
}

function applyUiState(json) {
  if (!json) return;
  let o;
  try { o = JSON.parse(json); } catch (_) { return; }
  if (!o || o.v !== 1) return;

  if ([3, 6, 12, 30].includes(o.range)) S.range = o.range;
  if (["off", "pre", "post", "prepost"].includes(o.spec)) S.spec = o.spec;
  if (["lente", "moy", "rapide"].includes(o.fall)) S.fall = o.fall;
  if ([60, 90, 120].includes(o.dbRange)) S.dbRange = o.dbRange;
  if (typeof o.tilt === "number") S.tilt = Math.max(0, Math.min(6, o.tilt));
  if (typeof o.presetName === "string") S.presetName = o.presetName;
  S.dirty = !!o.dirty;

  if (Array.isArray(o.macros))
    o.macros.slice(0, 8).forEach((m, i) => {
      if (m && typeof m.name === "string" && Array.isArray(m.asg)) {
        S.macros[i].name = m.name;
        S.macros[i].asg = m.asg.filter(a => a && Number.isInteger(a.id) && ["freq", "gain", "q", "range"].includes(a.p)
                                            && Number.isFinite(a.a) && Number.isFinite(a.b));
      }
    });
  syncBottomControls();
}
