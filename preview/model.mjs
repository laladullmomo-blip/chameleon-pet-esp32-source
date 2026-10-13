// Shared, dependency-free rules for the desktop chameleon preview.
// Sensor actions are explicit preview inputs; they do not read real hardware.
export const SAVE_VERSION = 1;
export const FEED_DURATION_MS = 1400;
export const GAME_DURATION_MS = 20_000;
export const MAX_ADVANCE_MS = 6 * 60 * 60 * 1000;
export const MAX_PHOTOS = 20;

export const SCENES = Object.freeze([
  Object.freeze({ id: 'forest', name: '蘑菇森林' }),
  Object.freeze({ id: 'clearing', name: '林间空地' }),
  Object.freeze({ id: 'night', name: '夜间森林' }),
  Object.freeze({ id: 'meadow', name: '蓝天蘑菇草地' }),
  Object.freeze({ id: 'meadow-night', name: '月夜蘑菇草地' }),
]);

export const CATALOGUE = Object.freeze({
  cricket: Object.freeze({ id: 'cricket', name: '蟋蟀', type: 'food', price: 6, fullness: 25, mood: 3, description: '伸舌捕捉的小虫，补充饱腹。' }),
  cockroach: Object.freeze({ id: 'cockroach', name: '蟑螂', type: 'food', price: 7, fullness: 30, mood: 2, description: '饱腹感十足的小虫，用舌头捉回来。' }),
  mealworm: Object.freeze({ id: 'mealworm', name: '面包虫', type: 'food', price: 5, fullness: 18, mood: 6, description: '软软的小虫，卷卷很喜欢。' }),
  fly: Object.freeze({ id: 'fly', name: '苍蝇', type: 'food', price: 3, fullness: 12, mood: 4, description: '飞来飞去的小虫，捕捉起来很有趣。' }),
  medicine: Object.freeze({ id: 'medicine', name: '药水', type: 'medicine', price: 12, health: 20, description: '病中用药，喂饱、清洁、休息后慢慢康复。' }),
  soap: Object.freeze({ id: 'soap', name: '泡泡皂', type: 'cleaning', price: 8, cleanliness: 35, description: '洗个澡，恢复清洁和心情。' }),
  ball: Object.freeze({ id: 'ball', name: '彩色球', type: 'toy', price: 15, description: '用于玩耍，使用后保留。' }),
});

