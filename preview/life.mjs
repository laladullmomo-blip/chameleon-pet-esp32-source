// Pure desktop / board behaviour rules. The controller changes no care values,
// supplies or money: its only model commands are SLEEP and WAKE.
export const WEATHER_MAX_AGE_MS = 45 * 60 * 1000;
export const NIGHT_WAKE_GRACE_MS = 30 * 60 * 1000;
export const MAX_NAP_MS = 60 * 60 * 1000;

const phases = new Set(['dawn', 'day', 'dusk', 'night']);
const weatherKinds = new Set(['clear', 'cloudy', 'rain', 'storm', 'fog', 'snow', 'unknown']);
const sceneIds = new Set(['forest', 'night', 'clearing', 'meadow', 'meadow-night']);
const finite = (value, fallback = 0) => typeof value === 'number' && Number.isFinite(value) ? value : fallback;
const clamp = (value, low, high) => Math.max(low, Math.min(high, value));
const isBranch = scene => scene === 'forest' || scene === 'night';
const phaseForHour = hour => hour >= 6 && hour < 8 ? 'dawn'
  : hour >= 8 && hour < 17 ? 'day' : hour >= 17 && hour < 20 ? 'dusk' : 'night';

function weatherKind(condition) {
  if (typeof condition !== 'string') return 'unknown';
  if (/雷|暴|storm|thunder/i.test(condition)) return 'storm';
  if (/雪|snow/i.test(condition)) return 'snow';
  if (/雨|rain|drizzle|shower/i.test(condition)) return 'rain';
  if (/雾|fog|mist/i.test(condition)) return 'fog';
  if (/云|阴|cloud|overcast/i.test(condition)) return 'cloudy';
  if (/晴|clear|sunny/i.test(condition)) return 'clear';
  return 'unknown';
}

/** Local clock drives phase; weather reacts only to a recent validated source.
 * Explicit overrides are visual test controls. hour / actualPhase always keep
 * the real local time, so moving a preview clock cannot reschedule sleep.
 */
export function getEnvironment(state = {}, {
  date = new Date(), weather = {}, phaseOverride = 'actual', weatherOverride = 'actual',
} = {}) {
  const validDate = date instanceof Date && Number.isFinite(date.getTime()) ? date : new Date();
  const hour = validDate.getHours();
  const actualPhase = phaseForHour(hour);
  const enabled = state.settings?.autoWorld !== false && state.sceneLocked !== true;
  const storedScene = sceneIds.has(state.scene) ? state.scene : 'forest';
  const timePreview = phases.has(phaseOverride);
  const weatherPreview = weatherKinds.has(weatherOverride);
  const phase = timePreview ? phaseOverride : enabled ? actualPhase
    : ['night', 'meadow-night'].includes(storedScene) ? 'night' : 'day';
  let scene = storedScene;
  if (enabled || timePreview) {
    // Manual scene choices survive sleeping and waking. Automatic worlds only
    // pair the selected habitat family; test overrides never change storage.
    const base = storedScene;
    if (['forest', 'night'].includes(base)) scene = phase === 'night' ? 'night' : 'forest';
    else if (['meadow', 'meadow-night'].includes(base)) scene = phase === 'night' ? 'meadow-night' : 'meadow';
    else scene = 'clearing';
  }
  const age = validDate.getTime() - finite(weather.updatedAt, -Infinity);
  const weatherFresh = weather.source === 'Open-Meteo' && weather.error == null && !weather.loading
    && typeof weather.temperature === 'number' && Number.isFinite(weather.temperature)
    && weather.temperature >= -100 && weather.temperature <= 100
    && typeof weather.isDay === 'boolean' && typeof weather.condition === 'string'
    && age >= -5 * 60 * 1000 && age <= WEATHER_MAX_AGE_MS;
  return {
    enabled, phase, actualPhase, hour, scene,
    weather: weatherPreview ? weatherOverride : enabled && weatherFresh ? weatherKind(weather.condition) : 'unknown',
    preview: timePreview || weatherPreview,
    timePreview, weatherPreview,
    weatherSource: weatherPreview ? 'preview' : weatherFresh ? 'Open-Meteo' : null,
    weatherFresh,
  };
}

