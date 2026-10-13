import test from 'node:test';
import assert from 'node:assert/strict';
import {
  advanceState, CATALOGUE, createState, FEED_DURATION_MS, GAME_DURATION_MS,
  getCondition, MAX_ADVANCE_MS, MAX_PHOTOS, normalizeState, reduceAction, SAVE_VERSION, SCENES,
  DIGESTION_MS,TREATMENT_MS,NEGLECT_MS,dailyReady,syncDay,getCareNeeds,localDayKey,
  getGrowthInfo,GROWTH_TIMES,
} from '../preview/model.mjs';

const NOW = 1_000_000;
const dispatch = (state, action, now = NOW) => reduceAction(state, action, now);
const freeze = state => {
  for (const value of Object.values(state)) if (value && typeof value === 'object') freeze(value);
  return Object.freeze(state);
};

test('new chameleon starts with specified stats, supplies and no upgrade fields', () => {
  const state = createState(NOW);
  assert.deepEqual(state.stats, { health: 82, fullness: 70, energy: 65, mood: 90, cleanliness: 80 });
  assert.equal(state.coins, 30);
  assert.equal(state.scene, 'forest');
  assert.equal(state.settings.name, '卷卷');
  assert.ok(!Object.hasOwn(state, 'level'));
  assert.ok(!Object.hasOwn(state, 'upgrade'));
  assert.equal(normalizeState(null, NOW).status, 'new');
  assert.deepEqual(getGrowthInfo(state), {id:'baby',label:'幼年',progress:0,nextLabel:'少年',
    remainingMs:GROWTH_TIMES.normal.teen,paused:false,pauseReason:null,speed:'normal'});
});

test('normal growth evolves after seven and fourteen days of good care and saturates at adulthood',()=>{
  let state=createState(NOW); state.sleeping=true;
  for(let hour=1;hour<=14*24;hour++){
    state.stats.fullness=state.stats.cleanliness=100;
    state=advanceState(state,3_600_000,NOW+hour*3_600_000);
    assert.equal(state.growthMs,hour*3_600_000);
    assert.equal(getGrowthInfo(state).id,hour<7*24?'baby':hour<14*24?'teen':'adult');
  }
  state=advanceState(state,3_600_000);
  assert.equal(state.growthMs,GROWTH_TIMES.normal.adult);
  assert.deepEqual(getGrowthInfo(state),{id:'adult',label:'成年',progress:1,nextLabel:null,remainingMs:0,paused:false,pauseReason:null,speed:'normal'});
});

test('isolated fast growth crosses both exact boundaries and cannot switch speed after progress',()=>{
  let state=createState(NOW,'fast'); Object.assign(state.stats,{health:100,fullness:100,cleanliness:100,energy:100});
  const teen=GROWTH_TIMES.fast.teen,adult=GROWTH_TIMES.fast.adult;
  state=advanceState(state,teen-1);
  assert.equal(getGrowthInfo(state).id,'baby');assert.equal(getGrowthInfo(state).remainingMs,1);
  assert.equal(dispatch(state,{type:'SET_GROWTH_SPEED',growthSpeed:'normal'}).reason,'growth_started');
  state=advanceState(state,1);assert.equal(getGrowthInfo(state).id,'teen');assert.equal(getGrowthInfo(state).progress,0);
  state=advanceState(state,adult-teen-1);assert.equal(getGrowthInfo(state).remainingMs,1);
  state=advanceState(state,1);assert.equal(getGrowthInfo(state).id,'adult');assert.equal(state.growthMs,adult);
  const fresh=createState(NOW);
  assert.equal(dispatch(fresh,{type:'SET_GROWTH_SPEED',growthSpeed:'fast'}).state.growthSpeed,'fast');
  assert.equal(dispatch(fresh,{type:'SET_GROWTH_SPEED',growthSpeed:'unknown'}).reason,'invalid_growth_speed');
  assert.equal(createState(NOW,'unknown').growthSpeed,'normal');
});

test('illness and six care limits pause growth and healthy sleep resumes retained progress',()=>{
  for(const [reason,changes] of [
    ['sick',{sick:true}],['health',{stats:{health:49}}],['food',{stats:{fullness:25}}],
    ['dirty',{stats:{cleanliness:30}}],['poop',{poopCount:3}],['rest',{stats:{energy:15}}],
  ]){
    let state=createState(NOW);state.growthMs=1234;
    if(changes.stats)Object.assign(state.stats,changes.stats);else Object.assign(state,changes);
    assert.equal(getGrowthInfo(state).pauseReason,reason);
    state=advanceState(state,1000);assert.equal(state.growthMs,1234,reason);
    state={...state,sick:false,poopCount:0,sleeping:true,stats:{health:82,fullness:70,energy:65,mood:90,cleanliness:80}};
    state=advanceState(state,1000);assert.equal(state.growthMs,2234,reason);assert.equal(getGrowthInfo(state).paused,false);
  }
});

