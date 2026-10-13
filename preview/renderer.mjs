/**
 * Shared 466 × 466 scene renderer for the desktop pet preview.
 * Rendering has no state mutations and schedules no animation loop of its own.
 */
import {validateStageCatalog,validateStageManifest} from './growth.mjs';
import {getGrowthInfo} from './model.mjs';
const SIZE = 466;
const BACKGROUND_PIXELS = 128;
const ASSET_NAMES = ['forest', 'clearing', 'night', 'meadow', 'meadow-night'];
const PHASES = new Set(['dawn', 'day', 'dusk', 'night']);
const WEATHER_KINDS = new Set(['clear', 'cloudy', 'rain', 'storm', 'fog', 'snow', 'unknown']);
const SCENES = {
  // These baselines match the top surfaces of the current background assets.
  forest: { sky: '#80c8df', floor: '#a4b866', shade: '#234d32', night: false, baseline: 280, perch: true },
  clearing: { sky: '#b5ddeb', floor: '#b3c17a', shade: '#476a44', night: false, baseline: 315, perch: false },
  night: { sky: '#2c4b70', floor: '#47634d', shade: '#334c58', night: true, baseline: 296, perch: true },
  meadow: { sky: '#b5ddeb', floor: '#b3c17a', shade: '#476a44', night: false, baseline: 315, perch: false },
  'meadow-night': { sky: '#2c4b70', floor: '#47634d', shade: '#334c58', night: true, baseline: 315, perch: false },
};

const clamp = (v, min = 0, max = 1) => Math.max(min, Math.min(max, v));
const ease = (p) => 1 - Math.pow(1 - clamp(p), 3);
const pixel = (n) => Math.round(n / 2) * 2;
const finite = (value, fallback) => Number.isFinite(Number(value)) ? Number(value) : fallback;

function cover(ctx, image) {
  const scale = Math.max(SIZE / image.naturalWidth, SIZE / image.naturalHeight);
  const w = image.naturalWidth * scale;
  const h = image.naturalHeight * scale;
  ctx.drawImage(image, pixel((SIZE - w) / 2), pixel((SIZE - h) / 2), pixel(w), pixel(h));
}

function blockLine(ctx, x1, y1, x2, y2, width, color) {
  ctx.fillStyle = color;
  const steps = Math.max(1, Math.ceil(Math.max(Math.abs(x2 - x1), Math.abs(y2 - y1)) / 2));
  for (let i = 0; i <= steps; i += 1) {
    const p = i / steps;
    ctx.fillRect(pixel(x1 + (x2 - x1) * p - width / 2),
      pixel(y1 + (y2 - y1) * p - width / 2), width, width);
  }
}

function heart(ctx, x, y, size = 4, color = '#f47583') {
  const rows = ['0110110', '1111111', '1111111', '0111110', '0011100', '0001000'];
  ctx.fillStyle = color;
  rows.forEach((row, dy) => [...row].forEach((cell, dx) => {
    if (cell === '1') ctx.fillRect(pixel(x + dx * size), pixel(y + dy * size), size, size);
  }));
}

function star(ctx, x, y, scale = 2, color = '#ffe48d') {
  ctx.fillStyle = color;
  const rows = ['00100', '00100', '11111', '01110', '01010'];
  rows.forEach((row, dy) => [...row].forEach((cell, dx) => {
    if (cell === '1') ctx.fillRect(pixel(x + dx * scale), pixel(y + dy * scale), scale, scale);
  }));
}

function leaf(ctx, x, y, color, scale = 1) {
  ctx.fillStyle = '#4f7042';
  ctx.fillRect(pixel(x + 2 * scale), pixel(y + 6 * scale), 12 * scale, 4 * scale);
  ctx.fillStyle = color;
  ctx.fillRect(pixel(x), pixel(y + 2 * scale), 10 * scale, 6 * scale);
  ctx.fillRect(pixel(x + 8 * scale), pixel(y), 8 * scale, 6 * scale);
}

function cloud(ctx, x, y, scale = 1, color = '#fbf4d8') {
  ctx.fillStyle = color;
  [[0, 12, 64, 14], [12, 4, 38, 16], [24, 0, 18, 14]].forEach(([dx, dy, w, h]) => {
    ctx.fillRect(pixel(x + dx * scale), pixel(y + dy * scale), w * scale, h * scale);
  });
}