const STAT_KEYS = ['health', 'fullness', 'energy', 'mood', 'cleanliness'];
const COUNTER_KEYS = ['meals', 'strokes', 'baths', 'medicine', 'games', 'hits', 'walks', 'shakes', 'photos', 'cleanups', 'recoveries'];
const SCENE_IDS = new Set(SCENES.map(scene => scene.id));
const MAX_COINS = 999_999;
const MAX_HISTORY = 64;
const MAX_INVENTORY = 99;
const clamp = (value, low, high) => Math.max(low, Math.min(high, value));
const finite = (value, fallback) => typeof value === 'number' && Number.isFinite(value) ? value : fallback;
const integer = (value, fallback, low, high) => Math.floor(clamp(finite(value, fallback), low, high));
const timestamp = (value, fallback) => integer(value, fallback, 0, Number.MAX_SAFE_INTEGER);
const validId = value => typeof value === 'string' && value.length > 0 && value.length <= 80;
const name = value => typeof value === 'string' && value.trim() ? [...value.replace(/[\r\n\t]/g, '').trim()].slice(0, 12).join('') : '卷卷';
const catalogueItem = id => Object.hasOwn(CATALOGUE, id) ? CATALOGUE[id] : null;
export const DIGESTION_MS = 120_000;
export const TREATMENT_MS = 30_000;
export const DAILY_REWARD = 10;
export const DAILY_ALL = 7;
// Real care progresses over hours; minute-scale animations are not care time.
export const NEGLECT_MS = Object.freeze([60 * 60_000, 90 * 60_000, 120 * 60_000]);
const HOUR_MS = 3_600_000;
export const GROWTH_TIMES = Object.freeze({
  normal: Object.freeze({ teen: 7 * 24 * HOUR_MS, adult: 14 * 24 * HOUR_MS }),
  fast: Object.freeze({ teen: 30 * 60_000, adult: 120 * 60_000 }),
});
const growthSpeed = value => value === 'fast' ? 'fast' : 'normal';
const growthTime = state => clamp(finite(state?.growthMs, 0), 0, GROWTH_TIMES.normal.adult);
export function getGrowthInfo(state) {
  const speed = growthSpeed(state?.growthSpeed), times = GROWTH_TIMES[speed], elapsed = growthTime(state);
  const id = elapsed >= times.adult ? 'adult' : elapsed >= times.teen ? 'teen' : 'baby';
  const start = id === 'teen' ? times.teen : 0, end = id === 'baby' ? times.teen : times.adult;
  const stats = state?.stats ?? {};
  const pauseReason = id === 'adult' ? null : state?.sick ? 'sick' : finite(stats.health, 0) < 50 ? 'health'
    : finite(stats.fullness, 0) <= 25 ? 'food' : finite(stats.cleanliness, 0) <= 30 ? 'dirty'
    : finite(state?.poopCount, 0) >= 3 ? 'poop' : finite(stats.energy, 0) <= 15 ? 'rest' : null;
  return { id, label: { baby: '幼年', teen: '少年', adult: '成年' }[id],
    progress: id === 'adult' ? 1 : clamp((elapsed - start) / (end - start), 0, 1),
    nextLabel: id === 'baby' ? '少年' : id === 'teen' ? '成年' : null,
    remainingMs: id === 'adult' ? 0 : Math.ceil(end - elapsed), paused: pauseReason !== null, pauseReason, speed };
}
const CAUSES = new Set(['none', 'hunger', 'dirty', 'exhaustion', 'weakness']);
const causeLabels = { hunger: '长期饥饿', dirty: '环境脏乱', exhaustion: '过度疲劳', weakness: '身体虚弱' };
export const BADGES = Object.freeze([
  Object.freeze({mask:1, threshold:10, name:'照顾新伙伴'}),
  Object.freeze({mask:2, threshold:50, name:'森林好朋友'}),
  Object.freeze({mask:4, threshold:100, name:'贴心守护者'}),
]);
export function localDayKey(date = new Date()) {
  if (!(date instanceof Date) || !Number.isFinite(date.getTime())) return 0;
  return date.getFullYear() * 10000 + (date.getMonth() + 1) * 100 + date.getDate();
}
export function syncDay(state, date = new Date()) {
  const key = localDayKey(date);
  if(!state)return state;
  const verified=!!key&&(!state.dayKey||key>=state.dayKey);
  if(!verified)return state.dayVerified===false?state:{...state,dayVerified:false};
  if(key===state.dayKey)return state.dayVerified===true?state:{...state,dayVerified:true};
  return {...state, dayKey:key, dayVerified:true, dailyMask:0, dailyClaimed:false};
}
function recordCare(state, bit) {
  state.careTotal = clamp(state.careTotal + 1, 0, 1_000_000);
  if (state.dayVerified && state.dayKey) state.dailyMask |= bit;
  for (const badge of BADGES) if (state.careTotal >= badge.threshold) state.badges |= badge.mask;
}
function updateIllness(state) {
  if (state.sick) return;
  const cause = state.neglectMs[0] >= NEGLECT_MS[0]-1e-6 ? 'hunger'
    : state.neglectMs[1] >= NEGLECT_MS[1]-1e-6 ? 'dirty'
    : state.neglectMs[2] >= NEGLECT_MS[2]-1e-6 ? 'exhaustion'
    : state.stats.health < 40 ? 'weakness' : 'none';
  if (cause !== 'none') { state.sick = true; state.sicknessCause = cause; state.treatmentMs = 0; }
}
export function getCareNeeds(state) {
  if (!state) return [];
  const needs = [];
  if (state.sick) needs.push({id:'sick',label:`生病：${causeLabels[state.sicknessCause] || '需要照顾'}${state.treatmentMs ? ' · 康复中' : ' · 需要药水'}`,page:'health'});
  if (state.stats.fullness <= 25) needs.push({id:'hungry',label:state.stats.fullness <= 10 ? '非常饿了，先喂一只小虫' : '肚子空了，来一只小虫吧',page:'feed'});
  if (state.poopCount) needs.push({id:'poop',label:`有 ${state.poopCount} 堆便便，清理一下吧`,page:'clean'});
  if (state.stats.cleanliness <= 30) needs.push({id:'dirty',label:'身上脏了，需要泡泡浴',page:'clean'});
  if (state.stats.energy <= 20) needs.push({id:'tired',label:'有点累了，睡一会儿吧',page:'status'});
  if (state.stats.mood <= 35) needs.push({id:'lonely',label:'想你陪陪它，摸摸小脑袋吧',page:'status'});
  return needs;
}
export function illnessLabel(state) { return state.sick ? causeLabels[state.sicknessCause] || '需要照顾' : '身体健康'; }
export function dailyReady(state) { return state.dayVerified===true && !!state.dayKey && (state.dailyMask & DAILY_ALL) === DAILY_ALL && !state.dailyClaimed; }

