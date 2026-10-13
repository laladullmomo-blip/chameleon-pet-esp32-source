// Current model-based weather from Open-Meteo, via the local preview server.
// Attribution: https://open-meteo.com/  Docs: https://open-meteo.com/en/docs
// Location data: GeoNames, https://www.geonames.org/
const CACHE_MS = 15 * 60 * 1000;
const TIMEOUT_MS = 20000;
const cache = new Map();
const pending = new Map();

function validateCity(city) {
  if (typeof city !== 'string') throw new Error('请输入城市名称。');
  const value = city.normalize('NFKC').trim();
  const length = [...value].length;
  if (length < 2 || length > 64 || !/^[\p{L}\p{N} ,.'-]+$/u.test(value)) {
    throw new Error('城市名称需为 2 到 64 个字，请勿输入网址或特殊符号。');
  }
  return value;
}

function abortError(signal) {
  return signal.reason instanceof Error ? signal.reason : new DOMException('已取消天气请求。', 'AbortError');
}

function withSignal(promise, signal) {
  if (!signal) return promise;
  if (signal.aborted) return Promise.reject(abortError(signal));
  // Cancellation applies to this caller; an already shared request may still
  // fill the cache for other callers without changing the cancelled page.
  return new Promise((resolve, reject) => {
    const onAbort = () => { signal.removeEventListener('abort', onAbort); reject(abortError(signal)); };
    signal.addEventListener('abort', onAbort, { once: true });
    promise.then(
      value => { signal.removeEventListener('abort', onAbort); resolve(value); },
      error => { signal.removeEventListener('abort', onAbort); reject(error); },
    );
  });
}

async function requestWeather(city) {
  const controller = new AbortController();
  const timeout = setTimeout(() => controller.abort(), TIMEOUT_MS);
  try {
    const response = await fetch(`/weather?city=${encodeURIComponent(city)}`, {
      signal: controller.signal, cache: 'no-store', headers: { Accept: 'application/json' },
    });
    let result;
    try { result = await response.json(); }
    catch { throw new Error('本地天气服务没有返回有效数据，请重启测试服务后重试。'); }
    if (!response.ok) throw new Error(typeof result?.error === 'string' ? result.error : '天气获取失败，请稍后重试。');
    if (!result || typeof result.city !== 'string' || !result.city.trim()
        || typeof result.temperature !== 'number' || !Number.isFinite(result.temperature)
        || result.temperature < -100 || result.temperature > 100
        || typeof result.condition !== 'string' || !result.condition.trim()
        || typeof result.isDay !== 'boolean'
        || typeof result.updatedAt !== 'number' || !Number.isFinite(result.updatedAt) || result.updatedAt <= 0
        || result.source !== 'Open-Meteo') {
      throw new Error('天气服务返回的数据不完整，请稍后重试。');
    }
    return {
      city: result.city, temperature: result.temperature, condition: result.condition,
      isDay: result.isDay, updatedAt: result.updatedAt, source: 'Open-Meteo',
    };
  } catch (error) {
    if (controller.signal.aborted) throw new Error('天气请求超时，请检查网络后重试。');
    if (error instanceof TypeError) throw new Error('无法连接天气服务，请保持终端开启并检查网络。');
    throw error;
  } finally {
    clearTimeout(timeout);
  }
}

export async function fetchCurrentWeather(city = '汕头', { signal } = {}) {
  const normalized = validateCity(city);
  if (signal?.aborted) throw abortError(signal);
  const key = normalized.toLocaleLowerCase();
  const saved = cache.get(key);
  if (saved && Date.now() - saved.savedAt < CACHE_MS) return { ...saved.data };
  let promise = pending.get(key);
  if (!promise) {
    promise = requestWeather(normalized).then(data => {
      if (cache.size >= 64) cache.delete(cache.keys().next().value);
      cache.set(key, { savedAt: Date.now(), data });
      return data;
    }).finally(() => pending.delete(key));
    pending.set(key, promise);
  }
  return { ...await withSignal(promise, signal) };
}