test('growth starts exactly at recovered health and energy and stops at pending third waste',()=>{
  let state=createState(NOW);state.sleeping=true;Object.assign(state.stats,{health:49,fullness:100,cleanliness:100,energy:100});
  state=advanceState(state,2*3_600_000);assert.equal(state.growthMs,0);
  state=advanceState(state,60_000);assert.equal(state.growthMs,60_000);
  state=createState(NOW);state.sleeping=true;Object.assign(state.stats,{health:82,fullness:100,cleanliness:100,energy:14});
  state=advanceState(state,360_000);assert.equal(state.growthMs,180_000);
  state=createState(NOW);state.poopCount=2;state.digestionMs[0]=12345;
  state=advanceState(state,60_000);assert.equal(state.growthMs,12345);assert.equal(state.poopCount,3);
});

test('growth normalization preserves age without offline advancement and bounds corrupt legacy fields',()=>{
  const legacy=createState(NOW);delete legacy.growthMs;delete legacy.growthSpeed;
  const restored=normalizeState(legacy,NOW+GROWTH_TIMES.normal.adult).state;
  assert.equal(restored.growthMs,0);assert.equal(restored.growthSpeed,'normal');assert.equal(getGrowthInfo(restored).id,'baby');
  legacy.growthMs=GROWTH_TIMES.normal.teen+54321;legacy.growthSpeed='normal';
  assert.equal(normalizeState(legacy,NOW+GROWTH_TIMES.normal.adult).state.growthMs,legacy.growthMs);
  const corrupt=normalizeState({...legacy,growthMs:Number.MAX_SAFE_INTEGER,growthSpeed:'future'},NOW).state;
  assert.equal(corrupt.growthMs,GROWTH_TIMES.normal.adult);assert.equal(corrupt.growthSpeed,'normal');
  assert.equal(getGrowthInfo({scene:'forest',sleeping:true}).id,'baby');
  assert.equal(normalizeState({...legacy,growthMs:-1},NOW).state.growthMs,0);
});

test('growth integration agrees at forty milliseconds and uneven ticks over an evolution boundary',()=>{
  let state=createState(NOW,'fast');state.growthMs=GROWTH_TIMES.fast.teen-12345;state.sleeping=true;
  Object.assign(state.stats,{health:100,fullness:100,cleanliness:100,energy:14});
  const coarse=advanceState(state,600_000,NOW+600_000);
  let fine=state,uneven=state;
  for(let passed=40;passed<=600_000;passed+=40)fine=advanceState(fine,40,NOW+passed);
  for(let passed=0;passed<600_000;){const step=Math.min(600_000-passed,17+passed%977);passed+=step;uneven=advanceState(uneven,step,NOW+passed);}
  for(const result of [coarse,fine,uneven]){
    assert.ok(Math.abs(result.growthMs-(GROWTH_TIMES.fast.teen-12345+420_000))<1e-6);
    assert.equal(getGrowthInfo(result).id,'teen');
  }
});

test('tongue capture reserves a meal, finishes after animation and consumes exactly once', () => {
  const state = freeze(createState(NOW));
  const start = dispatch(state, { type: 'FEED_START', id: 'meal-1', item: 'cricket' });
  assert.equal(start.ok, true);
  assert.equal(start.state.inventory.cricket, 3);
  assert.equal(start.state.stats.fullness, 70);
  assert.equal(getCondition(start.state), 'eat');
  const early = dispatch(start.state, { type: 'FEED_FINISH', id: 'meal-1' }, NOW + FEED_DURATION_MS - 1);
  assert.equal(early.ok, false);
  assert.equal(early.reason, 'animation_pending');
  assert.strictEqual(early.state, start.state);
  const done = dispatch(start.state, { type: 'FEED_FINISH', id: 'meal-1' }, NOW + FEED_DURATION_MS);
  assert.equal(done.state.inventory.cricket, 2);
  assert.equal(done.state.stats.fullness, 95);
  assert.equal(done.state.counters.meals, 1);
  assert.equal(dispatch(done.state, { type: 'FEED_FINISH', id: 'meal-1' }, NOW + 2000).ok, false);
  assert.equal(dispatch(done.state, { type: 'FEED_START', id: 'meal-1', item: 'cricket' }, NOW + 2000).ok, false);
  assert.equal(state.inventory.cricket, 3);
});

test('all four insect foods are stocked, purchasable and have distinct one-time nutrition', () => {
  const foods = { cricket: 25, cockroach: 30, mealworm: 18, fly: 12 };
  assert.deepEqual(Object.values(CATALOGUE).filter(item => item.type === 'food').map(item => item.id).sort(), Object.keys(foods).sort());
  assert.equal(createState(NOW).inventory.cockroach, 3);
  assert.equal(createState(NOW).inventory.fly, 4);
  for (const [item, fullness] of Object.entries(foods)) {
    const state = createState(NOW);
    state.stats.fullness = 40;
    state.stats.mood = 50;
    const start = dispatch(state, { type: 'FEED_START', id: `${item}-meal`, item }).state;
    const result = dispatch(start, { type: 'FEED_FINISH', id: `${item}-meal` }, NOW + FEED_DURATION_MS);
    assert.equal(result.ok, true, `${item} should be edible`);
    assert.equal(result.state.stats.fullness, 40 + fullness);
    assert.equal(result.state.stats.mood, 50 + CATALOGUE[item].mood);
    assert.equal(result.state.inventory[item], state.inventory[item] - 1);
    assert.equal(dispatch(result.state, { type: 'FEED_FINISH', id: `${item}-meal` }, NOW + 2000).ok, false);
    const buy = dispatch(state, { type: 'BUY', item });
    assert.equal(buy.ok, true, `${item} should be purchasable`);
    assert.equal(buy.state.inventory[item], state.inventory[item] + 1);
    assert.equal(buy.state.coins, 30 - CATALOGUE[item].price);
  }
});

