/* Démarrage : bandes de démonstration (navigateur seulement), première image, pont avec le plugin. */
/* bandes de départ : coupe-bas Side à 120 Hz (cas type du cahier des charges) et une cloche dynamique sur la basse */
if(!JUCE_MODE){
  S.bands.push({id:1,type:"lowcut",freq:120,gain:0,q:0.71,slope:24,place:"side",on:true,dyn:false,range:0});
  S.bands.push({id:2,type:"bell",freq:80,gain:0,q:2,slope:12,place:"st",on:true,dyn:true,range:-8});
  S.bands.push({id:3,type:"highshelf",freq:9000,gain:2.5,q:0.71,slope:12,place:"st",on:true,dyn:false,range:0});
  S.macros[0]={name:"Filtre dub",v:0,asg:[{id:1,p:"freq",a:120,b:900},{id:1,p:"q",a:0.71,b:2.5}]}; // macro de démonstration
  S.sel=2; // la cloche dynamique est sélectionnée au départ pour montrer le panneau
}
resize(); refresh(); requestAnimationFrame(loop);
/* aide à la vérification : #presets, #macros, #preset=Bass:2 (catégorie:indice), #macroValue=50 */
(function(){
  const h=decodeURIComponent(location.hash.slice(1)); if(!h) return;
  const m=h.match(/preset=(\w+):(\d+)/); if(m) applyPreset(m[1],+m[2]);
  const sm=h.match(/selidx=(\d+)/); if(sm&&S.bands[+sm[1]]){ S.sel=S.bands[+sm[1]].id; refresh(); }
  if(/macros/.test(h)){ setView("macros"); const mv=h.match(/macroValue=(\d+)/); if(mv){ S.macros.forEach(x=>{ if(x.asg.length){ x.v=+mv[1]/100; applyMacro(x); } }); renderMacros(); } }
  if(/presets/.test(h)) togglePresets();
  if(/popan/.test(h)) togglePop("pAn",$("anBtn"));
  if(/pophelp/.test(h)) togglePop("pHelp",$("helpBtn"));
})();
bridgeStart();