// A colorful fallback remains visible before assets finish loading. loadAssets()
// still rejects missing assets so a broken delivery cannot be silently accepted.
function fallbackScene(ctx, scene) {
  const palette = SCENES[scene];
  const night = palette.night;
  ctx.fillStyle = palette.sky;
  ctx.fillRect(0, 0, SIZE, SIZE);
  if (night) {
    ctx.fillStyle = '#f2dea0';
    ctx.fillRect(333, 73, 25, 32);
    ctx.fillRect(327, 79, 37, 20);
    ctx.fillStyle = palette.sky;
    ctx.fillRect(346, 67, 26, 26);
    for (let i = 0; i < 7; i += 1) star(ctx, 110 + (i * 43) % 300, 50 + (i * 27) % 130, 1);
  } else {
    cloud(ctx, 121, 70, 1.05);
    cloud(ctx, 314, 147, .7);
  }
  ctx.fillStyle = night ? '#375d63' : '#8ab493';
  ctx.fillRect(0, 219, SIZE, 170);
  for (let i = 0; i < 10; i += 1) {
    ctx.fillStyle = night ? (i % 2 ? '#304b4e' : '#416958') : (i % 2 ? '#739b76' : '#63936e');
    const x = i * 59 - 25;
    ctx.fillRect(x, 194 + (i % 3) * 18, 44, 183);
    ctx.fillRect(x - 10, 218 + (i % 3) * 18, 65, 157);
  }
  const groundY = palette.perch ? 377 : 315;
  ctx.fillStyle = palette.floor;
  ctx.fillRect(0, groundY, SIZE, SIZE - groundY);
  ctx.fillStyle = night ? '#5a7255' : '#9eb566';
  ctx.fillRect(0, groundY + 29, SIZE, 35);
  ctx.fillStyle = night ? '#625644' : '#a1764e';
  ctx.fillRect(50, 20, 25, 371);
  ctx.fillRect(70, 37, 92, 17);
  for (let i = 0; i < 17; i += 1) {
    leaf(ctx, (i * 31) % 191, 7 + (i % 4) * 18,
      night ? (i % 2 ? '#345546' : '#426c59') : (i % 2 ? '#477e51' : '#6a9b54'), 2);
  }
  for (let i = 0; i < 38; i += 1) {
    const x = (i * 113 + 16) % SIZE;
    const y = groundY + 19 + (i * 37) % Math.max(1, SIZE - groundY - 19);
    ctx.fillStyle = night ? '#8d9970' : i % 3 ? '#d7dc87' : '#f2d88d';
    ctx.fillRect(x, y, 4, 5);
    ctx.fillStyle = night ? '#41674d' : '#5e8b4c';
    ctx.fillRect(x + 2, y + 5, 2, 6);
  }
  if (palette.perch) {
    const baseline = palette.baseline;
    ctx.fillStyle = night ? '#5a5040' : '#725137';
    ctx.fillRect(64, baseline, 328, 13);
    ctx.fillRect(66, baseline + 10, 201, 7);
    ctx.fillStyle = night ? '#7b7351' : '#b28b51';
    ctx.fillRect(66, baseline, 308, 4);
    ctx.fillStyle = night ? '#5f7653' : '#739153';
    ctx.fillRect(87, baseline - 4, 51, 4);
    ctx.fillRect(310, baseline - 4, 39, 4);
  }
  for (const [mx, my, scale] of [[69, 397, 1], [382, 387, 1.2], [358, 411, .8]]) {
    ctx.fillStyle = night ? '#b2bf9e' : '#e0d3a3';
    ctx.fillRect(pixel(mx - 3 * scale), pixel(my), 6 * scale, 13 * scale);
    ctx.fillStyle = night ? '#967588' : '#b76750';
    ctx.fillRect(pixel(mx - 12 * scale), pixel(my - 5 * scale), 24 * scale, 7 * scale);
    ctx.fillRect(pixel(mx - 8 * scale), pixel(my - 10 * scale), 16 * scale, 6 * scale);
    ctx.fillStyle = night ? '#d5cda8' : '#f0d5b0';
    ctx.fillRect(pixel(mx - 6 * scale), pixel(my - 6 * scale), 3 * scale, 3 * scale);
    ctx.fillRect(pixel(mx + 5 * scale), pixel(my - 4 * scale), 3 * scale, 3 * scale);
  }
}