test('wrong meal IDs, empty inventory, full stomach and cancel do not waste supplies', () => {
  const state = createState(NOW);
  state.inventory.cricket = 0;
  assert.equal(dispatch(state, { type: 'FEED_START', id: 'a', item: 'cricket' }).reason, 'out_of_stock');
  assert.equal(dispatch(state, { type: 'FEED_START', id: 'a', item: 'soap' }).reason, 'invalid_food');
  state.stats.fullness = 100;
  assert.equal(dispatch(state, { type: 'FEED_START', id: 'b', item: 'mealworm' }).reason, 'already_full');
  state.stats.fullness = 50;
  const start = dispatch(state, { type: 'FEED_START', id: 'c', item: 'mealworm' }).state;
  assert.equal(dispatch(start, { type: 'FEED_FINISH', id: 'wrong' }, NOW + 5000).ok, false);
  assert.equal(dispatch(start, { type: 'SLEEP' }).reason, 'busy');
  const cancel = dispatch(start, { type: 'FEED_CANCEL', id: 'c' }).state;
  assert.equal(cancel.inventory.mealworm, 2);
  assert.equal(cancel.activity, null);
});

test('sleep blocks feeding and playing, restores energy and returns to previous scene', () => {
  const clearing = dispatch(createState(NOW), { type: 'SET_SCENE', scene: 'clearing' }).state;
  const asleep = dispatch(clearing, { type: 'SLEEP' }).state;
  assert.equal(asleep.scene, 'clearing');
  assert.equal(getCondition(asleep), 'sleep');
  for (const action of [
    { type: 'FEED_START', id: 'feed', item: 'cricket' }, { type: 'GAME_START', id: 'game' },
    { type: 'WALK' }, { type: 'SHAKE', strength: 'strong' },
  ]) assert.equal(dispatch(asleep, action).reason, 'sleeping');
  const rested = advanceState(asleep, 3_600_000);
  assert.equal(rested.stats.energy, 85);
  assert.equal(rested.stats.fullness, 68);
  const awake = dispatch(rested, { type: 'WAKE' }, NOW + 3_600_000).state;
  assert.equal(awake.scene, 'clearing');
  assert.equal(awake.sleeping, false);
});

test('five forest and meadow scenes are available; saves and photos use the same scene validation', () => {
  assert.deepEqual(SCENES.map(scene => scene.id), ['forest', 'clearing', 'night', 'meadow', 'meadow-night']);
  const state = createState(NOW);
  for (const scene of SCENES) assert.equal(dispatch(state, { type: 'SET_SCENE', scene: scene.id }).ok, true);
  assert.equal(dispatch(state, { type: 'SET_SCENE', scene: 'removed-scene' }).reason, 'invalid_scene');
  const raw = { ...state, scene: 'removed-scene', awakeScene: 'removed-scene', sleeping: true, album: [
    { id: 'old', scene: 'removed-scene' }, { id: 'forest-photo', scene: 'forest' },
  ] };
  const restored = normalizeState(raw, NOW).state;
  assert.equal(restored.scene, 'forest');
  assert.equal(restored.awakeScene, 'forest');
  assert.deepEqual(restored.album.map(photo => photo.id), ['forest-photo']);
  assert.equal(dispatch(restored, { type: 'WAKE' }).state.scene, 'forest');
});

test('manual scene choice is preserved through sleep, save and wake', () => {
  for (const scene of SCENES.map(item => item.id)) {
    const awake = dispatch(createState(NOW), { type: 'SET_SCENE', scene }).state;
    const asleep = dispatch(awake, { type: 'SLEEP' }).state;
    const expectedNight = scene;
    assert.equal(asleep.scene, expectedNight, `sleep from ${scene}`);
    assert.equal(asleep.awakeScene, scene);
    const restored = normalizeState(JSON.parse(JSON.stringify(asleep)), NOW + 1000).state;
    assert.equal(restored.scene, expectedNight, `restore sleeping in ${scene}`);
    assert.equal(dispatch(restored, { type: 'WAKE' }, NOW + 1000).state.scene, scene);
  }
});

