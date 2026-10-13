import test from 'node:test';
import assert from 'node:assert/strict';
import { createState, normalizeState, reduceAction, advanceState } from '../preview/model.mjs';
import {
  createLifeController, getEnvironment, MAX_NAP_MS,
  NIGHT_WAKE_GRACE_MS, WEATHER_MAX_AGE_MS,
} from '../preview/life.mjs';

const EPOCH = new Date(2026, 9, 1, 12).getTime();
test('photos record the rendered environment scene without changing the selected habitat', () => {
  const initial=createState(EPOCH);
  const captured=reduceAction(initial,{type:'TAKE_PHOTO',id:'actual-night',scene:'night'},EPOCH);
  assert.equal(captured.state.scene,'forest');assert.equal(captured.state.album[0].scene,'night');
  const invalid=reduceAction(initial,{type:'TAKE_PHOTO',id:'fallback',scene:'unknown'},EPOCH);
  assert.equal(invalid.state.album[0].scene,'forest');
});
const day = (hour = 12, minute = 0) => new Date(2026, 9, 1, hour, minute);
const awake = () => createState(EPOCH);
const environment = (state, hour = 12, extra = {}) => getEnvironment(state, { date: day(hour), ...extra });
const tick = (controller, state, now, extra = {}) => controller.step({ state, now, environment: environment(state), ...extra });
const apply = (state, command, now = EPOCH) => {
  const result = reduceAction(state, command, now);
  assert.equal(result.ok, true);
  return result.state;
};

test('the local clock maps exact dawn, day, dusk and night boundaries', () => {
  for (const [hour, phase] of [[0, 'night'], [5, 'night'], [6, 'dawn'], [7, 'dawn'], [8, 'day'], [16, 'day'], [17, 'dusk'], [19, 'dusk'], [20, 'night'], [23, 'night']]) {
    assert.equal(environment(awake(), hour).phase, phase);
  }
});

test('automatic worlds keep forest and meadow composition families while clearing only changes light', () => {
  for (const [scene, daytime, nighttime] of [['forest', 'forest', 'night'], ['night', 'forest', 'night'], ['meadow', 'meadow', 'meadow-night'], ['meadow-night', 'meadow', 'meadow-night'], ['clearing', 'clearing', 'clearing']]) {
    const state = { ...awake(), scene, awakeScene: scene };
    assert.equal(environment(state, 12).scene, daytime);
    assert.equal(environment(state, 22).scene, nighttime);
    assert.equal(state.scene, scene, 'environment must not mutate the selected scene');
  }
});

test('disabled automatic world preserves selected scene and explicit clock tests never alter actual routine time', () => {
  const state = awake(); state.scene = 'meadow-night'; state.settings.autoWorld = false;
  const plain = environment(state, 12);
  assert.equal(plain.enabled, false);
  assert.equal(plain.scene, 'meadow-night');
  assert.equal(plain.phase, 'night');
  const preview = environment(state, 12, { phaseOverride: 'dawn' });
  assert.equal(preview.preview, true);
  assert.equal(preview.phase, 'dawn');
  assert.equal(preview.scene, 'meadow');
  assert.equal(preview.enabled, false);
  assert.equal(preview.hour, 12);
  assert.equal(preview.actualPhase, 'day');
});

test('explicit time previews pair manually selected forest scenes without enabling automatic routines', () => {
  const controller = createLifeController({ random: () => 0 });
  for (const [scene, phaseOverride, expected] of [['forest', 'night', 'night'], ['night', 'day', 'forest']]) {
    const state = { ...awake(), scene, awakeScene: scene };
    state.settings.autoWorld = false;
    const env = environment(state, 22, { phaseOverride });
    assert.equal(env.scene, expected);
    assert.equal(env.enabled, false);
    assert.equal(env.preview, true);
    assert.equal(state.scene, scene, 'preview must leave the stored selection unchanged');
    assert.equal(tick(controller, state, 8000, { environment: env }).command, null);
    assert.equal(environment(state, 12).scene, scene, 'returning to actual restores the selection');
  }
});