function settingsFrom(raw = {}) {
  return {
    name: name(raw.name),
    sound: typeof raw.sound === 'boolean' ? raw.sound : true,
    brightness: integer(raw.brightness, 80, 10, 100),
    idleMinutes: integer(raw.idleMinutes, 1, 0, 60),
    autoLife: typeof raw.autoLife === 'boolean' ? raw.autoLife : true,
    autoWorld: typeof raw.autoWorld === 'boolean' ? raw.autoWorld : true,
  };
}

export function createState(now = Date.now(), selectedGrowthSpeed = 'normal') {
  const time = timestamp(now, Date.now());
  return {
    version: SAVE_VERSION,
    createdAt: time,
    updatedAt: time,
    stats: { health: 82, fullness: 70, energy: 65, mood: 90, cleanliness: 80 },
    coins: 30,
    inventory: { cricket: 3, cockroach: 3, mealworm: 2, fly: 4, medicine: 1, soap: 2, ball: 1 },
    scene: 'forest',
    awakeScene: 'forest',
    sleeping: false,
    sleepReason: null,
    settings: settingsFrom(),
    activity: null,
    game: null,
    reaction: null,
    counters: Object.fromEntries(COUNTER_KEYS.map(key => [key, 0])),
    album: [],
    completedIds: [],
    batteryPercent:85, batteryCharging:true,
    sceneLocked: false, sick: false, sicknessCause: 'none', treatmentMs: 0,
    poopCount: 0, digestionMs: [0,0,0], neglectMs: [0,0,0],
    careTotal: 0, dayKey: localDayKey(new Date(time)), dayVerified:true, dailyMask: 0,
    dailyClaimed: false, dailyClaims: 0, badges: 0,
    growthMs: 0, growthSpeed: growthSpeed(selectedGrowthSpeed),
  };
}

/**
 * Loading never advances time or resumes unfinished games/meals. A meal only
 * consumes stock when finished, so cancelling an interrupted meal loses nothing.
 * Preserve the raw save when status === 'unsupported'; state is deliberately null.
 */
export function normalizeState(raw, now = Date.now()) {
  if (raw == null) return { state: createState(now), status: 'new' };
  if (typeof raw !== 'object' || Array.isArray(raw)) return { state: createState(now), status: 'invalid' };
  if (Number.isInteger(raw.version) && raw.version !== SAVE_VERSION) return { state: null, status: 'unsupported' };
  if (raw.version !== SAVE_VERSION) return { state: createState(now), status: 'invalid' };
  const state = createState(now);
  state.createdAt = timestamp(raw.createdAt, state.createdAt);
  state.updatedAt = timestamp(raw.updatedAt, state.updatedAt);
  for (const key of STAT_KEYS) state.stats[key] = clamp(finite(raw.stats?.[key], state.stats[key]), 0, 100);
  state.coins = integer(raw.coins, state.coins, 0, MAX_COINS);
  for (const key of Object.keys(CATALOGUE)) state.inventory[key] = integer(raw.inventory?.[key], state.inventory[key], 0, MAX_INVENTORY);
  state.scene = SCENE_IDS.has(raw.scene) ? raw.scene : 'forest';
  state.awakeScene = SCENE_IDS.has(raw.awakeScene) ? raw.awakeScene : state.scene;
  state.sleeping = raw.sleeping === true;
  state.sleepReason = state.sleeping
    ? (['manual', 'routine', 'nap'].includes(raw.sleepReason) ? raw.sleepReason : 'manual') : null;
  state.settings = settingsFrom(raw.settings && typeof raw.settings === 'object' ? raw.settings : {});
  state.batteryPercent=integer(raw.batteryPercent,85,0,100);state.batteryCharging=typeof raw.batteryCharging==='boolean'?raw.batteryCharging:true;
  state.sceneLocked = raw.sceneLocked === true || state.settings.autoWorld === false;
  state.sick = raw.sick === true || state.stats.health < 40;
  state.sicknessCause = state.sick ? (CAUSES.has(raw.sicknessCause) && raw.sicknessCause !== 'none' ? raw.sicknessCause : 'weakness') : 'none';
  state.treatmentMs = state.sick ? integer(raw.treatmentMs,0,0,TREATMENT_MS) : 0;
  state.poopCount = integer(raw.poopCount,0,0,3);
  state.digestionMs = Array.from({length:3},(_,index)=>integer(raw.digestionMs?.[index],0,0,DIGESTION_MS));
  let pending = 3 - state.poopCount;
  state.digestionMs = state.digestionMs.map(value=>value && pending-- > 0 ? value : 0);
  state.neglectMs = Array.from({length:3},(_,index)=>integer(raw.neglectMs?.[index],0,0,NEGLECT_MS[index]));
  state.careTotal = integer(raw.careTotal,0,0,1_000_000);
  state.dayKey = integer(raw.dayKey,state.dayKey,0,99991231);state.dayVerified=false;
  state.dailyMask = integer(raw.dailyMask,0,0,DAILY_ALL);
  state.dailyClaimed = raw.dailyClaimed === true;
  state.dailyClaims = integer(raw.dailyClaims,0,0,1_000_000);
  state.growthMs = integer(raw.growthMs,0,0,GROWTH_TIMES.normal.adult);
  state.growthSpeed = growthSpeed(raw.growthSpeed);
  state.badges = integer(raw.badges,0,0,7);
  for (const badge of BADGES) if (state.careTotal >= badge.threshold) state.badges |= badge.mask;
  for (const key of COUNTER_KEYS) state.counters[key] = integer(raw.counters?.[key], 0, 0, 1_000_000);
  state.completedIds = Array.isArray(raw.completedIds) ? [...new Set(raw.completedIds.filter(value => typeof value === 'string' && value.length <= 90))].slice(-MAX_HISTORY) : [];
  state.album = Array.isArray(raw.album) ? raw.album.filter(photo => photo && validId(photo.id) && SCENE_IDS.has(photo.scene)).slice(-MAX_PHOTOS).map(photo => ({
    id: photo.id,
    createdAt: timestamp(photo.createdAt, state.updatedAt),
    scene: photo.scene,
    name: name(photo.name),
    pose: ['idle', 'happy', 'sleep', 'sick', 'angry', 'walk', 'dizzy', 'eat'].includes(photo.pose) ? photo.pose : 'idle',
    stats: Object.fromEntries(STAT_KEYS.map(key => [key, clamp(finite(photo.stats?.[key], state.stats[key]), 0, 100)])),
  })) : [];
  updateIllness(state);return { state, status: 'loaded' };
}

