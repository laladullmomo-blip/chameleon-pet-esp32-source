import test from 'node:test';
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {createState,advanceState,normalizeState,getGrowthInfo,GROWTH_TIMES} from '../preview/model.mjs';
import {createRenderer} from '../preview/renderer.mjs';
import {ACTIVE_ACTIONS,createStagePreview,validateStageCatalog,validateStageManifest} from '../preview/growth.mjs';

const teen = JSON.parse(await readFile(new URL('../preview/assets/frames/manifest.json',import.meta.url),'utf8'));
const catalog = {
  version:1,default_stage:'teen',stages:[
    {id:'baby',label:'幼年',reference:'growth-concepts/baby-v2.png',manifest:'growth/baby/frames/manifest.json',render_scale:.84},
    {id:'teen',label:'少年',reference:'chameleon.png',manifest:'frames/manifest.json',render_scale:1},
    {id:'adult',label:'成年',reference:'growth-concepts/adult-v1.png',manifest:'growth/adult/frames/manifest.json',render_scale:1.06},
  ],
};
const copy = value => structuredClone(value);
function variant(id) {
  const manifest = copy(teen);
  delete manifest.actions.grip;
  for (const entry of Object.values(manifest.actions)) {
    entry.sheet = `growth/${id}/${entry.sheet}`;
    entry.frames = entry.frames.map(path => `growth/${id}/${path}`);
  }
  // Deliberately distinct anchors prove that no teen anchor is reused.
  const mouth = id === 'baby' ? {x:.67,y:.62} : {x:.88,y:.54};
  for (const index of [2,3,4]) manifest.actions.eat.mouths[index] = {...mouth};
  return manifest;
}

function canvasMock() {
  const calls = [];
  const context = new Proxy({drawImage(...args) { calls.push(args); }},{get(target,key) { return key in target ? target[key] : () => {}; }});
  return {
    width:466,height:466,calls,getContext:()=>context,
    ownerDocument:{createElement:()=>canvasMock()},
    toDataURL:()=>'data:image/png;base64,captured',
    toBlob:callback=>callback(new Blob(['captured'],{type:'image/png'})),
  };
}
function mockAssets(t,{missingManifest,missingImage,badDimensions,invalidManifest}={}) {
  const manifests = {baby:variant('baby'),teen:copy(teen),adult:variant('adult')};
  if (invalidManifest) invalidManifest(manifests);
  const json = new Map([['growth/stages.json',catalog],...catalog.stages.map(stage => [stage.manifest,manifests[stage.id]])]);
  const dimensions = new Map(['forest','clearing','night','meadow','meadow-night'].map(name=>[`${name}.png`,[128,128]]));
  for (const manifest of Object.values(manifests)) for (const entry of Object.values(manifest.actions)) {
    dimensions.set(entry.sheet,[entry.width * entry.count,entry.height]);
    entry.frames.forEach(path=>dimensions.set(path,[entry.width,entry.height]));
  }
  if (badDimensions) dimensions.set(badDimensions.path,badDimensions.size);
  const requested = [],originalFetch=globalThis.fetch,originalImage=globalThis.Image;
  const pathOf = url => String(url).split('/assets/')[1];
  globalThis.fetch = async url => {
    const path = pathOf(url);
    const exists = json.has(path) && path !== missingManifest;
    return {ok:exists,status:exists?200:404,json:async()=>copy(json.get(path))};
  };
  globalThis.Image = class {
    set src(url) {
      this.url = url;
      const path = pathOf(url);requested.push(path);
      queueMicrotask(()=>{
        if (!dimensions.has(path) || path === missingImage) {this.onerror();return;}
        [this.naturalWidth,this.naturalHeight] = dimensions.get(path);this.onload();
      });
    }
  };
  t.after(()=>{globalThis.fetch=originalFetch;globalThis.Image=originalImage;});
  return {manifests,requested};
}
function freeze(value) { if (value && typeof value==='object') {Object.values(value).forEach(freeze);Object.freeze(value);}return value; }

test('asset catalog retains the original reference default independently of real pet age',()=>{
  assert.deepEqual(validateStageCatalog(catalog).stages.map(stage=>stage.id),['baby','teen','adult']);
  const wrong=copy(catalog);wrong.stages[0].manifest='frames/manifest.json';
  assert.throws(()=>validateStageCatalog(wrong),/Wrong animation manifest for baby/);
  const duplicate=copy(catalog);duplicate.stages[2]=duplicate.stages[0];
  assert.throws(()=>validateStageCatalog(duplicate),/duplicate growth stage/);
  assert.throws(()=>validateStageCatalog({...catalog,default_stage:'baby'}),/teen as default/);
});

test('preview selection remains separate from a saved pet and is reset on reopening',()=>{
  const state=freeze(createState(new Date(2026,9,2,12).getTime()));
  const before=JSON.stringify(state),preview=createStagePreview(catalog);
  assert.equal(preview.id,'teen');
  preview.select('baby');preview.select('adult');
  assert.equal(preview.id,'adult');assert.equal(preview.current.render_scale,1.06);
  assert.equal(JSON.stringify(state),before);
  assert.equal(Object.hasOwn(state,'stage'),false);
  assert.equal(createStagePreview(catalog).id,'teen');
  assert.throws(()=>preview.select('dragon'),/Unknown growth preview stage/);
  assert.equal(preview.id,'adult','invalid selection must leave the chosen form intact');
});