function drawBug(ctx, x, y, time, type = 'cricket') {
  x = pixel(x); y = pixel(y);
  const flutter = Math.sin(time / 55) > 0;
  if (type === 'mealworm') {
    // Six separate amber segments and a dark head distinguish the mealworm.
    for (let i = 0; i < 6; i += 1) {
      const sx = x - 13 + i * 4;
      const sy = y + Math.sin(time / 180 + i * .7) * 2;
      ctx.fillStyle = '#80502f';
      ctx.fillRect(pixel(sx), pixel(sy - 4), 5, 8);
      ctx.fillStyle = i % 2 ? '#d9ae66' : '#efc782';
      ctx.fillRect(pixel(sx + 1), pixel(sy - 3), 3, 5);
    }
    ctx.fillStyle = '#5b402c';
    ctx.fillRect(x + 10, y - 3, 5, 6);
    ctx.fillStyle = '#fbefd0';
    ctx.fillRect(x + 13, y - 2, 2, 2);
    return;
  }
  if (type === 'cockroach') {
    ctx.fillStyle = '#4e362d';
    ctx.fillRect(x - 11, y - 4, 20, 9);
    ctx.fillRect(x - 7, y - 7, 12, 15);
    ctx.fillStyle = '#9b673d';
    ctx.fillRect(x - 7, y - 5, 10, 11);
    ctx.fillStyle = '#c79155';
    ctx.fillRect(x - 5, y - 4, 3, 8);
    ctx.fillStyle = '#563b2b';
    ctx.fillRect(x + 6, y - 4, 6, 8);
    for (const side of [-1, 1]) {
      for (let i = 0; i < 3; i += 1) {
        const lx = x - 6 + i * 6;
        blockLine(ctx, lx, y + side * 5, lx - 3, y + side * (10 + (flutter ? 1 : -1)), 2, '#634b35');
      }
      blockLine(ctx, x + 10, y + side * 2, x + 18, y + side * 8, 2, '#5b4736');
    }
    ctx.fillStyle = '#f6dca2';
    ctx.fillRect(x + 10, y - 3, 2, 2);
    return;
  }
  ctx.fillStyle = type === 'fly' ? '#3a4b4b' : '#334f37';
  ctx.fillRect(x - 6, y - 3, 13, 7);
  ctx.fillRect(x + 6, y - 5, 4, 7);
  ctx.fillStyle = type === 'fly' ? '#a6b8a7' : '#b5d184';
  ctx.fillRect(x - 4, y - 2, 8, 4);
  ctx.fillStyle = '#fff3c2';
  ctx.fillRect(x + 7, y - 4, 2, 2);
  if (type === 'fly') {
    ctx.fillStyle = '#789dac';
    ctx.fillRect(x - 6, y - (flutter ? 11 : 7), 7, 7);
    ctx.fillRect(x + 1, y - (flutter ? 7 : 11), 7, 7);
    ctx.fillStyle = '#e5f1e4';
    ctx.fillRect(x - 5, y - (flutter ? 10 : 6), 5, 5);
    ctx.fillRect(x + 2, y - (flutter ? 6 : 10), 5, 5);
    ctx.fillStyle = '#c87955';
    ctx.fillRect(x + 6, y - 4, 3, 3);
  } else {
    blockLine(ctx, x - 2, y + 3, x - 9, y + (flutter ? 12 : 8), 2, '#334f37');
    blockLine(ctx, x + 4, y + 3, x + 9, y + (flutter ? 8 : 12), 2, '#334f37');
    blockLine(ctx, x + 7, y - 4, x + 9, y - 10, 2, '#334f37');
  }
}

function actionProgress(action, time) {
  if (!action) return null;
  const start = finite(action.start, time);
  const defaultDuration = ['tongue', 'feed', 'eat'].includes(action.type) ? 1400 : 1700;
  const duration = Math.max(1, finite(action.duration, defaultDuration));
  const elapsed = Math.max(0, time - start);
  const progress = elapsed / duration;
  // Shelter lasts until its controller stops supplying it.
  // Preserve its original start so repeated renders keep advancing real frames.
  return progress <= 1 || action.type === 'shelter' ?
    { ...action, progress: clamp(progress), elapsed } : null;
}