test('sleeping save preserves selected scene and accepts meadow photos', () => {
  const raw = createState(NOW);
  raw.sleeping = true;
  raw.scene = 'forest';
  raw.awakeScene = 'meadow';
  raw.album = [{ id: 'day', scene: 'meadow' }, { id: 'moon', scene: 'meadow-night' }];
  const restored = normalizeState(raw, NOW).state;
  assert.equal(restored.scene, 'forest');
  assert.deepEqual(restored.album.map(photo => photo.scene), ['meadow', 'meadow-night']);
  const captured = dispatch(restored, { type: 'TAKE_PHOTO', id: 'sleeping-meadow' }).state;
  assert.equal(captured.album.at(-1).scene, 'forest');
  assert.equal(captured.album.at(-1).pose, 'sleep');
});

test('game validates targets and duplicate hits, rewards only once on early end', () => {
  const started = dispatch(createState(NOW), { type: 'GAME_START', id: 'game-1' }).state;
  assert.equal(started.stats.energy, 60);
  assert.equal(started.inventory.ball, 1);
  assert.equal(dispatch(started, { type: 'GAME_HIT', id: 'game-1', target: 0, hitId: 'miss' }).reason, 'wrong_target');
  const hit = dispatch(started, { type: 'GAME_HIT', id: 'game-1', target: 1, hitId: 'hit-1' }, NOW + 100).state;
  assert.equal(hit.coins, 30);
  assert.equal(hit.game.hits, 1);
  assert.equal(dispatch(hit, { type: 'GAME_HIT', id: 'game-1', target: hit.game.target, hitId: 'hit-1' }, NOW + 400).reason, 'already_finished');
  assert.equal(dispatch(hit, { type: 'GAME_HIT', id: 'game-1', target: hit.game.target, hitId: 'hit-2' }, NOW + 200).reason, 'too_fast');
  const second = dispatch(hit, { type: 'GAME_HIT', id: 'game-1', target: hit.game.target, hitId: 'hit-2' }, NOW + 400).state;
  const ended = dispatch(second, { type: 'GAME_END', id: 'game-1' }, NOW + 500);
  assert.equal(ended.state.coins, 34);
  assert.equal(ended.state.counters.games, 1);
  assert.equal(ended.state.counters.hits, 2);
  assert.deepEqual(ended.event, { type: 'game_end', id: 'game-1', hits: 2, reward: 4 });
  assert.equal(dispatch(ended.state, { type: 'GAME_END', id: 'game-1' }).ok, false);
  assert.equal(dispatch(ended.state, { type: 'GAME_START', id: 'game-1' }).reason, 'already_finished');
});

test('game timeout auto settles once and late input is not counted', () => {
  const start = dispatch(createState(NOW), { type: 'GAME_START', id: 'game' }).state;
  const hit = dispatch(start, { type: 'GAME_HIT', id: 'game', hitId: '1', target: 1 }, NOW + 500).state;
  assert.equal(dispatch(hit, { type: 'GAME_HIT', id: 'game', hitId: 'late', target: hit.game.target }, NOW + GAME_DURATION_MS).reason, 'game_over');
  const finished = advanceState(hit, GAME_DURATION_MS, NOW + GAME_DURATION_MS);
  assert.equal(finished.game, null);
  assert.equal(finished.coins, 32);
  assert.equal(advanceState(finished, 1000).coins, 32);
  assert.equal(dispatch(finished, { type: 'GAME_END', id: 'game' }).ok, false);
});

test('purchase is atomic, rejects invalid quantities and optionally de-duplicates IDs', () => {
  const state = freeze(createState(NOW));
  const buy = dispatch(state, { type: 'BUY', item: 'cricket', quantity: 2, id: 'order' });
  assert.equal(buy.state.coins, 18);
  assert.equal(buy.state.inventory.cricket, 5);
  assert.equal(dispatch(buy.state, { type: 'BUY', item: 'cricket', quantity: 2, id: 'order' }).reason, 'already_finished');
  assert.equal(dispatch(state, { type: 'BUY', item: 'medicine', quantity: 3 }).reason, 'not_enough_coins');
  for (const quantity of [-1, 0, 1.5, 100, Infinity]) {
    const invalid = dispatch(state, { type: 'BUY', item: 'cricket', quantity });
    assert.equal(invalid.ok, false);
    assert.strictEqual(invalid.state, state);
  }
  const full = createState(NOW);
  full.inventory.soap = 99;
  assert.equal(dispatch(full, { type: 'BUY', item: 'soap' }).reason, 'inventory_full');
  assert.equal(full.coins, 30);
});

test('bathing consumes soap while healthy pets cannot consume medicine', () => {
  let state = createState(NOW);state.stats.health=90;state.stats.cleanliness=90;
  const medicine=dispatch(state,{type:'MEDICINE'});
  assert.equal(medicine.ok,false);assert.equal(medicine.reason,'already_healthy');
  assert.equal(medicine.state.inventory.medicine,1);
  const clean=dispatch(state,{type:'CLEAN'});
  assert.equal(clean.state.stats.cleanliness,100);assert.equal(clean.state.inventory.soap,1);
  assert.equal(dispatch(clean.state,{type:'CLEAN'}).reason,'already_clean');
});