function copyState(state) {
  return {
    ...state,
    stats: { ...state.stats },
    digestionMs: [...state.digestionMs], neglectMs: [...state.neglectMs],
    inventory: { ...state.inventory },
    settings: { ...state.settings },
    counters: { ...state.counters },
    album: state.album.map(photo => ({ ...photo, stats: { ...photo.stats } })),
    completedIds: [...state.completedIds],
    activity: state.activity ? { ...state.activity } : null,
    game: state.game ? { ...state.game, hitIds: [...state.game.hitIds] } : null,
    reaction: state.reaction ? { ...state.reaction } : null,
  };
}

function changeStats(state, changes) {
  for (const [key, amount] of Object.entries(changes)) state.stats[key] = clamp(state.stats[key] + amount, 0, 100);
}

function count(state, key, amount = 1) {
  state.counters[key] = clamp(state.counters[key] + amount, 0, 1_000_000);
}

function remember(state, kind, id) {
  state.completedIds.push(`${kind}:${id}`);
  state.completedIds = state.completedIds.slice(-MAX_HISTORY);
}

function used(state, kind, id) {
  return state.completedIds.includes(`${kind}:${id}`);
}

function finishGame(state, now) {
  const game = state.game;
  if (!game) return null;
  const reward = game.hits * 2;
  state.coins = clamp(state.coins + reward, 0, MAX_COINS);
  changeStats(state, { mood: Math.min(10, game.hits * 2) });
  count(state, 'games');
  if (game.hits > 0) recordCare(state,4);
  count(state, 'hits', game.hits);
  remember(state, 'game', game.id);
  state.game = null;
  state.reaction = { type: game.hits ? 'happy' : 'idle', until: now + 1600 };
  return { type: 'game_end', id: game.id, hits: game.hits, reward };
}