function chooseAnimation(state, action, time, mode, animations) {
  const stats = state.stats || state;
  let id = state.sleeping || mode === 'charging' ? 'sleep' : action?.type ||
    (state.sick || finite(stats.health, 100) < 40 ? 'sick' : finite(stats.mood ?? stats.happiness, 100) < 25 ? 'angry' : 'idle');
  if (['tongue', 'feed', 'eat'].includes(id)) id = 'eat';
  else if (id === 'play') id = 'happy';
  if (!animations.has(id)) throw new Error(`No animation resource for action: ${id}`);
  let definition = animations.get(id);
  let index;
  if (id === 'idle' && !action) {
    const blink = animations.get('blink');
    const blinkDuration = blink.count / blink.fps * 1000;
    const phase = time % (6500 + blinkDuration);
    if (phase >= 6500) {
      id = 'blink';
      definition = blink;
      index = Math.min(blink.count - 1, Math.floor((phase - 6500) / 1000 * blink.fps));
    }
  }
  if (index === undefined) {
    // Loop metadata drives genuine sprite playback. Finite interactions use
    // their declared duration so that the complete frame sequence is visible.
    const elapsed = action ? action.elapsed : time;
    index = definition.loop ? Math.floor(elapsed / 1000 * definition.fps) % definition.count :
      Math.min(definition.count - 1, Math.floor((action?.progress || 0) * definition.count));
  }
  return { id, index, definition };
}

function drawPetFrames(ctx, state, action, time, mode, scene, animations, positionX, renderScale) {
  const selected = chooseAnimation(state, action, time, mode, animations);
  const { id, index, definition } = selected;
  const small = ['clock', 'weather', 'charging'].includes(mode);
  // A single stage scale is constant across every pose; no frame is stretched.
  const cellSize = (small ? 185 : 250) * renderScale;
  const baseline = SCENES[scene].baseline;
  // Only scene translation is permitted; every bodily pose is in a PNG frame.
  const position = Number.isFinite(positionX) ? clamp(positionX, -48, 48) :
    id === 'walk' ? finite(action?.distance, 0) * (action?.progress || 0) : 0;
  const x = pixel(SIZE / 2 - cellSize * definition.pivot.x + position);
  // A frame's actual front foot (or resting support) sets its ground contact.
  // Use only its y coordinate: per-frame x anchors would make the body jitter.
  const footAnchor = definition.foot_anchors?.[id==='sleep'?0:index] || definition.pivot;
  const y = baseline - cellSize * footAnchor.y;
  ctx.fillStyle = SCENES[scene].perch ? '#695837' : '#547c55';
  ctx.globalAlpha = .22;
  ctx.beginPath();
  ctx.ellipse(SIZE / 2 + position, baseline + 1, cellSize * .25,
    SCENES[scene].perch ? 2 : 5, 0, 0, Math.PI * 2);
  ctx.fill();
  ctx.globalAlpha = 1;
  ctx.drawImage(definition.image, index * definition.width, 0, definition.width, definition.height,
    x, y, cellSize, cellSize);

  if (id==='sleep') {
    // Pixel letters float above a fixed head; sprite frame size and resting
    // support never change. Breathing is painted into the four PNG poses.
    const z = ['11111','00001','00010','00100','01000','10000','11111'];
    const bounds=definition.foot_anchors?.[0]?.body_bounds;
    const head = definition.sleep_head || {x:.72,y:bounds?bounds[1]/definition.height:.42};
    for (let letter=0; letter<4; letter++) {
      const scale=letter<2?2:3;
      const zx=pixel(x+cellSize*head.x+letter*14-25);
      const zy=pixel(y+cellSize*head.y-letter*4-28+Math.sin(time/1500+letter*.5)*2);
      ctx.fillStyle='#375268';
      for(let row=0;row<z.length;row++)for(let col=0;col<z[row].length;col++)if(z[row][col]==='1')ctx.fillRect(zx+col*scale+1,zy+row*scale+1,scale,scale);
      ctx.fillStyle='#fbf5cb';
      for(let row=0;row<z.length;row++)for(let col=0;col<z[row].length;col++)if(z[row][col]==='1')ctx.fillRect(zx+col*scale,zy+row*scale,scale,scale);
    }
  }
  let mouth = null, tongue = null;
  if (id === 'eat' && action) {
    const p = action.progress;
    // Mouth poses are real frames. Anchors may be calibrated per frame in the
    // manifest. Loaded eat resources always include the open-mouth anchors.
    const anchor = definition.mouths?.[index] || definition.mouth;
    mouth = anchor ? { x: x + cellSize * anchor.x, y: y + cellSize * anchor.y } :
      { x: x + cellSize * .84, y: y + cellSize * .60 };
    const target = { x: finite(action.targetX, Math.min(408, mouth.x + 64)), y: finite(action.targetY, mouth.y - 22) };
    let extension = 0;
    // eat 0/6/7 are closed, 1 opens, 2/3 are wide open, 4 remains open,
    // and 5 closes. The independent tongue must finish before frame 5.
    if (index >= 2 && index <= 4) {
      if (p >= .25 && p < .36) extension = ease((p - .25) / .11);
      else if (p < .43 && p >= .36) extension = 1;
      else if (p < .625 && p >= .43) extension = 1 - ease((p - .43) / .195);
    }
    const tip = { x: mouth.x + (target.x - mouth.x) * extension, y: mouth.y + (target.y - mouth.y) * extension };
    tongue = { extension, tip, target };
    if (extension > .02) {
      blockLine(ctx, mouth.x, mouth.y, tip.x, tip.y, 6, '#9c4355');
      blockLine(ctx, mouth.x, mouth.y - 1, tip.x, tip.y - 1, 4, '#f07d8c');
      ctx.fillStyle = '#f2a4a0';
      ctx.fillRect(pixel(tip.x - 4), pixel(tip.y - 4), 8, 8);
    }
    const item = typeof action.item === 'string' ? action.item : action.item?.id;
    const food = item || action.food || 'cricket';
    if (p < .43) {
      drawBug(ctx, target.x + (p < .43 ? Math.sin(time / 80) * 2 : 0),
        target.y + (p < .43 ? Math.sin(time / 110) * 3 : 0), time, food);
    } else if (p < .60 && extension > .02) drawBug(ctx, tip.x, tip.y, time, food);
  }
  if ((id === 'happy' || id === 'pet') && action) {
    const phase = action.progress;
    ctx.globalAlpha = Math.min(1, (1 - phase) * 2);
    heart(ctx, x + cellSize * .28, y + cellSize * .18 - phase * 25, 2);
    ctx.globalAlpha = 1;
  }
  return { action: id, frameIndex: index, count: definition.count, x, y, cellSize, baseline, positionX: position,
    footAnchor: { ...footAnchor }, groundPoint: { x: x + cellSize * footAnchor.x, y: baseline }, mouth, tongue };
}