test('all active actions retain their frame counts and removed motions stay disabled',()=>{
  const valid=validateStageManifest(teen,'teen');
  assert.deepEqual(new Set(valid.map(([id])=>id)),new Set(ACTIVE_ACTIONS));
  assert.equal(valid.reduce((sum,[,entry])=>sum+entry.count,0),82);
  const baby=variant('baby');baby.actions.grip=copy(teen.actions.grip);
  assert.throws(()=>validateStageManifest(baby,'baby'),/Unsupported animation action for baby: grip/);
  for (const id of ['jump','tilt']) {
    const invalid=variant('adult');invalid.actions[id]=invalid.actions.idle;
    assert.throws(()=>validateStageManifest(invalid,'adult'),/Unsupported animation action/);
  }
});

test('open-mouth poses, source dimensions and sleeping cadence are enforced before any image loads',()=>{
  for (const index of [2,3,4]) {
    const invalid=variant('baby');invalid.actions.eat.mouths[index]=null;
    assert.throws(()=>validateStageManifest(invalid,'baby'),/anchors at frames 2, 3 and 4/);
  }
  const closed=variant('baby');closed.actions.eat.mouths[2]={x:1.2,y:.5};
  assert.throws(()=>validateStageManifest(closed,'baby'),/mouth-pose frames/);
  const dimensions=variant('adult');dimensions.actions.walk.width=256;
  assert.throws(()=>validateStageManifest(dimensions,'adult'),/128 × 128/);
  const frames=variant('adult');frames.actions.eat.count=6;
  assert.throws(()=>validateStageManifest(frames,'adult'),/expected 8 frames/);
  const sleep=variant('baby');sleep.actions.sleep.fps=5;
  assert.throws(()=>validateStageManifest(sleep,'baby'),/gently breathing frames at 1 FPS/);
});

test('rounded support coordinates at the body edge are accepted while genuinely outside anchors fail',()=>{
  const manifest=variant('adult');
  manifest.actions.walk.foot_anchors[5]={x:.636719,y:.835938,pixel_x:81.5,pixel_y:107,body_bounds:[16,35,114,107]};
  assert.equal(validateStageManifest(manifest,'adult').length,14);
  manifest.actions.walk.foot_anchors[5].y=108/128;
  manifest.actions.walk.foot_anchors[5].pixel_y=108;
  assert.throws(()=>validateStageManifest(manifest,'adult'),/Invalid per-frame animation foot anchors: adult\/walk/);
});

test('saved manifests for all three stages load their complete action catalogs',async()=>{
  const actual=JSON.parse(await readFile(new URL('../preview/assets/growth/stages.json',import.meta.url),'utf8'));
  for(const stage of validateStageCatalog(actual).stages){
    const manifest=JSON.parse(await readFile(new URL(`../preview/assets/${stage.manifest}`,import.meta.url),'utf8'));
    const entries=validateStageManifest(manifest,stage.id);
    assert.equal(entries.length,14);
    assert.equal(entries.reduce((sum,[,entry])=>sum+entry.count,0),82);
  }
});

test('renderer selects independent stage sheets and calibrated mouths while retaining the pet state',async t=>{
  const {requested}=mockAssets(t),canvas=canvasMock(),renderer=createRenderer(canvas);
  const assets=await renderer.loadAssets();
  assert.equal(assets.totalFrames,82);assert.deepEqual(assets.stages.map(stage=>stage.totalFrames),[82,82,82]);
  assert.equal(requested.some(path=>path.includes('grip')),false);
  const state=freeze(createState(new Date(2026,9,2,12).getTime())),before=JSON.stringify(state);
  assert.equal(renderer.render({state,time:0}).stage,'baby');
  for (const [stage,scale,anchorX] of [['baby',.84,.67],['adult',1.06,.88],['teen',1,teen.actions.eat.mouths[2].x]]) {
    const info=renderer.render({state,stage,time:350,action:{type:'eat',start:0,duration:1000,item:'fly'}});
    assert.equal(info.stage,stage);assert.equal(info.frameIndex,2);
    assert.equal(info.cellSize,250*scale);
    assert.equal(info.mouth.x,info.x+info.cellSize*anchorX);
    assert.ok(info.tongue.extension>.8,'tongue must extend from an open-mouth frame');
    const draw=canvas.calls.filter(call=>call.length===9).at(-1);
    assert.ok(draw[0].url.endsWith(stage==='teen'?'assets/frames/eat.png':`assets/growth/${stage}/frames/eat.png`));
    assert.equal(draw[7],draw[8],'stage scale must preserve square pixels without stretching');
  }
  assert.equal(JSON.stringify(state),before);
  assert.equal(renderer.captureDataURL(),'data:image/png;base64,captured');
  assert.equal((await renderer.capture()).type,'image/png');
  assert.throws(()=>renderer.render({state,stage:'dragon'}),/Growth stage resources have not loaded/);
  assert.throws(()=>renderer.render({state,stage:'teen',action:{type:'grip',start:0},time:0}),/No animation resource for action: grip/);
});