test('walking and simulated shake have separate reactions and bounded effects', () => {
  const state = createState(NOW);
  const walk = dispatch(state, { type: 'WALK' }).state;
  assert.equal(getCondition(walk), 'walk');
  assert.equal(walk.stats.energy, 63);
  assert.equal(dispatch(state, { type: 'JUMP' }).ok, false);
  const shake = dispatch(state, { type: 'SHAKE', strength: 'strong' });
  assert.equal(shake.event.simulated, true);
  assert.equal(getCondition(shake.state), 'dizzy');
  assert.equal(getCondition(advanceState(shake.state, 3000)), 'idle');
  assert.equal(dispatch(state, { type: 'SHAKE', strength: 'not-a-motion' }).ok, false);
});

test('invalid and very long time steps remain bounded, and every stat stays finite', () => {
  const state = freeze(createState(NOW));
  assert.deepEqual(advanceState(state, -500), state);
  assert.deepEqual(advanceState(state, NaN), state);
  assert.deepEqual(advanceState(state, Infinity), state);
  const limited = advanceState(state, MAX_ADVANCE_MS * 100);
  assert.deepEqual(limited, advanceState(state, MAX_ADVANCE_MS));
  for (const value of Object.values(limited.stats)) assert.ok(Number.isFinite(value) && value >= 0 && value <= 100);
  assert.equal(state.stats.fullness, 70);
});

test('fast-forward integrates health threshold crossings like normal one-second ticking', () => {
  for (const scenario of [
    { sleeping: true, health: 82, fullness: 20, cleanliness: 25 },
    { sleeping: true, health: 99, fullness: 20, cleanliness: 25 },
    { sleeping: false, health: 82, fullness: 20, cleanliness: 25 },
    { sleeping: true, health: 1, fullness: 14, cleanliness: 18 },
    { sleeping: false, health: 82, fullness: 15, cleanliness: 20 },
    { sleeping: true, health: 82, fullness: 24, cleanliness: 26 },
  ]) {
    const initial = createState(NOW);
    Object.assign(initial.stats, { health: scenario.health, fullness: scenario.fullness, cleanliness: scenario.cleanliness });
    initial.sleeping = scenario.sleeping;
    const fast = advanceState(initial, 600_000, NOW + 600_000);
    let ticking = initial;
    for (let second = 1; second <= 600; second++) ticking = advanceState(ticking, 1000, NOW + second * 1000);
    for (const key of Object.keys(initial.stats)) {
      assert.ok(Math.abs(fast.stats[key] - ticking.stats[key]) < 1e-8,
        `${key} should agree for ${JSON.stringify(scenario)}: ${fast.stats[key]} vs ${ticking.stats[key]}`);
    }
  }
});

test('sleep heals only until a threshold crossing and does not retain healing beyond the health cap', () => {
  const initial = createState(NOW);
  initial.sleeping = true;
  initial.stats.fullness = 16;
  initial.stats.cleanliness = 20.5;
  const advanced = advanceState(initial, 3_600_000);
  // Thirty healthy minutes heal 0.25; thirty hungry/dirty minutes lose 1.25.
  assert.ok(Math.abs(advanced.stats.health - 81) < 1e-10);
  initial.stats.health = 99.9;
  const capped = advanceState(initial, 3_600_000);
  assert.ok(Math.abs(capped.stats.health - 98.75) < 1e-10);
});

test('saved data round-trips and deliberately cancels interrupted animation without consumption', () => {
  const start = dispatch(createState(NOW), { type: 'FEED_START', id: 'interrupted', item: 'cricket' }).state;
  const raw = JSON.parse(JSON.stringify(start));
  const loaded = normalizeState(raw, NOW + 10_000);
  assert.equal(loaded.status, 'loaded');
  assert.equal(loaded.state.activity, null);
  assert.equal(loaded.state.inventory.cricket, 3);
  assert.equal(loaded.state.stats.fullness, 70);
  assert.equal(loaded.state.updatedAt, NOW);
  assert.equal(dispatch(loaded.state, { type: 'FEED_FINISH', id: 'interrupted' }, NOW + 10_000).ok, false);
});

test('malformed stats and settings normalize safely; future version is preserved by caller', () => {
  const raw = createState(NOW);
  raw.stats = { health: -10, fullness: 200, energy: NaN, mood: '20', cleanliness: Infinity };
  raw.coins = -100;
  raw.inventory.cricket = -1;
  raw.inventory.medicine = 1000;
  raw.settings = { brightness: -20, idleMinutes: 1000, name: ' \n ', sound: 'yes' };
  raw.scene = 'missing';
  const { state } = normalizeState(raw, NOW);
  assert.deepEqual(state.stats, { health: 0, fullness: 100, energy: 65, mood: 90, cleanliness: 80 });
  assert.equal(state.coins, 0);
  assert.equal(state.inventory.cricket, 0);
  assert.equal(state.inventory.medicine, 99);
  assert.equal(state.settings.brightness, 10);
  assert.equal(state.settings.idleMinutes, 60);
  assert.equal(state.settings.name, '卷卷');
  const future = { version: SAVE_VERSION + 1, precious: 'keep me' };
  assert.deepEqual(normalizeState(future), { state: null, status: 'unsupported' });
  assert.deepEqual(future, { version: SAVE_VERSION + 1, precious: 'keep me' });
});