export function advanceState(state, elapsedMs, now) {
  if (!state || state.version !== SAVE_VERSION) return state;
  const elapsed = clamp(finite(elapsedMs, 0), 0, MAX_ADVANCE_MS);
  const next = copyState(state);
  const time = Math.max(state.updatedAt, timestamp(now, state.updatedAt + elapsed));
  updateIllness(next);
  // Integrate to every care threshold or timer boundary. The board uses the
  // same event boundaries, so fast-forward and small UI ticks agree.
  const thresholds=NEGLECT_MS;
  const boundaries={health:[0,40,50,100],fullness:[0,10,15,25,100],energy:[0,10,15,100],mood:[0,100],cleanliness:[0,20,30,100]};
  const below=(value,threshold,rate)=>value<threshold-1e-9 || Math.abs(value-threshold)<=1e-9&&rate<0;
  for(let remaining=elapsed;remaining>1e-7;) {
    // Rates below are points per hour, shared with the board model.
    const fullnessRate=next.sleeping?-2:-6;
    const cleanlinessRate=(next.sleeping?-1:-3)-next.poopCount*.5,energyRate=next.sleeping?20:-5;
    const hungry=below(next.stats.fullness,15,fullnessRate),dirty=below(next.stats.cleanliness,20,cleanlinessRate);
    const neglected=[below(next.stats.fullness,10,fullnessRate)||next.stats.fullness===10,
      dirty||next.stats.cleanliness===20||next.poopCount===3,
      energyRate<=0&&(below(next.stats.energy,10,energyRate)||next.stats.energy===10)];
    const rested=next.stats.energy>15 || Math.abs(next.stats.energy-15)<=1e-9&&energyRate>0;
    const recovering=next.sick&&next.treatmentMs>0&&next.stats.fullness>25&&next.stats.cleanliness>30&&next.poopCount<3&&rested;
    const rates={health:(hungry?-1.5:0)+(dirty?-1:0)+(next.poopCount===3?-.5:0)+(next.sick?-2:next.sleeping&&!hungry&&!dirty?.5:0),
      fullness:fullnessRate,energy:energyRate,mood:next.sleeping?1:-3,cleanliness:cleanlinessRate};
    if(!next.sick&&next.stats.health<=40+1e-9&&rates.health<0){next.sick=true;next.sicknessCause='weakness';rates.health-=2;}
    let step=remaining;
    const cut=until=>{if(until>1e-7&&until<step)step=until;};
    for(const key of STAT_KEYS)for(const boundary of boundaries[key])if(rates[key])cut((boundary-next.stats[key])/rates[key]*HOUR_MS);
    for(let index=0;index<3;index++){
      if(next.digestionMs[index]>0)cut(next.digestionMs[index]);
      if(!next.sick&&neglected[index])cut(thresholds[index]-next.neglectMs[index]);
    }
    if(recovering)cut(next.treatmentMs);
    const growthTimes=GROWTH_TIMES[growthSpeed(next.growthSpeed)];
    const growing=!next.sick&&next.stats.health>=50-1e-9&&next.stats.fullness>25+1e-9
      &&next.stats.cleanliness>30+1e-9&&next.poopCount<3&&rested&&next.growthMs<growthTimes.adult;
    if(growing){
      cut(next.growthMs<growthTimes.teen?growthTimes.teen-next.growthMs:growthTimes.adult-next.growthMs);
      next.growthMs=Math.min(growthTimes.adult,next.growthMs+step);
    }
    changeStats(next,Object.fromEntries(STAT_KEYS.map(key=>[key,rates[key]*step/HOUR_MS])));
    for(let index=0;index<3;index++){
      next.neglectMs[index]=neglected[index]?Math.min(thresholds[index],next.neglectMs[index]+step):0;
      if(next.digestionMs[index]>0){
        next.digestionMs[index]=Math.max(0,next.digestionMs[index]-step);
        if(next.digestionMs[index]<=1e-7){next.digestionMs[index]=0;next.poopCount=Math.min(3,next.poopCount+1);}
      }
    }
    if(recovering){
      next.treatmentMs=Math.max(0,next.treatmentMs-step);
      if(next.treatmentMs<=1e-7){
        next.treatmentMs=0;next.sick=false;next.sicknessCause='none';next.neglectMs=[0,0,0];
        next.stats.health=Math.max(45,next.stats.health);count(next,'recoveries');
      }
    }
    remaining-=step;updateIllness(next);
  }
  next.updatedAt = time;
  if (next.reaction && next.reaction.until <= time) next.reaction = null;
  if (next.game && time - next.game.startedAt >= next.game.durationMs) finishGame(next, time);
  return next;
}