test('weather is trusted only when complete, recent and from the expected source', () => {
  const state = awake();
  const valid = { city: '汕头', temperature: 26, condition: '小雨', isDay: true, updatedAt: EPOCH, source: 'Open-Meteo' };
  assert.equal(environment(state, 12, { weather: valid }).weather, 'rain');
  assert.equal(environment(state, 12, { weather: { ...valid, updatedAt: EPOCH - WEATHER_MAX_AGE_MS } }).weatherFresh, true);
  for (const invalid of [
    { ...valid, updatedAt: EPOCH - WEATHER_MAX_AGE_MS - 1 }, { ...valid, updatedAt: EPOCH + 6 * 60000 },
    { ...valid, source: 'fake' }, { ...valid, temperature: NaN }, { ...valid, error: 'offline' },
    { ...valid, isDay: null }, { ...valid, loading: true }, {},
  ]) {
    const env = environment(state, 12, { weather: invalid });
    assert.equal(env.weather, 'unknown'); assert.equal(env.weatherFresh, false);
  }
  const preview = environment(state, 12, { weatherOverride: 'storm' });
  assert.equal(preview.weather, 'storm'); assert.equal(preview.weatherSource, 'preview');
  assert.equal(preview.weatherFresh, false); assert.equal(preview.preview, true);
});

test('supported source conditions map to weather effects; unrecognized conditions never invent sunshine', () => {
  for (const [condition, kind] of [['晴', 'clear'], ['多云', 'cloudy'], ['小雨', 'rain'], ['雷暴', 'storm'], ['雾', 'fog'], ['小雪', 'snow'], ['资料不足', 'unknown']]) {
    const weather = { temperature: 24, condition, isDay: true, updatedAt: EPOCH, source: 'Open-Meteo' };
    assert.equal(environment(awake(), 12, { weather }).weather, kind);
  }
});

test('V1 saves acquire safe defaults and preserve explicit autonomous settings', () => {
  const raw = awake(); delete raw.settings.autoLife; delete raw.settings.autoWorld; delete raw.sleepReason;
  const restored = normalizeState(raw, EPOCH).state;
  assert.equal(restored.settings.autoLife, true); assert.equal(restored.settings.autoWorld, true);
  assert.equal(restored.version, 1); assert.equal(restored.sleepReason, null);
  const changed = apply(restored, { type: 'SET_SETTINGS', settings: { autoLife: false, autoWorld: false } });
  const saved = normalizeState(JSON.parse(JSON.stringify(changed)), EPOCH).state;
  assert.equal(saved.settings.autoLife, false); assert.equal(saved.settings.autoWorld, false);
});

test('sleep and naps preserve selected habitat and reasons survive a round trip', () => {
  for (const scene of ['forest', 'clearing', 'meadow']) {
    const state = { ...awake(), scene, awakeScene: scene };
    const manual = apply(state, { type: 'SLEEP' });
    assert.equal(manual.sleepReason, 'manual');
    assert.equal(manual.scene,scene);
    assert.equal(environment(manual, 12).scene, manual.scene);
    const old = { ...manual }; delete old.sleepReason;
    assert.equal(normalizeState(old, EPOCH).state.sleepReason, 'manual');
    const nap = apply(state, { type: 'SLEEP', reason: 'nap' });
    assert.equal(nap.scene, scene); assert.equal(nap.sleepReason, 'nap');
    const restored = normalizeState(JSON.parse(JSON.stringify(nap)), EPOCH).state;
    assert.equal(restored.scene, scene); assert.equal(restored.sleepReason, 'nap');
    assert.equal(environment(restored, 12).scene, scene);
    const woken = apply(restored, { type: 'WAKE' });
    assert.equal(woken.sleepReason, null); assert.equal(woken.scene, scene);
  }
});

test('quiet time precedes everyday poses, which use different actions without awarding care benefits', () => {
  const state = awake(), original = JSON.stringify(state);
  const controller = createLifeController({ random: () => 0 });
  assert.equal(tick(controller, state, 7999).action, null);
  const walking = tick(controller, state, 8000);
  assert.equal(walking.action.type, 'walk');
  assert.equal(tick(controller, state, 8500).action.start, 8000);
  tick(controller, state, 10400);
  assert.equal(tick(controller, state, 18399).action, null);
  assert.equal(tick(controller, state, 18400).action.type, 'stretch', 'avoid the exact previous pose');
  assert.equal(JSON.stringify(state), original, 'autonomy never edits stock, stats, counters or coins');
});