test('virtual photo album caps storage, preserves scene/state and does not duplicate captures', () => {
  let state = createState(NOW);
  for (let i = 0; i < MAX_PHOTOS + 2; i++) state = dispatch(state, { type: 'TAKE_PHOTO', id: `photo-${i}` }, NOW + i).state;
  assert.equal(state.album.length, MAX_PHOTOS);
  assert.equal(state.album[0].id, 'photo-2');
  assert.equal(state.counters.photos, MAX_PHOTOS + 2);
  assert.equal(state.album[0].scene, 'forest');
  assert.equal(dispatch(state, { type: 'TAKE_PHOTO', id: 'photo-2' }).reason, 'already_finished');
  const loaded = normalizeState(JSON.parse(JSON.stringify(state)), NOW).state;
  assert.deepEqual(loaded.album, state.album);
  assert.equal(dispatch(loaded, { type: 'DELETE_PHOTO', id: 'photo-2' }).state.album.length, MAX_PHOTOS - 1);
});

test('catalogue and action layer do not allow unknown property purchases', () => {
  const state = createState(NOW);
  for (const item of ['__proto__', 'constructor', 'unknown']) {
    assert.equal(dispatch(state, { type: 'BUY', item }).ok, false);
    assert.equal(dispatch(state, { type: 'FEED_START', item, id: 'bad' }).ok, false);
  }
  assert.ok(Object.values(CATALOGUE).every(item => item.price > 0));
});

test('low fullness creates care reminders and severe persistent hunger causes illness',()=>{
  const initial=createState(NOW);initial.stats.fullness=25;
  assert.equal(getCareNeeds(initial)[0].id,'hungry');
  initial.stats.fullness=10;
  assert.match(getCareNeeds(initial)[0].label,/非常饿/);
  const before=advanceState(initial,NEGLECT_MS[0]-1000,NOW+NEGLECT_MS[0]-1000);assert.equal(before.sick,false);
  const sick=advanceState(before,1000,NOW+NEGLECT_MS[0]);assert.equal(sick.sick,true);assert.equal(sick.sicknessCause,'hunger');
  assert.equal(getCondition(sick),'sick');
  assert.equal(dispatch(sick,{type:'GAME_START',id:'ill-game'},NOW+NEGLECT_MS[0]).reason,'sick_rest');
});

test('one unattended waking hour preserves comfortable healthy needs at hourly care rates',()=>{
  const state=advanceState(createState(NOW),3_600_000,NOW+3_600_000);
  assert.deepEqual(state.stats,{health:82,fullness:64,energy:60,mood:87,cleanliness:77});
  assert.equal(state.sick,false);assert.deepEqual(getCareNeeds(state),[]);
});

test('eight hours of sleep conserve food and hygiene and fully restore energy without illness',()=>{
  const asleep=dispatch(createState(NOW),{type:'SLEEP'}).state;
  const first=advanceState(asleep,6*3_600_000,NOW+6*3_600_000);
  const rested=advanceState(first,2*3_600_000,NOW+8*3_600_000);
  assert.deepEqual(rested.stats,{health:86,fullness:54,energy:100,mood:98,cleanliness:72});
  assert.equal(rested.sick,false);assert.equal(rested.sleeping,true);
  assert.deepEqual(rested.neglectMs,[0,0,0]);
});

test('three waste piles remind after two minutes and need ninety continuous minutes to cause illness',()=>{
  const initial=createState(NOW);initial.poopCount=3;
  const reminded=advanceState(initial,120_000,NOW+120_000);
  assert.equal(reminded.sick,false);assert.equal(getCareNeeds(reminded)[0].id,'poop');
  const almost=advanceState(reminded,90*60_000-120_000-1000,NOW+90*60_000-1000);
  assert.equal(almost.sick,false);assert.ok(almost.stats.health>80);
  const sick=advanceState(almost,1000,NOW+90*60_000);
  assert.equal(sick.sick,true);assert.equal(sick.sicknessCause,'dirty');
});

test('feeding bathing cleanup and starting restorative sleep reset the corresponding exposure',()=>{
  const hungry=createState(NOW);hungry.stats.fullness=10;
  let fed=advanceState(hungry,55*60_000,NOW+55*60_000);
  fed=dispatch(fed,{type:'FEED_START',id:'care-now',item:'cricket'},fed.updatedAt).state;
  fed=dispatch(fed,{type:'FEED_FINISH',id:'care-now'},fed.updatedAt+FEED_DURATION_MS).state;
  fed=advanceState(fed,1,fed.updatedAt+1);
  assert.equal(fed.neglectMs[0],0);assert.equal(fed.sick,false);
  const dirty=createState(NOW);dirty.stats.cleanliness=20;
  let washed=advanceState(dirty,85*60_000,NOW+85*60_000);
  washed=dispatch(washed,{type:'CLEAN'},washed.updatedAt).state;
  washed=advanceState(washed,1,washed.updatedAt+1);
  assert.equal(washed.neglectMs[1],0);assert.equal(washed.sick,false);
  const waste=createState(NOW);waste.poopCount=3;
  let cleaned=advanceState(waste,85*60_000,NOW+85*60_000);
  cleaned=dispatch(cleaned,{type:'CLEAR_POOP'},cleaned.updatedAt).state;
  assert.equal(cleaned.neglectMs[1],0);assert.equal(cleaned.sick,false);
  const tired=createState(NOW);tired.stats.energy=10;
  let rested=advanceState(tired,115*60_000,NOW+115*60_000);
  rested=dispatch(rested,{type:'SLEEP'},rested.updatedAt).state;
  rested=advanceState(rested,1,rested.updatedAt+1);
  assert.equal(rested.neglectMs[2],0);assert.equal(rested.sick,false);
});