/** Returns {state, ok, reason, event}. Failed actions never mutate the input. */
export function reduceAction(state, action, now = Date.now()) {
  if (!state || state.version !== SAVE_VERSION) return { state, ok: false, reason: 'unsupported_save', event: null };
  if (!action || typeof action.type !== 'string') return { state, ok: false, reason: 'invalid_action', event: null };
  const next = copyState(state);
  const time = Math.max(state.updatedAt, timestamp(now, state.updatedAt));
  updateIllness(next);
  const fail = reason => ({ state, ok: false, reason, event: null });
  const succeed = event => {
    next.updatedAt = time;
    return { state: next, ok: true, reason: null, event: event ?? { type: action.type.toLowerCase() } };
  };
  const busy = () => next.activity || next.game;
  const awakeReady = () => next.sleeping ? 'sleeping' : busy() ? 'busy' : null;

  switch (action.type) {
    case 'FEED_START': {
      const blocked = awakeReady();
      if (blocked) return fail(blocked);
      if (!validId(action.id)) return fail('invalid_id');
      if (used(next, 'feed', action.id)) return fail('already_finished');
      const item = catalogueItem(action.item);
      if (!item || item.type !== 'food') return fail('invalid_food');
      if (next.inventory[action.item] <= 0) return fail('out_of_stock');
      if (next.stats.fullness >= 100) return fail('already_full');
      next.activity = { type: 'feed', id: action.id, item: action.item, startedAt: time, durationMs: FEED_DURATION_MS };
      next.reaction = null;
      return succeed({ type: 'feed_start', item: action.item, id: action.id });
    }
    case 'FEED_FINISH': {
      if (!next.activity || next.activity.id !== action.id) return fail('no_matching_feed');
      if (time - next.activity.startedAt < next.activity.durationMs) return fail('animation_pending');
      const item = catalogueItem(next.activity.item);
      if (next.sleeping || !item || next.inventory[item.id] <= 0) return fail(next.sleeping ? 'sleeping' : 'out_of_stock');
      next.inventory[item.id]--;
      changeStats(next, { fullness: item.fullness, mood: item.mood });
      count(next, 'meals'); recordCare(next,1);
      if (next.poopCount + next.digestionMs.filter(value=>value>0).length < 3) {
        const slot = next.digestionMs.findIndex(value=>value===0);
        if (slot>=0) next.digestionMs[slot]=DIGESTION_MS;
      }
      remember(next, 'feed', action.id);
      next.activity = null;
      next.reaction = { type: 'happy', until: time + 1600 };
      return succeed({ type: 'feed_finish', item: item.id, id: action.id });
    }
    case 'FEED_CANCEL':
      if (!next.activity || next.activity.id !== action.id) return fail('no_matching_feed');
      next.activity = null;
      return succeed();
    case 'PET':
      if (busy()) return fail('busy');
      changeStats(next, { mood: next.sleeping ? 2 : 6 });
      count(next, 'strokes');
      if (!next.sleeping) next.reaction = { type: 'happy', until: time + 1600 };
      return succeed();
    case 'CLEAN': {
      const blocked = awakeReady();
      if (blocked) return fail(blocked);
      if (next.inventory.soap <= 0) return fail('out_of_stock');
      if (next.stats.cleanliness >= 100) return fail('already_clean');
      next.inventory.soap--;
      changeStats(next, { cleanliness: CATALOGUE.soap.cleanliness, mood: 3 });
      count(next, 'baths');
      next.reaction = { type: 'happy', until: time + 1800 };
      return succeed();
    }
    case 'CLEAR_POOP': {
      if (busy()) return fail('busy');
      if (!next.poopCount) return fail('no_poop');
      const cleared = next.poopCount; next.poopCount = 0; next.neglectMs[1]=0;
      changeStats(next,{cleanliness:8,mood:2}); count(next,'cleanups'); recordCare(next,2);
      if(!next.sleeping)next.reaction={type:'happy',until:time+1200};
      return succeed({type:'clear_poop',cleared});
    }
    case 'CLAIM_DAILY':
      if (!dailyReady(next)) return fail(next.dailyClaimed?'daily_claimed':'daily_incomplete');
      if (action.dayKey !== next.dayKey) return fail('day_unavailable');
      next.coins = clamp(next.coins+DAILY_REWARD,0,MAX_COINS);
      next.dailyClaimed=true; next.dailyClaims=clamp(next.dailyClaims+1,0,1_000_000);
      changeStats(next,{mood:3});if(!next.sleeping&&!busy())next.reaction={type:'happy',until:time+1600};
      return succeed({type:'daily_claim',reward:DAILY_REWARD});
    case 'MEDICINE': {
      const blocked = awakeReady();
      if (blocked) return fail(blocked);
      if (!next.sick) return fail('already_healthy');
      if (next.treatmentMs > 0) return fail('treatment_pending');
      if (next.inventory.medicine <= 0) return fail('out_of_stock');
      next.inventory.medicine--;
      changeStats(next, { health: CATALOGUE.medicine.health });
      count(next, 'medicine');
      next.treatmentMs = TREATMENT_MS;
      next.reaction = null;
      return succeed();
    }
    case 'SLEEP':
      if (next.sleeping) return fail('already_sleeping');
      if (busy()) return fail('busy');
      next.awakeScene = next.scene;
      next.sleepReason = ['routine', 'nap'].includes(action.reason) ? action.reason : 'manual';
      next.sleeping = true;
      next.reaction = null;
      return succeed();
    case 'WAKE':
      if (!next.sleeping) return fail('already_awake');
      next.sleeping = false;
      next.sleepReason = null;
      next.reaction = { type: 'happy', until: time + 1200 };
      return succeed();
    case 'GAME_START': {
      const blocked = awakeReady();
      if (blocked) return fail(blocked);
      if (!validId(action.id)) return fail('invalid_id');
      if (used(next, 'game', action.id)) return fail('already_finished');
      if (next.sick) return fail('sick_rest');
      if (next.stats.fullness<=10) return fail('starving');
      if (next.inventory.ball <= 0) return fail('need_toy');
      if (next.stats.energy < 10) return fail('tired');
      changeStats(next, { energy: -5 });
      next.game = { id: action.id, startedAt: time, durationMs: GAME_DURATION_MS, hits: 0, target: 1, lastHitAt: null, hitIds: [] };
      next.reaction = null;
      return succeed({ type: 'game_start', id: action.id });
    }
    case 'GAME_HIT': {
      if (!next.game || next.game.id !== action.id) return fail('no_matching_game');
      if (time - next.game.startedAt >= next.game.durationMs) return fail('game_over');
      if (!validId(action.hitId)) return fail('invalid_id');
      if (next.game.hitIds.includes(action.hitId)) return fail('already_finished');
      if (!Number.isInteger(action.target) || action.target !== next.game.target) return fail('wrong_target');
      if (next.game.lastHitAt !== null && time - next.game.lastHitAt < 250) return fail('too_fast');
      if (next.game.hits >= 40) return fail('game_limit');
      next.game.hits++;
      next.game.hitIds.push(action.hitId);
      next.game.lastHitAt = time;
      next.game.target = (next.game.target + (next.game.hits % 2 ? 1 : 2)) % 3;
      return succeed({ type: 'game_hit', hits: next.game.hits, target: next.game.target });
    }
    case 'GAME_END':
      if (!next.game || next.game.id !== action.id) return fail('no_matching_game');
      return succeed(finishGame(next, time));
    case 'BUY': {
      const item = catalogueItem(action.item);
      if (!item) return fail('invalid_item');
      const quantity = action.quantity ?? 1;
      if (!Number.isInteger(quantity) || quantity < 1 || quantity > MAX_INVENTORY) return fail('invalid_quantity');
      if (action.id != null && !validId(action.id)) return fail('invalid_id');
      if (action.id && used(next, 'buy', action.id)) return fail('already_finished');
      if (next.inventory[item.id] + quantity > MAX_INVENTORY) return fail('inventory_full');
      const cost = item.price * quantity;
      if (next.coins < cost) return fail('not_enough_coins');
      next.coins -= cost;
      next.inventory[item.id] += quantity;
      if (action.id) remember(next, 'buy', action.id);
      return succeed({ type: 'buy', item: item.id, quantity, cost });
    }
    case 'WALK': {
      const blocked = awakeReady();
      if (blocked) return fail(blocked);
      const cost = 2;
      if (next.stats.energy < cost) return fail('tired');
      changeStats(next, { energy: -cost, mood: 2 });
      count(next, 'walks');
      next.reaction = { type: 'walk', until: time + 2200 };
      return succeed();
    }
    case 'SHAKE': {
      const blocked = awakeReady();
      if (blocked) return fail(blocked);
      if (!['gentle', 'strong'].includes(action.strength)) return fail('invalid_strength');
      const strong = action.strength === 'strong';
      changeStats(next, strong ? { mood: -5, energy: -2 } : { mood: 3 });
      count(next, 'shakes');
      next.reaction = { type: strong ? 'dizzy' : 'happy', until: time + (strong ? 2500 : 1600) };
      return succeed({ type: 'shake', simulated: true, strength: action.strength });
    }
    case 'SET_SCENE':
      if (!SCENE_IDS.has(action.scene)) return fail('invalid_scene');
      if (busy()) return fail('busy');
      next.scene = action.scene;
      next.awakeScene = action.scene;
      next.sceneLocked=true; next.settings.autoWorld=false;
      return succeed();
    case 'SET_PREVIEW_BATTERY':
      next.batteryPercent=integer(action.percent,next.batteryPercent,0,100);
      if(typeof action.charging==='boolean')next.batteryCharging=action.charging;
      return succeed({type:'preview_battery',simulated:true});
    case 'SET_SETTINGS':
      if (!action.settings || typeof action.settings !== 'object' || Array.isArray(action.settings)) return fail('invalid_settings');
      next.settings = settingsFrom({ ...next.settings, ...action.settings });
      if (typeof action.settings.autoWorld==='boolean') next.sceneLocked=!action.settings.autoWorld;
      return succeed();
    case 'SET_GROWTH_SPEED':
      if (!['normal','fast'].includes(action.growthSpeed)) return fail('invalid_growth_speed');
      if (next.growthMs > 0) return fail('growth_started');
      next.growthSpeed=action.growthSpeed;
      return succeed();
    case 'TAKE_PHOTO':
      if (!validId(action.id)) return fail('invalid_id');
      if (used(next, 'photo', action.id) || next.album.some(photo => photo.id === action.id)) return fail('already_finished');
      next.album.push({ id: action.id, createdAt: time, scene: SCENE_IDS.has(action.scene) ? action.scene : next.scene, name: next.settings.name, pose: getCondition(next, time), stats: { ...next.stats } });
      next.album = next.album.slice(-MAX_PHOTOS);
      count(next, 'photos');
      remember(next, 'photo', action.id);
      return succeed({ type: 'photo', id: action.id });
    case 'DELETE_PHOTO': {
      if (!next.album.some(photo => photo.id === action.id)) return fail('photo_missing');
      next.album = next.album.filter(photo => photo.id !== action.id);
      return succeed();
    }
    default:
      return fail('invalid_action');
  }
}

