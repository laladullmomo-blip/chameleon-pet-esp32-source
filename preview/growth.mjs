// Visual stage selection lives outside the pet model and its saved state.
export const STAGE_IDS = Object.freeze(['baby', 'teen', 'adult']);
export const ACTIVE_ACTIONS = Object.freeze(['idle', 'happy', 'eat', 'sleep', 'walk', 'blink', 'stretch', 'wave', 'dizzy', 'angry', 'sick', 'bath', 'pet', 'shelter']);
const COUNTS = Object.freeze({idle:6,happy:6,eat:8,sleep:4,walk:8,blink:4,stretch:6,wave:6,dizzy:6,angry:6,sick:4,bath:6,pet:6,shelter:6});
const validAnchor = anchor => anchor && Number.isFinite(anchor.x) && Number.isFinite(anchor.y) &&
  anchor.x >= 0 && anchor.x <= 1 && anchor.y >= 0 && anchor.y <= 1;
const safePath = path => typeof path === 'string' && /^[\w./-]+$/.test(path) &&
  !path.startsWith('/') && !path.split('/').includes('..');

export function validateStageCatalog(catalog) {
  if (!catalog || catalog.version !== 1 || catalog.default_stage !== 'teen' ||
    !Array.isArray(catalog.stages) || catalog.stages.length !== STAGE_IDS.length) {
    throw new Error('Invalid growth stage catalog; expected baby, teen and adult with teen as default');
  }
  const seen = new Set();
  const stages = catalog.stages.map(entry => {
    if (!entry || !STAGE_IDS.includes(entry.id) || seen.has(entry.id) ||
      typeof entry.label !== 'string' || !entry.label.trim() || !safePath(entry.reference) || !safePath(entry.manifest) ||
      !Number.isFinite(entry.render_scale) || entry.render_scale < .5 || entry.render_scale > 1.5) {
      throw new Error(`Invalid or duplicate growth stage: ${entry?.id || 'unknown'}`);
    }
    seen.add(entry.id);
    const expected = entry.id === 'teen' ? 'frames/manifest.json' : `growth/${entry.id}/frames/manifest.json`;
    if (entry.manifest !== expected) throw new Error(`Wrong animation manifest for ${entry.id}: ${entry.manifest}`);
    return Object.freeze({id:entry.id,label:entry.label,reference:entry.reference,manifest:entry.manifest,render_scale:entry.render_scale});
  });
  return Object.freeze({version:1,default_stage:'teen',stages:Object.freeze(stages)});
}

export function createStagePreview(catalog) {
  const verified = validateStageCatalog(catalog);
  const stages = new Map(verified.stages.map(stage => [stage.id, stage]));
  let selected = verified.default_stage;
  return Object.freeze({
    get id() { return selected; },
    get current() { return stages.get(selected); },
    get stages() { return verified.stages; },
    select(id) {
      if (!stages.has(id)) throw new Error(`Unknown growth preview stage: ${id}`);
      selected = id;
      return stages.get(id);
    },
  });
}