test('walking translates inside bounds, preserves resting position and freezes on a user action', () => {
  const state = awake(), controller = createLifeController({ random: () => 0 });
  tick(controller, state, 8000);
  const halfway = tick(controller, state, 9200);
  assert.equal(halfway.positionX, -6);
  const interrupted = tick(controller, state, 9200, { manualBusy: true });
  assert.equal(interrupted.action, null); assert.equal(interrupted.positionX, -6);
  assert.equal(tick(controller, state, 14000, { manualBusy: true }).positionX, -6);
  for (let now = 22000; now < 600000; now += 1000) {
    const result = tick(controller, state, now);
    assert.ok(result.positionX >= -24 && result.positionX <= 24);
  }
  const snapshot = controller.getSnapshot();
  assert.ok(Number.isFinite(snapshot.positionX));
  controller.reset(700000);
  assert.equal(controller.getSnapshot().positionX, 0);
  assert.equal(tick(controller, state, 700001).action, null);
});

test('feeding, games, manual poses, reactions and poor care take priority over autonomous life', () => {
  const controller = createLifeController({ random: () => 0 });
  for (const blocked of [
    { ...awake(), activity: { type: 'feed' } }, { ...awake(), game: { id: 'game' } },
    { ...awake(), reaction: { type: 'happy', until: EPOCH + 1000 } },
    { ...awake(), stats: { ...awake().stats, health: 39 } },
    { ...awake(), stats: { ...awake().stats, mood: 34 } },
  ]) {
    const result = tick(controller, blocked, 50000, { tilt: { x: 30, y: 0 }, environment: { ...environment(blocked), weather: 'rain' } });
    assert.equal(result.action, null); assert.equal(result.command, null); assert.ok(!Object.hasOwn(result,'gripping'));
  }
  assert.equal(tick(controller, awake(), 50001, { manualBusy: true }).mode, 'busy');
  assert.equal(tick(controller, awake(), 50002, { active: false }).mode, 'inactive');
});

test('rain shelters continuously and tilt inputs cannot trigger gripping or move the pet',()=>{
  const state=awake(),controller=createLifeController({random:()=>0});
  const rain={...environment(state),weather:'rain'};
  const sheltered=tick(controller,state,0,{environment:rain});assert.equal(sheltered.action.type,'shelter');
  const tilted=tick(controller,state,500,{environment:rain,tilt:{x:60,y:60}});
  assert.equal(tilted.action.type,'shelter');assert.equal(tilted.action.start,0);assert.equal(tilted.positionX,0);
  assert.ok(!Object.hasOwn(tilted,'gripping'));
  const sunny=tick(controller,state,6000);assert.equal(sunny.action,null);
});

test('freely selected scenes survive sleeping waking and save; automatic time can be restored by habitat family',()=>{
  for(const scene of ['forest','night','clearing','meadow','meadow-night']){
    const selected=apply(awake(),{type:'SET_SCENE',scene});assert.equal(selected.settings.autoWorld,false);
    const asleep=apply(selected,{type:'SLEEP'});assert.equal(environment(asleep,22).scene,scene);
    const moved=apply(asleep,{type:'SET_SCENE',scene:'clearing'});assert.equal(moved.sleeping,true);assert.equal(moved.scene,'clearing');
    assert.equal(apply(moved,{type:'WAKE'}).scene,'clearing');
    const restored=normalizeState(JSON.parse(JSON.stringify(asleep)),EPOCH).state;assert.equal(environment(restored,12).scene,scene);
    const automatic=apply(restored,{type:'SET_SETTINGS',settings:{autoWorld:true}});
    assert.equal(environment(automatic,22).scene,scene.startsWith('meadow')?'meadow-night':scene==='clearing'?'clearing':'night');
  }
});

test('night routines issue each command once, wake after 7 and never wake a manually sleeping pet', () => {
  const state = awake(), controller = createLifeController({ random: () => 0 });
  const night = environment(state, 21);
  assert.equal(tick(controller, state, 7999, { environment: night }).command, null);
  const bedtime = tick(controller, state, 8000, { environment: night });
  assert.deepEqual(bedtime.command, { type: 'SLEEP', reason: 'routine' });
  assert.equal(tick(controller, state, 8001, { environment: night }).command, null);
  const asleep = apply(state, bedtime.command);
  assert.equal(tick(controller, asleep, 9000, { environment: environment(asleep, 6) }).command, null);
  const morning = tick(controller, asleep, 10000, { environment: environment(asleep, 7) });
  assert.deepEqual(morning.command, { type: 'WAKE' });
  assert.equal(tick(controller, asleep, 10001, { environment: environment(asleep, 7) }).command, null);
  const manual = apply(state, { type: 'SLEEP' });
  assert.equal(tick(createLifeController(), manual, 10000, { environment: environment(manual, 12) }).command, null);
});