export function getCondition(state, now = state?.updatedAt ?? Date.now()) {
  if (!state) return 'idle';
  if (state.sleeping) return 'sleep';
  if (state.activity?.type === 'feed') return 'eat';
  if (state.sick || state.stats.health < 40) return 'sick';
  if (state.reaction && state.reaction.until > now) return state.reaction.type;
  if (state.stats.mood < 35) return 'angry';
  return 'idle';
}

const REASONS = Object.freeze({
  unsupported_save: '这份存档属于其他版本，请先保留原存档。',
  invalid_growth_speed: '请选择正常成长或隔离体验的快速成长。', growth_started: '已经开始成长，保留当前成长速度。',
  invalid_action: '这个操作暂时不可用。', invalid_id: '操作编号无效，请重新操作。',
  sleeping: '卷卷正在睡觉，先把它叫醒吧。', busy: '等卷卷完成当前动作再试试。',
  already_finished: '这次操作已经完成了。', invalid_food: '变色龙只吃这里提供的小虫。',
  out_of_stock: '道具用完了，可以去背包商店补充。', already_full: '卷卷已经吃饱了。',
  no_matching_feed: '这次喂食已经结束或取消。', animation_pending: '等舌头把小虫捉回来。',
  already_clean: '卷卷已经很干净了。', already_healthy: '卷卷现在很健康，不需要用药。',
  already_sleeping: '卷卷已经睡着了。', already_awake: '卷卷已经醒了。',
  need_toy: '先准备一个彩色球再玩吧。', tired: '卷卷累了，先睡一会儿。',
  no_matching_game: '这局游戏已经结束。', game_over: '时间到啦，结算这一局吧。',
  wrong_target: '点亮着的目标才能得分。', too_fast: '稍等一下再点下一个目标。',
  game_limit: '这局已经捉到足够多的目标啦。', invalid_item: '没有这个道具。',
  invalid_quantity: '购买数量无效。', inventory_full: '这个道具的背包格已经满了。',
  not_enough_coins: '金币不足，玩小游戏可以赚金币。', invalid_strength: '请选择轻摇或快速摇晃。',
  no_poop:'森林很干净，暂时没有便便。', treatment_pending:'已经用过药了，喂饱、清洁、休息后会慢慢康复。', sick_rest:'卷卷正在生病，先治疗和休息吧。', starving:'卷卷非常饿，先喂食再玩吧。', daily_claimed:'今天的奖励已领取。', daily_incomplete:'先完成今天的喂食、清理和游戏目标。', day_unavailable:'日期尚未确认，暂时无法领取。',
  invalid_scene: '这个场景暂时不可用。', invalid_settings: '设置内容无效。', photo_missing: '找不到这张照片。',
});

export function formatActionReason(reason) {
  return Object.hasOwn(REASONS, reason) ? REASONS[reason] : '操作没有完成，请再试一次。';
}