test('slower care preserves existing saved illness and avoids weakening through a new hunger grace period',()=>{
  const old=createState(NOW);old.sick=true;old.sicknessCause='dirty';old.neglectMs=[180_000,120_000,240_000];
  const restored=normalizeState(JSON.parse(JSON.stringify(old)),NOW).state;
  assert.equal(restored.sick,true);assert.equal(restored.sicknessCause,'dirty');
  assert.deepEqual(restored.neglectMs,old.neglectMs);
  const neglected=createState(NOW);neglected.stats.fullness=0;neglected.stats.cleanliness=0;neglected.poopCount=3;
  const warned=advanceState(neglected,60*60_000-1000,NOW+60*60_000-1000);
  assert.equal(warned.sick,false);assert.ok(warned.stats.health>79);
});

test('neglect timers reset when the need improves and dirt and exhaustion have their own causes',()=>{
  const hunger=createState(NOW);hunger.stats.fullness=9;
  const almost=advanceState(hunger,100_000,NOW+100_000);almost.stats.fullness=60;
  const fed=advanceState(almost,1000,NOW+101_000);assert.equal(fed.neglectMs[0],0);
  const dirty=createState(NOW);dirty.stats.cleanliness=19;
  assert.equal(advanceState(dirty,NEGLECT_MS[1],NOW+NEGLECT_MS[1]).sicknessCause,'dirty');
  const tired=createState(NOW);tired.stats.energy=9;
  assert.equal(advanceState(tired,NEGLECT_MS[2],NOW+NEGLECT_MS[2]).sicknessCause,'exhaustion');
});

test('medicine starts one treatment and recovery only advances after feeding cleaning and rest',()=>{
  const original=createState(NOW);original.sick=true;original.sicknessCause='hunger';original.stats.health=0;original.stats.fullness=8;
  const treated=dispatch(original,{type:'MEDICINE'}).state;
  assert.equal(treated.stats.health,20);assert.equal(treated.inventory.medicine,0);assert.equal(treated.treatmentMs,TREATMENT_MS);
  assert.equal(dispatch(treated,{type:'MEDICINE'}).reason,'treatment_pending');
  const waiting=advanceState(treated,20_000,NOW+20_000);assert.equal(waiting.treatmentMs,TREATMENT_MS);assert.equal(waiting.sick,true);
  waiting.stats.fullness=80;waiting.stats.cleanliness=90;waiting.stats.energy=80;
  const almost=advanceState(waiting,TREATMENT_MS-1000,NOW+49_000);assert.equal(almost.sick,true);
  const healed=advanceState(almost,1000,NOW+50_000);assert.equal(healed.sick,false);assert.equal(healed.stats.health,45);assert.equal(healed.counters.recoveries,1);
  assert.equal(advanceState(healed,1000,NOW+51_000).sick,false,'recovery does not immediately relapse at very low previous health');
});

test('completed meals digest after two minutes, stack at three and free cleanup never consumes soap',()=>{
  let state=createState(NOW);state.stats.fullness=0;
  for(let i=0;i<4;i++){
    const started=dispatch(state,{type:'FEED_START',id:`digest-${i}`,item:'fly'},NOW+i*2000);
    state=dispatch(started.state,{type:'FEED_FINISH',id:`digest-${i}`},NOW+i*2000+FEED_DURATION_MS).state;
  }
  assert.equal(state.digestionMs.filter(value=>value>0).length,3);
  const before=advanceState(state,DIGESTION_MS-1000,state.updatedAt+DIGESTION_MS-1000);assert.equal(before.poopCount,0);
  const dirty=advanceState(before,1000,before.updatedAt+1000);assert.equal(dirty.poopCount,3);
  const soap=dirty.inventory.soap,cleanups=dirty.counters.cleanups;
  const cleaned=dispatch(dirty,{type:'CLEAR_POOP'},dirty.updatedAt);
  assert.equal(cleaned.state.poopCount,0);assert.equal(cleaned.state.inventory.soap,soap);assert.equal(cleaned.state.counters.cleanups,cleanups+1);
  assert.equal(dispatch(cleaned.state,{type:'CLEAR_POOP'},dirty.updatedAt).reason,'no_poop');
});