test('normal and isolated growth render the saved form across both milestones without a manual selection',async t=>{
  mockAssets(t);const renderer=createRenderer(canvasMock());await renderer.loadAssets();
  const now=new Date(2026,9,2,12).getTime();
  for(const speed of ['normal','fast']){
    let state=createState(now,speed);
    assert.equal(renderer.render({state,time:0}).stage,'baby');
    for(const [milestone,expected] of [['teen','teen'],['adult','adult']]){
      state.growthMs=GROWTH_TIMES[speed][milestone]-1000;
      state=advanceState(state,1001,state.updatedAt+1001);
      assert.equal(getGrowthInfo(state).id,expected);
      assert.equal(renderer.render({state,time:0}).stage,expected);
      const saved=JSON.stringify(state),restored=normalizeState(JSON.parse(saved));
      assert.equal(restored.status,'loaded');
      assert.equal(renderer.render({state:restored.state,time:0}).stage,expected,'reopening must follow the saved growth time');
      renderer.render({state,stage:expected==='teen'?'adult':'baby',time:0});
      assert.equal(JSON.stringify(state),saved,'temporary shape and photo rendering cannot change the pet');
      assert.equal(renderer.render({state,time:0}).stage,expected,'omitting the override returns to the real age');
    }
  }
});

test('care pauses affect growth, while the renderer preserves age and real stage resources',async t=>{
  mockAssets(t);const renderer=createRenderer(canvasMock());await renderer.loadAssets();
  const now=new Date(2026,9,2,12).getTime();
  let state=createState(now);state.growthMs=GROWTH_TIMES.normal.teen-1;state.sick=true;
  state=advanceState(state,1000,now+1000);
  assert.equal(getGrowthInfo(state).paused,true);assert.equal(renderer.render({state,time:0}).stage,'baby');
  state.sick=false;state.sicknessCause='none';
  state=advanceState(state,1000,now+2000);
  assert.equal(renderer.render({state,time:0}).stage,'teen');
  assert.equal(renderer.render({state:{sleeping:true,scene:'forest'},time:0}).stage,'baby','old partial renderer inputs must safely begin as a baby');
});

test('every scene and widget uses the selected form and sleep keeps a fixed support point',async t=>{
  mockAssets(t);const renderer=createRenderer(canvasMock());await renderer.loadAssets();
  const sleeping={sleeping:true,scene:'forest'};
  for (const stage of ['baby','teen','adult']) {
    for (const scene of ['forest','clearing','night','meadow','meadow-night']) for (const mode of ['pet','clock','weather','charging']) {
      const info=renderer.render({state:sleeping,stage,time:0,mode,environment:{scene,phase:'night',weather:'rain'}});
      assert.equal(info.stage,stage);assert.equal(info.scene,scene);assert.equal(info.action,'sleep');
      const support=[];
      for (const time of [0,1000,2000,3000]) support.push(renderer.render({state:sleeping,stage,time,mode,environment:{scene,phase:'night',weather:'rain'}}));
      assert.deepEqual(support.map(frame=>frame.frameIndex),[0,1,2,3]);
      assert.equal(new Set(support.map(frame=>`${frame.x},${frame.y},${frame.cellSize}`)).size,1);
    }
  }
});

test('missing form manifest prevents readiness instead of silently falling back to teen',async t=>{
  mockAssets(t,{missingManifest:'growth/adult/frames/manifest.json'});
  const renderer=createRenderer(canvasMock());
  await assert.rejects(renderer.loadAssets(),/成年 animation manifest \(404\)/);
  assert.throws(()=>renderer.render({stage:'teen'}),/frames have not loaded/);
});

test('missing frame and wrong-sized frame or sheet each produce a visible loading failure',async t=>{
  await t.test('missing baby frame',async sub=>{
    const frame=variant('baby').actions.walk.frames[0];mockAssets(sub,{missingImage:frame});
    await assert.rejects(createRenderer(canvasMock()).loadAssets(),/Could not load pet preview asset/);
  });
  await t.test('wrong adult frame dimensions',async sub=>{
    const frame=variant('adult').actions.eat.frames[2];mockAssets(sub,{badDimensions:{path:frame,size:[64,128]}});
    await assert.rejects(createRenderer(canvasMock()).loadAssets(),/成年\/eat frame 2 must be 128 × 128; got 64 × 128/);
  });
  await t.test('wrong baby sheet dimensions',async sub=>{
    mockAssets(sub,{badDimensions:{path:'growth/baby/frames/sleep.png',size:[384,128]}});
    await assert.rejects(createRenderer(canvasMock()).loadAssets(),/幼年\/sleep sheet must be 512 × 128; got 384 × 128/);
  });
});