function environmentForScene(scene, environment, mode, weather) {
  const phase = PHASES.has(environment?.phase) ? environment.phase : SCENES[scene].night ? 'night' : 'day';
  let kind = WEATHER_KINDS.has(environment?.weather) ? environment.weather : 'unknown';
  // Legacy weather widgets remain usable. An explicit unknown environment is
  // never inferred from old weather text, which could represent a stale fetch.
  if (!environment && mode === 'weather') {
    const description = String(weather.condition || weather.text || '');
    if (/雷|storm|thunder/i.test(description)) kind = 'storm';
    else if (/雪|snow/i.test(description)) kind = 'snow';
    else if (/雾|fog/i.test(description)) kind = 'fog';
    else if (/雨|rain|shower/i.test(description) || weather.rain === true) kind = 'rain';
    else if (/云|cloud|overcast/i.test(description)) kind = 'cloudy';
  }
  return { phase, weather: kind, preview: Boolean(environment?.preview), enabled: Boolean(environment?.enabled) };
}

function drawEnvironmentTone(ctx, scene, environment) {
  const { phase, weather } = environment;
  // Flat palette overlays retain the original square pixels without blurring
  // the forest, and leave the chameleon's actual sprite colors untouched.
  if (phase === 'night' && !SCENES[scene].night) {
    ctx.fillStyle = '#18374d';
    ctx.globalAlpha = .55;
    ctx.fillRect(0, 0, SIZE, SIZE);
  } else if (phase === 'dusk') {
    ctx.fillStyle = '#ba7750';
    ctx.globalAlpha = .17;
    ctx.fillRect(0, 0, SIZE, SIZE);
    ctx.fillStyle = '#705070';
    ctx.globalAlpha = .07;
    ctx.fillRect(0, 0, SIZE, 214);
  } else if (phase === 'dawn') {
    ctx.fillStyle = '#e7d1a1';
    ctx.globalAlpha = .09;
    ctx.fillRect(0, 0, SIZE, SIZE);
  }
  if (['cloudy', 'rain', 'storm'].includes(weather)) {
    ctx.fillStyle = phase === 'night' ? '#223d50' : '#526b75';
    ctx.globalAlpha = weather === 'storm' ? .24 : weather === 'rain' ? .14 : .08;
    ctx.fillRect(0, 0, SIZE, SIZE);
  }
  ctx.globalAlpha = 1;
}