test('waking or interacting at night protects the user choice for at least 30 minutes', () => {
  const controller = createLifeController({ random: () => 0 }), state = awake();
  controller.interaction(1000);
  assert.equal(tick(controller, state, 10000, { environment: environment(state, 22) }).command, null);
  const deadline = 1000 + NIGHT_WAKE_GRACE_MS;
  assert.equal(tick(controller, state, deadline - 1, { environment: environment(state, 22) }).command, null);
  assert.equal(tick(controller, state, deadline, { environment: environment(state, 22) }).command?.reason, 'routine');
  const fresh = createLifeController({ random: () => 0 });
  const asleep = apply(state, { type: 'SLEEP' });
  tick(fresh, asleep, 0);
  const woken = apply(asleep, { type: 'WAKE' });
  woken.reaction = null;
  assert.equal(tick(fresh, woken, 10000, { environment: environment(woken, 22) }).command, null);
});

test('preview nighttime changes artwork without making an actual midday routine sleep', () => {
  const state = awake(), controller = createLifeController({ random: () => 0 });
  const env = environment(state, 12, { phaseOverride: 'night' });
  assert.equal(env.phase, 'night'); assert.equal(env.hour, 12);
  assert.equal(tick(controller, state, 8000, { environment: env }).command, null);
});

test('low energy naps stay in place, end at 35 energy or 60 minutes, and do not use stock or coins', () => {
  const controller = createLifeController({ random: () => 0 }), state = awake();
  state.stats.energy = 17;
  const nap = tick(controller, state, 8000);
  assert.deepEqual(nap.command, { type: 'SLEEP', reason: 'nap' });
  const asleep = apply(state, nap.command);
  tick(controller, asleep, 8001);
  assert.equal(tick(controller, asleep, 8001 + MAX_NAP_MS - 1).command, null);
  assert.deepEqual(tick(controller, asleep, 8001 + MAX_NAP_MS).command, { type: 'WAKE' });
  const rested = advanceState(asleep, 54*60_000, EPOCH + 54*60_000);
  assert.equal(rested.stats.energy, 35);
  assert.equal(rested.coins, state.coins); assert.deepEqual(rested.inventory, state.inventory);
  const byEnergy = createLifeController();
  const notQuiteRested=advanceState(asleep,53*60_000,EPOCH+53*60_000);
  assert.equal(tick(byEnergy,notQuiteRested,0).command,null);
  assert.deepEqual(tick(byEnergy, rested, 60_000).command, { type: 'WAKE' });
});

test('an automatic nap grants thirty minutes before another low-energy nap', () => {
  const controller=createLifeController({random:()=>0}), state=awake();state.stats.energy=17;
  const asleep=apply(state,tick(controller,state,8000).command);
  tick(controller,asleep,8001);
  const end=8001+MAX_NAP_MS;
  const awakeAgain=advanceState(apply(asleep,tick(controller,asleep,end).command),1200,EPOCH+1200);
  tick(controller,awakeAgain,end+1);
  assert.equal(tick(controller,awakeAgain,end+30*60_000).command,null);
  assert.deepEqual(tick(controller,awakeAgain,end+1+30*60_000).command,{type:'SLEEP',reason:'nap'});
});

test('turning off autonomy clears poses and automatic sleep but preserves manual sleep', () => {
  const controller = createLifeController({ random: () => 0 }), state = awake();
  assert.equal(tick(controller, state, 8000).action?.type, 'walk');
  const off = apply(state, { type: 'SET_SETTINGS', settings: { autoLife: false } });
  const stopped = tick(controller, off, 8500, { tilt: { x: 40, y: 0 } });
  assert.equal(stopped.action, null); assert.ok(!Object.hasOwn(stopped,'gripping'));
  const routine = apply(off, { type: 'SLEEP', reason: 'routine' });
  assert.deepEqual(tick(controller, routine, 8501).command, { type: 'WAKE' });
  assert.equal(tick(controller, routine, 8502).command, null);
  const manual = apply(off, { type: 'SLEEP' });
  assert.equal(tick(controller, manual, 8503).command, null);
});