export function validateStageManifest(manifest, stageId) {
  if (!STAGE_IDS.includes(stageId)) throw new Error(`Unknown growth stage: ${stageId}`);
  if (!manifest || manifest.version !== 1 || !manifest.actions || typeof manifest.actions !== 'object' || Array.isArray(manifest.actions)) {
    throw new Error(`Unsupported animation manifest for ${stageId}`);
  }
  for (const id of ACTIVE_ACTIONS) {
    if (!Object.hasOwn(manifest.actions,id)) throw new Error(`${stageId} animation manifest is missing the ${id} frame list`);
  }
  const root = stageId === 'teen' ? 'frames/' : `growth/${stageId}/frames/`;
  const entries = Object.entries(manifest.actions).map(([id,entry]) => {
    const legacyGrip = stageId === 'teen' && id === 'grip';
    if (!ACTIVE_ACTIONS.includes(id) && !legacyGrip) throw new Error(`Unsupported animation action for ${stageId}: ${id}`);
    const count = legacyGrip ? 6 : COUNTS[id];
    if (!entry || entry.count !== count || entry.width !== 128 || entry.height !== 128 ||
      !Number.isFinite(entry.fps) || entry.fps < 1 || entry.fps > 30 || typeof entry.loop !== 'boolean') {
      throw new Error(`Invalid 128 × 128 animation metadata for ${stageId}/${id}; expected ${count} frames`);
    }
    if (!Array.isArray(entry.frames) || entry.frames.length !== count || new Set(entry.frames).size !== count ||
      !safePath(entry.sheet) || !entry.sheet.startsWith(root) ||
      entry.frames.some(path => !safePath(path) || !path.startsWith(root))) {
      throw new Error(`Invalid, duplicate or cross-stage animation paths for ${stageId}/${id}`);
    }
    if (id === 'eat' && (entry.loop || !Array.isArray(entry.mouths) || entry.mouths.length !== count ||
      [2,3,4].some(index => !validAnchor(entry.mouths[index])))) {
      throw new Error(`${stageId} eat requires 8 non-looping mouth-pose frames with anchors at frames 2, 3 and 4`);
    }
    if (id === 'sleep' && (entry.fps !== 1 || !entry.loop)) throw new Error(`${stageId} sleep requires 4 gently breathing frames at 1 FPS`);
    if (['grip','shelter'].includes(id) && !entry.loop) throw new Error(`${stageId}/${id} requires 6 looping pose frames`);
    const pivot = entry.pivot || {x:.5,y:.85};
    if (!validAnchor(pivot)) throw new Error(`Invalid animation pivot: ${stageId}/${id}`);
    if (entry.distinct_frames !== undefined && entry.distinct_frames < 2) throw new Error(`Animation lacks distinct poses: ${stageId}/${id}`);
    if (entry.mouth != null && !validAnchor(entry.mouth)) throw new Error(`Invalid animation mouth anchor: ${stageId}/${id}`);
    if (entry.mouths !== undefined && (!Array.isArray(entry.mouths) || entry.mouths.length !== count ||
      entry.mouths.some(anchor => anchor != null && !validAnchor(anchor)))) throw new Error(`Invalid per-frame animation mouth anchors: ${stageId}/${id}`);
    if (entry.sleep_head != null && !validAnchor(entry.sleep_head)) throw new Error(`Invalid sleep head anchor: ${stageId}/${id}`);
    if (!Array.isArray(entry.foot_anchors) || entry.foot_anchors.length !== count || entry.foot_anchors.some(anchor => {
      if (!validAnchor(anchor)) return true;
      for (const [axis,dimension] of [['x',entry.width],['y',entry.height]]) {
        const sourcePixel = anchor[`pixel_${axis}`];
        if (sourcePixel !== undefined && (!Number.isFinite(sourcePixel) || sourcePixel < 0 || sourcePixel > dimension ||
          Math.abs(sourcePixel / dimension - anchor[axis]) > .00001)) return true;
      }
      const bounds = anchor.body_bounds;
      return bounds !== undefined && (!Array.isArray(bounds) || bounds.length !== 4 || bounds.some(value => !Number.isInteger(value)) ||
        bounds[0] < 0 || bounds[1] < 0 || bounds[2] > entry.width || bounds[3] > entry.height ||
        // Manifest fractions are rounded to six decimals. A support anchor at
        // the bottom edge can therefore be a fraction of a pixel beyond it.
        bounds[0] >= bounds[2] || bounds[1] >= bounds[3] || anchor.x * entry.width < bounds[0] - .001 ||
        anchor.x * entry.width > bounds[2] + .001 || anchor.y * entry.height < bounds[1] - .001 ||
        anchor.y * entry.height > bounds[3] + .001);
    })) throw new Error(`Invalid per-frame animation foot anchors: ${stageId}/${id}`);
    return [id,{...entry,pivot}];
  });
  // Retain the old teen grip resource on disk without making it selectable.
  return entries.filter(([id]) => ACTIVE_ACTIONS.includes(id));
}