test('daily care requires completed feeding real cleanup and a scored settled game and rewards once per actual day',()=>{
  const today=new Date(2026,9,1,12),tomorrow=new Date(2026,9,2,12);let state=syncDay(createState(NOW),today);
  const key=localDayKey(today);assert.equal(state.dayKey,key);
  const started=dispatch(state,{type:'FEED_START',id:'daily-meal',item:'fly'}).state;assert.equal(started.dailyMask,0);
  state=dispatch(started,{type:'FEED_FINISH',id:'daily-meal'},NOW+FEED_DURATION_MS).state;
  assert.equal(state.dailyMask,1);state.poopCount=1;state=dispatch(state,{type:'CLEAR_POOP'},state.updatedAt).state;
  assert.equal(state.dailyMask,3);
  const empty=dispatch(state,{type:'GAME_START',id:'empty'},state.updatedAt).state;
  state=dispatch(empty,{type:'GAME_END',id:'empty'},state.updatedAt).state;assert.equal(state.dailyMask,3);
  state=dispatch(state,{type:'GAME_START',id:'score'},state.updatedAt).state;
  state=dispatch(state,{type:'GAME_HIT',id:'score',target:1,hitId:'one'},state.updatedAt+500).state;
  state=dispatch(state,{type:'GAME_END',id:'score'},state.updatedAt).state;
  assert.equal(state.dailyMask,7);assert.equal(dailyReady(state),true);assert.equal(state.careTotal,3);
  const coins=state.coins;assert.equal(dispatch(state,{type:'CLAIM_DAILY',dayKey:key+1},state.updatedAt).ok,false);
  state=dispatch(state,{type:'CLAIM_DAILY',dayKey:key},state.updatedAt).state;
  assert.equal(state.coins,coins+10);assert.equal(state.dailyClaims,1);assert.equal(dailyReady(state),false);
  assert.equal(dispatch(state,{type:'CLAIM_DAILY',dayKey:key},state.updatedAt).reason,'daily_claimed');
  assert.equal(advanceState(state,600_000,state.updatedAt+600_000).dayKey,key,'preview fast-forward never changes real calendar');
  const next=syncDay(state,tomorrow);assert.equal(next.dailyMask,0);assert.equal(next.dailyClaimed,false);assert.equal(next.careTotal,3);
  assert.equal(syncDay(next,today).dayVerified,false,'clock rollback cannot reopen a previously rewarded day');
});

test('care milestone badges persist through save and daily reset; old v1 saves gain safe defaults',()=>{
  let state=createState(NOW);state.careTotal=9;state.poopCount=1;
  state=dispatch(state,{type:'CLEAR_POOP'}).state;assert.equal(state.badges,1);
  state.careTotal=49;state.poopCount=1;state=dispatch(state,{type:'CLEAR_POOP'}).state;assert.equal(state.badges,3);
  state.careTotal=99;state.poopCount=1;state=dispatch(state,{type:'CLEAR_POOP'}).state;assert.equal(state.badges,7);
  const restored=normalizeState(JSON.parse(JSON.stringify(state)),NOW).state;assert.equal(restored.badges,7);assert.equal(restored.careTotal,100);
  const legacy=createState(NOW);for(const key of ['sick','sicknessCause','poopCount','digestionMs','treatmentMs','neglectMs','dailyMask','dailyClaims','careTotal','badges'])delete legacy[key];
  const upgraded=normalizeState(legacy,NOW).state;assert.equal(upgraded.sick,false);assert.deepEqual(upgraded.digestionMs,[0,0,0]);assert.equal(upgraded.dailyMask,0);
});

test('battery controls are explicitly desktop data and persist within bounded values',()=>{
  const original=freeze(createState(NOW));
  const changed=dispatch(original,{type:'SET_PREVIEW_BATTERY',percent:17,charging:false});
  assert.equal(changed.ok,true);assert.equal(changed.event.simulated,true);assert.equal(changed.state.batteryPercent,17);assert.equal(changed.state.batteryCharging,false);
  assert.equal(original.batteryPercent,85);
  const saved=normalizeState(JSON.parse(JSON.stringify(changed.state)),NOW).state;
  assert.equal(saved.batteryPercent,17);assert.equal(saved.batteryCharging,false);
  assert.equal(dispatch(saved,{type:'SET_PREVIEW_BATTERY',percent:500}).state.batteryPercent,100);
});

test('care timers and digestion agree between a long advance and fine-grained ticks',()=>{
  const initial=createState(NOW);initial.stats.fullness=10.2;initial.stats.cleanliness=21;initial.stats.energy=12;initial.digestionMs=[11_250,12345,0];
  const duration=3*3_600_000;
  const fast=advanceState(initial,duration,NOW+duration);let slow=initial;
  for(let ms=1000;ms<=duration;ms+=1000)slow=advanceState(slow,1000,NOW+ms);
  for(const key of Object.keys(fast.stats))assert.ok(Math.abs(fast.stats[key]-slow.stats[key])<1e-8,`${key}: ${fast.stats[key]} vs ${slow.stats[key]}`);
  assert.equal(fast.sick,slow.sick);assert.equal(fast.sicknessCause,slow.sicknessCause);assert.equal(fast.poopCount,slow.poopCount);
  for(let index=0;index<3;index++)assert.ok(Math.abs(fast.neglectMs[index]-slow.neglectMs[index])<1e-5);
});