function drawMist(ctx, time, dense, night) {
  const drift = pixel(Math.sin(time / 6000) * 14);
  ctx.fillStyle = night ? '#9cadb7' : '#e2edda';
  ctx.globalAlpha = dense ? .18 : .10;
  for (const [x, y, width, height] of [[-22, 215, 218, 14], [182, 241, 308, 10], [12, 315, 308, 12]]) {
    ctx.fillRect(pixel(x + drift), y, width, height);
    ctx.fillRect(pixel(x + drift + 14), y - 4, width - 34, 4);
    ctx.fillRect(pixel(x + drift + 28), y + height, width - 55, 4);
  }
  if (dense) {
    ctx.globalAlpha = .13;
    ctx.fillRect(pixel(102 - drift), 168, 342, 12);
    ctx.fillRect(pixel(24 + drift), 357, 412, 16);
  }
  ctx.globalAlpha = 1;
}

function drawAmbience(ctx, scene, time, mode, environment) {
  const night = environment.phase === 'night';
  const weather = environment.weather;
  const precipitation = ['rain', 'storm', 'snow'].includes(weather);
  if (!night && !precipitation && weather !== 'fog') {
    for (let i = 0; i < 5; i += 1) {
      const x = 77 + ((time / 240 + i * 71) % 319);
      const y = 125 + Math.sin(time / 1800 + i * 2.3) * 34 + i * 22;
      ctx.globalAlpha = .45;
      ctx.fillStyle = '#ffebbb';
      ctx.fillRect(pixel(x), pixel(y), 3, 3);
    }
    ctx.globalAlpha = 1;
  } else if (night && !['rain', 'storm', 'cloudy', 'fog', 'snow'].includes(weather)) {
    if (!SCENES[scene].night) {
      // The approved clearing keeps its ground layout when night falls.
      ctx.fillStyle = '#ece4ae';
      ctx.fillRect(333, 73, 20, 28);
      ctx.fillRect(327, 79, 32, 16);
      ctx.fillStyle = '#42657e';
      ctx.fillRect(341, 69, 22, 22);
    }
    for (let i = 0; i < 6; i += 1) {
      ctx.globalAlpha = .18 + (Math.sin(time / 1500 + i) + 1) * .14;
      star(ctx, 75 + (i * 53) % 308, 68 + (i * 67) % 130, 1, '#f3e4a7');
    }
    for (let i = 0; i < 4; i += 1) {
      ctx.globalAlpha = .26 + (Math.sin(time / 1900 + i * 1.9) + 1) * .16;
      ctx.fillStyle = '#d7e794';
      ctx.fillRect(pixel(84 + i * 81 + Math.sin(time / 3200 + i) * 7),
        pixel(291 + i % 2 * 48 + Math.sin(time / 2600 + i) * 9), 3, 3);
    }
    ctx.globalAlpha = 1;
  }
  if (weather === 'cloudy' || weather === 'storm') {
    ctx.globalAlpha = weather === 'storm' ? .36 : .25;
    cloud(ctx, 132, 104, 1.25, night ? '#648197' : '#acbec0');
    cloud(ctx, 312, 159, .9, night ? '#557185' : '#acbec0');
    ctx.globalAlpha = 1;
  }
  if (weather === 'fog' || environment.phase === 'dawn') drawMist(ctx, time, weather === 'fog', night);
  if (weather === 'rain' || weather === 'storm') {
    ctx.globalAlpha = night ? .40 : .52;
    const count = weather === 'storm' ? 38 : 24;
    for (let i = 0; i < count; i += 1) {
      const x = 24 + (i * 79) % 419;
      const y = 54 + ((time / (weather === 'storm' ? 5 : 8) + i * 41) % 337);
      blockLine(ctx, x, y, x - 4, y + 10, 2, night ? '#9fc4cf' : '#d8edf1');
    }
    // Small square splashes sit on existing terrain; no river is introduced.
    ctx.globalAlpha = .28;
    ctx.fillStyle = '#c9e0d8';
    for (let i = 0; i < 5; i += 1) {
      const phase = (time / 700 + i * .37) % 1;
      if (phase < .35) ctx.fillRect(92 + i * 67, 369 + (i % 2) * 25, phase < .16 ? 4 : 8, 2);
    }
    ctx.globalAlpha = 1;
  } else if (weather === 'snow') {
    ctx.globalAlpha = .72;
    ctx.fillStyle = '#edf2e6';
    for (let i = 0; i < 19; i += 1) {
      const x = 29 + (i * 71) % 404 + Math.sin(time / 2400 + i) * 8;
      const y = 64 + ((time / 35 + i * 37) % 321);
      ctx.fillRect(pixel(x), pixel(y), i % 3 ? 3 : 5, i % 3 ? 3 : 5);
    }
    ctx.globalAlpha = 1;
  }
  if (mode === 'charging') {
    for (let i = 0; i < 4; i += 1) {
      const phase = ((time / 2100) + i / 4) % 1;
      ctx.globalAlpha = (1 - phase) * .85;
      star(ctx, 129 + i * 48, 317 - phase * 54, 2, '#c7eabc');
    }
    ctx.globalAlpha = 1;
  }
}