const everyday = [
  { type: 'walk', duration: 2400, note: '沿着树枝慢慢走几步' },
  { type: 'stretch', duration: 1800, note: '伸个懒腰，舒展一下身体' },
  { type: 'blink', duration: 800, note: '眨眨眼，享受安静的小森林' },
  { type: 'wave', duration: 1600, note: '向你挥挥小爪子' },
  { type: 'idle', duration: 2600, note: '抬头看看飞过的小虫' },
];

/** now is monotonic milliseconds (performance.now on the desktop). Call this
 * only with active=true for home, clock, weather or scene camera. Pose actions
 * keep one start time while held; positionX is a rigid horizontal translation.
 */
export function createLifeController({ now = 0, random = Math.random } = {}) {
  let clock = Math.max(0, finite(now));
  const randomUnit = () => clamp(finite(random(), .5), 0, 1 - Number.EPSILON);
  const quietDuration = () => 8000 + randomUnit() * 4000;
  const interval = () => 8000 + randomUnit() * 8000;
  let quietMs = quietDuration(), lastInteraction = clock, seenExternalInteraction = clock;
  let nextActionAt = clock + quietMs, action = null, actionNote = '', mode = 'idle', positionX = 0;
  let walkFrom = 0, walkTo = 0, lastChoice = -1;
  let wakeGraceUntil = 0, napStartedAt = null, napGraceUntil = 0, pendingCommand = null;
  let lastSleeping = null, lastSleepReason = null;

  const clearPose = () => { action = null; actionNote = ''; };
  const currentPosition = time => {
    if (action?.type === 'walk') {
      const progress = clamp((time - action.start) / action.duration, 0, 1);
      positionX = clamp(walkFrom + (walkTo - walkFrom) * progress, -24, 24);
    }
  };
  const output = (note = '', command = null) => ({
    action: action ? { ...action } : null, command, positionX, note, mode,
  });
  const issueCommand = command => {
    pendingCommand = { ...command, sleepingBefore: lastSleeping, reasonBefore: lastSleepReason };
    return command;
  };
  function interaction(time = clock) {
    const stamp = Math.max(clock, finite(time, clock));
    currentPosition(stamp);
    clock = stamp;
    lastInteraction = stamp;
    seenExternalInteraction = Math.max(seenExternalInteraction, stamp);
    quietMs = quietDuration();
    nextActionAt = stamp + quietMs;
    wakeGraceUntil = stamp + NIGHT_WAKE_GRACE_MS;
    clearPose();
  }
  function reset(time = clock) {
    clock = Math.max(0, finite(time, clock));
    quietMs = quietDuration();
    lastInteraction = seenExternalInteraction = clock;
    nextActionAt = clock + quietMs;
    clearPose();
    mode = 'idle'; positionX = 0; walkFrom = walkTo = 0; lastChoice = -1;
    wakeGraceUntil = napGraceUntil = 0; napStartedAt = pendingCommand = null;
    lastSleeping = lastSleepReason = null;
  }
  function step({
    now: requestedNow = clock, state, environment = {}, manualBusy = false,
    active = true, lastInteractionAt = 0,
  } = {}) {
    const time = Math.max(clock, finite(requestedNow, clock));
    currentPosition(time);
    clock = time;
    const externalInteraction = finite(lastInteractionAt);
    if (externalInteraction > seenExternalInteraction) interaction(externalInteraction);
    if (!state) { clearPose(); mode = 'inactive'; return output(); }
    const sleeping = state.sleeping === true;
    const reason = sleeping ? (state.sleepReason || 'manual') : null;
    if (pendingCommand && (sleeping !== pendingCommand.sleepingBefore || reason !== pendingCommand.reasonBefore)) pendingCommand = null;
    if (lastSleeping && !sleeping) {
      if (lastSleepReason === 'nap') napGraceUntil = time + 30 * 60 * 1000;
      wakeGraceUntil = Math.max(wakeGraceUntil, time + NIGHT_WAKE_GRACE_MS);
    }
    if (sleeping && reason === 'nap' && (!lastSleeping || lastSleepReason !== 'nap' || napStartedAt == null)) napStartedAt = time;
    if (!sleeping) napStartedAt = null;
    lastSleeping = sleeping; lastSleepReason = reason;
    const reactionBusy = state.reaction && finite(state.reaction.until) > finite(state.updatedAt);
    const busy = manualBusy || !!state.activity || !!state.game || !!reactionBusy;
    const enabled = state.settings?.autoLife !== false;
    if (!active || busy) {
      clearPose(); mode = !active ? 'inactive' : 'busy';
      nextActionAt = Math.max(nextActionAt, time + quietMs);
      return output();
    }
    if (!enabled) {
      clearPose(); mode = 'off';
      // Disabling autonomous life only wakes sleeps owned by this controller.
      if (sleeping && reason !== 'manual' && !pendingCommand) return output('自主生活已暂停', issueCommand({ type: 'WAKE' }));
      return output('自主生活已暂停');
    }
    if (sleeping) {
      clearPose(); mode = 'sleep';
      const hour = finite(environment.hour, -1);
      const morning = environment.enabled !== false && hour >= 7 && hour < 21;
      const restedNap = reason === 'nap' && (finite(state.stats?.energy) >= 35 - 1e-9 || time - napStartedAt >= MAX_NAP_MS);
      if (!pendingCommand && ((reason === 'routine' && morning) || restedNap)) return output('休息好了，醒来看看森林', issueCommand({ type: 'WAKE' }));
      return output(reason === 'nap' ? '在原来的栖息地打个小盹' : '安静地睡觉，慢慢恢复精力');
    }
    const healthy = !state.sick && finite(state.stats?.health, 100) >= 40 && finite(state.stats?.fullness,100)>25 && finite(state.stats?.mood, 100) >= 35;
    if (!healthy) {
      clearPose(); mode = 'care';
      nextActionAt = Math.max(nextActionAt, time + quietMs);
      return output('先休息一下，等你来照顾');
    }
    const scene = environment.scene || state.scene;
    const quiet = time - lastInteraction >= quietMs;
    const hour = finite(environment.hour, -1);
    const nightRest = environment.enabled !== false && hour >= 0 && (hour >= 21 || hour < 7);
    if (quiet && !pendingCommand) {
      if (nightRest && time >= wakeGraceUntil) {
        clearPose(); mode = 'routine';
        return output('夜深了，在树枝上慢慢睡着', issueCommand({ type: 'SLEEP', reason: 'routine' }));
      }
      if (finite(state.stats?.energy, 100) < 18 && time >= napGraceUntil) {
        clearPose(); mode = 'nap';
        return output('有点困，在这里打个小盹', issueCommand({ type: 'SLEEP', reason: 'nap' }));
      }
    }
    if (['rain', 'storm'].includes(environment.weather)) {
      if (action?.type !== 'shelter') { clearPose(); action = { type: 'shelter', start: time, duration: 1800 }; }
      mode = 'shelter'; nextActionAt = time + quietMs;
      return output(environment.weather === 'storm' ? '雷雨来了，伏低身体小心避雨' : '下雨了，伏低身体静静避雨');
    }
    if (action?.type === 'shelter') { clearPose(); nextActionAt = time + quietMs; }
    if (action && time >= action.start + action.duration) {
      clearPose(); nextActionAt = time + interval();
    }
    if (!quiet) { clearPose(); mode = 'quiet'; return output('安静地陪着你'); }
    if (!action && time >= nextActionAt && !pendingCommand) {
      let choice = Math.floor(randomUnit() * everyday.length);
      if (choice === lastChoice) choice = (choice + 1) % everyday.length;
      lastChoice = choice;
      const pose = everyday[choice];
      action = { type: pose.type, start: time, duration: pose.duration };
      actionNote = pose.note;
      if (pose.type === 'walk') {
        walkFrom = positionX;
        const direction = positionX >= 12 ? -1 : positionX <= -12 ? 1 : randomUnit() < .5 ? -1 : 1;
        walkTo = clamp(walkFrom + direction * (12 + randomUnit() * 12), -24, 24);
        action.distance = walkTo - walkFrom;
        actionNote = isBranch(scene) ? pose.note : '在草地上慢慢走几步';
      }
    }
    mode = action ? action.type === 'idle' ? 'observe' : action.type : 'idle';
    return output(action ? actionNote : '静静地观察森林里的小动静');
  }
  return {
    step, interaction, reset,
    getSnapshot: () => ({ now: clock, positionX, mode, action: action ? { ...action } : null,
      nextActionAt, lastInteractionAt: lastInteraction, quietMs, wakeGraceUntil, napStartedAt,
      pendingCommand: pendingCommand ? { ...pendingCommand } : null }),
  };
}