export function createRenderer(canvas) {
  if (!(canvas && typeof canvas.getContext === 'function')) throw new TypeError('createRenderer requires a canvas element');
  const ctx = canvas.getContext('2d', { alpha: true });
  if (!ctx) throw new Error('This browser cannot create a 2D canvas context');
  if (!canvas.width || !canvas.height) { canvas.width = SIZE; canvas.height = SIZE; }
  const images = new Map();
  const stages = new Map();
  let stageCatalog = null;
  const backgroundCanvas = canvas.ownerDocument.createElement('canvas');
  backgroundCanvas.width = BACKGROUND_PIXELS;
  backgroundCanvas.height = BACKGROUND_PIXELS;
  const backgroundCtx = backgroundCanvas.getContext('2d', { alpha: false });
  if (!backgroundCtx) throw new Error('Could not create the pixel background canvas');
  const assetsBase = new URL('./assets/', import.meta.url);
  let cachedScene = null, cachedBackgroundImage = null;
  let destroyed = false, loading = null, ready = false, frameInfo = null;

  function assetUrl(path) {
    if (typeof path !== 'string' || !path) throw new Error('Missing preview asset path');
    const url = new URL(path, assetsBase);
    if (url.origin !== assetsBase.origin || !url.pathname.startsWith(assetsBase.pathname)) {
      throw new Error(`Asset must be inside preview/assets: ${path}`);
    }
    return url.href;
  }

  function loadImage(url) {
    return new Promise((resolve, reject) => {
      const image = new Image();
      image.onload = () => {
        if (destroyed) reject(new Error('Renderer was destroyed while loading assets'));
        else if (!image.naturalWidth || !image.naturalHeight) reject(new Error(`Image is empty: ${url}`));
        else resolve(image);
      };
      image.onerror = () => reject(new Error(`Could not load pet preview asset: ${url}`));
      image.src = url;
    });
  }

  function loadAssets() {
    if (destroyed) return Promise.reject(new Error('Renderer has been destroyed'));
    if (loading) return loading;
    ready = false;
    loading = (async () => {
      await Promise.all(ASSET_NAMES.map(async (name) => {
        images.set(name, await loadImage(assetUrl(`${name}.png`)));
      }));
      async function readJson(path, label) {
        const url = assetUrl(path);
        const response = await fetch(url, {cache:'no-store'});
        if (!response.ok) throw new Error(`Could not load ${label} (${response.status}): ${url}`);
        try { return await response.json(); }
        catch { throw new Error(`${label} is not valid JSON: ${url}`); }
      }
      const catalog = validateStageCatalog(await readJson('growth/stages.json','growth stage catalog'));
      const loadedStages = await Promise.all(catalog.stages.map(async stage => {
        const manifest = await readJson(stage.manifest,`${stage.label} animation manifest`);
        const entries = validateStageManifest(manifest,stage.id);
        const loaded = await Promise.all(entries.map(async ([id,entry]) => {
          const [image] = await Promise.all([
            loadImage(assetUrl(entry.sheet)),
            ...entry.frames.map(async (path,index) => {
              const frame = await loadImage(assetUrl(path));
              if (frame.naturalWidth !== entry.width || frame.naturalHeight !== entry.height) {
                throw new Error(`${stage.label}/${id} frame ${index} must be ${entry.width} × ${entry.height}; got ${frame.naturalWidth} × ${frame.naturalHeight}`);
              }
            }),
          ]);
          if (image.naturalWidth !== entry.width * entry.count || image.naturalHeight !== entry.height) {
            throw new Error(`${stage.label}/${id} sheet must be ${entry.width * entry.count} × ${entry.height}; got ${image.naturalWidth} × ${image.naturalHeight}`);
          }
          return [id,{...entry,image}];
        }));
        return [stage.id,{...stage,animations:new Map(loaded),totalFrames:loaded.reduce((sum,[,entry]) => sum + entry.count,0)}];
      }));
      if (destroyed) throw new Error('Renderer was destroyed while loading assets');
      stages.clear();
      loadedStages.forEach(([id,stage]) => stages.set(id,stage));
      stageCatalog = catalog;
      ready = true;
      const defaultStage = stages.get(catalog.default_stage);
      return {ok:true,assets:ASSET_NAMES,actions:[...defaultStage.animations.keys()],totalFrames:defaultStage.totalFrames,
        catalog,stages:loadedStages.map(([id,stage]) => ({id,label:stage.label,actions:[...stage.animations.keys()],totalFrames:stage.totalFrames}))};
    })().catch((error) => { loading = null; ready = false; throw error; });
    return loading;
  }

  function render({ state = {}, action = null, time = performance.now(), mode = 'pet', weather = {},
    environment, positionX, stage = getGrowthInfo(state).id } = {}) {
    if (destroyed) return;
    if (!ready) throw new Error('Pet animation frames have not loaded; await renderer.loadAssets()');
    const resources = stages.get(stage);
    if (!resources) throw new Error(`Growth stage resources have not loaded: ${stage}`);
    const active = actionProgress(action, time);
    const suppliedScene = environment && Object.hasOwn(SCENES, environment.scene) ? environment.scene : null;
    let scene = suppliedScene || state.scene || 'forest';
    if (!Object.hasOwn(SCENES, scene)) scene = 'forest';
    const currentEnvironment = environmentForScene(scene, environment, mode, weather);
    ctx.save();
    ctx.setTransform(1, 0, 0, 1, 0, 0);
    ctx.clearRect(0, 0, canvas.width, canvas.height);
    ctx.setTransform(canvas.width / SIZE, 0, 0, canvas.height / SIZE, 0, 0);
    ctx.imageSmoothingEnabled = false;
    ctx.beginPath();
    ctx.arc(SIZE / 2, SIZE / 2, SIZE / 2, 0, Math.PI * 2);
    ctx.clip();
    ctx.fillStyle = SCENES[scene].sky;
    ctx.fillRect(0, 0, SIZE, SIZE);
    const background = images.get(scene);
    if (cachedScene !== scene || cachedBackgroundImage !== background) {
      backgroundCtx.setTransform(BACKGROUND_PIXELS / SIZE, 0, 0, BACKGROUND_PIXELS / SIZE, 0, 0);
      backgroundCtx.imageSmoothingEnabled = false;
      if (background) cover(backgroundCtx, background);
      else fallbackScene(backgroundCtx, scene);
      cachedScene = scene;
      cachedBackgroundImage = background;
    }
    ctx.drawImage(backgroundCanvas, 0, 0, BACKGROUND_PIXELS, BACKGROUND_PIXELS, 0, 0, SIZE, SIZE);
    drawEnvironmentTone(ctx, scene, currentEnvironment);
    drawAmbience(ctx, scene, time, mode, currentEnvironment);
    for(let pile=0;pile<Math.min(3,state.poopCount||0);pile++) {
      const px=148+pile*42,py=SCENES[scene].perch?383:SCENES[scene].baseline+14;
      ctx.fillStyle='#775540';ctx.fillRect(px,py,24,8);ctx.fillRect(px+4,py-6,16,8);ctx.fillRect(px+8,py-10,8,6);
      ctx.fillStyle='#c08c58';ctx.fillRect(px+6,py-3,6,3);ctx.fillRect(px+10,py-9,4,3);
    }
    frameInfo = { ...drawPetFrames(ctx, state, active, time, mode, scene, resources.animations, positionX, resources.render_scale),
      stage, scene, environment: currentEnvironment };
    ctx.fillStyle = currentEnvironment.phase === 'night' ? '#294756' : '#315b3b';
    ctx.globalAlpha = .07;
    ctx.fillRect(0, 384, SIZE, 82);
    ctx.fillRect(0, 418, SIZE, 48);
    ctx.globalAlpha = 1;
    ctx.restore();
    return frameInfo;
  }

  function capture() {
    if (destroyed || !ready) return Promise.reject(new Error('Pet animation renderer is not ready'));
    return new Promise((resolve, reject) => {
      try { canvas.toBlob((blob) => blob ? resolve(blob) : reject(new Error('Could not capture the pet screen')), 'image/png'); }
      catch (error) { reject(error); }
    });
  }

  return {
    loadAssets, render, capture,
    captureDataURL: () => {
      if (destroyed || !ready) throw new Error('Pet animation renderer is not ready');
      return canvas.toDataURL('image/png');
    },
    getFrameInfo: () => frameInfo ? { ...frameInfo } : null,
    destroy() { destroyed = true; ready = false; images.clear(); stages.clear(); stageCatalog = null; frameInfo = null; cachedScene = null; cachedBackgroundImage = null; },
  };
}

export { SIZE as SCREEN_SIZE };
