#!/usr/bin/env python3
"""Serve only the desktop preview, using Python's standard library."""

import argparse
import errno
import json
import math
import mimetypes
import subprocess
import sys
import threading
import time
import unicodedata
import urllib.error
import urllib.parse
import urllib.request
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path


PREVIEW_DIRECTORY = (Path(__file__).resolve().parent.parent / "preview").resolve()
APPLICATION = "chameleon-preview"
SERVICE_VERSION = 2
HOST = "127.0.0.1"
WEATHER_CACHE_SECONDS = 15 * 60
WEATHER_TIMEOUT_SECONDS = 8
WEATHER_CACHE_LIMIT = 64
WEATHER_CACHE = {}
WEATHER_LOCK = threading.Lock()
GEOCODING_URL = "https://geocoding-api.open-meteo.com/v1/search"
FORECAST_URL = "https://api.open-meteo.com/v1/forecast"


class WeatherError(Exception):
    def __init__(self, message, status=502):
        super().__init__(message)
        self.status = status


class NoWeatherRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, request, response, code, message, headers, new_url):
        # Weather requests have two fixed destinations; do not follow other URLs.
        raise urllib.error.URLError("天气服务返回了重定向")


def validate_city(city):
    if not isinstance(city, str):
        raise WeatherError("请输入城市名称。", 400)
    city = unicodedata.normalize("NFKC", city).strip()
    if not 2 <= len(city) <= 64 or any(
        not (character.isalnum() or character in " ,.'-") for character in city
    ):
        raise WeatherError("城市名称需为 2 到 64 个字，请勿输入网址或特殊符号。", 400)
    return city


def weather_condition(code):
    descriptions = {
        0: "晴", 1: "晴间多云", 2: "多云", 3: "阴",
        45: "雾", 48: "冻雾", 51: "小毛毛雨", 53: "毛毛雨", 55: "浓毛毛雨",
        56: "轻微冻毛毛雨", 57: "冻毛毛雨", 61: "小雨", 63: "中雨", 65: "大雨",
        66: "小冻雨", 67: "强冻雨", 71: "小雪", 73: "中雪", 75: "大雪",
        77: "米雪", 80: "小阵雨", 81: "中阵雨", 82: "强阵雨",
        85: "小阵雪", 86: "大阵雪", 95: "雷暴", 96: "雷暴伴冰雹",
        97: "强雷暴", 99: "强雷暴伴冰雹",
    }
    if not isinstance(code, int) or isinstance(code, bool) or code not in descriptions:
        raise WeatherError("天气服务返回了无法识别的天气代码。")
    return descriptions[code]


def open_meteo_json(endpoint, parameters):
    # No URL parameter can replace the official endpoint or redirect target.
    if endpoint not in (GEOCODING_URL, FORECAST_URL):
        raise WeatherError("天气服务地址无效。")
    url = endpoint + "?" + urllib.parse.urlencode(parameters)
    opener = urllib.request.build_opener(
        urllib.request.ProxyHandler({}), NoWeatherRedirect()
    )
    request = urllib.request.Request(
        url, headers={"Accept": "application/json", "User-Agent": "ChameleonDays/1.0"}
    )
    try:
        with opener.open(request, timeout=WEATHER_TIMEOUT_SECONDS) as response:
            raw = response.read(128 * 1024 + 1)
        if len(raw) > 128 * 1024:
            raise WeatherError("天气服务返回的数据过大。")
        payload = json.loads(raw.decode("utf-8"))
        if not isinstance(payload, dict) or payload.get("error"):
            raise WeatherError("天气服务暂时无法提供数据，请稍后重试。")
        return payload
    except (TimeoutError, urllib.error.URLError, OSError) as error:
        raise WeatherError("天气获取失败，请检查网络后重试。", 503) from error
    except (UnicodeError, ValueError) as error:
        raise WeatherError("天气服务返回的数据无法读取。") from error


def finite_number(value, minimum, maximum):
    return (
        isinstance(value, (int, float)) and not isinstance(value, bool)
        and math.isfinite(value) and minimum <= value <= maximum
    )


def resolve_city(city):
    # The default location uses the requested fixed Shantou coordinates.
    search_name = "Shantou" if city in ("汕头", "汕头市") else city
    if search_name.casefold() == "shantou":
        return "汕头", 23.354, 116.682
    payload = open_meteo_json(GEOCODING_URL, {
        "name": search_name, "count": 5, "language": "zh", "format": "json",
    })
    results = payload.get("results", [])
    if not isinstance(results, list):
        raise WeatherError("城市搜索返回的数据无法读取。")
    for result in results:
        if not isinstance(result, dict):
            continue
        latitude, longitude = result.get("latitude"), result.get("longitude")
        if finite_number(latitude, -90, 90) and finite_number(longitude, -180, 180):
            name = result.get("name")
            return name if isinstance(name, str) and name else city, latitude, longitude
    raise WeatherError("没有找到这个城市，请尝试完整城市名或英文名。", 404)


def get_current_weather(city):
    city = validate_city(city)
    key = city.casefold()
    # Serialise cache misses so repeated page updates do not issue extra requests.
    with WEATHER_LOCK:
        cached = WEATHER_CACHE.get(key)
        if cached and time.monotonic() - cached[0] < WEATHER_CACHE_SECONDS:
            return dict(cached[1])
        display_city, latitude, longitude = resolve_city(city)
        payload = open_meteo_json(FORECAST_URL, {
            "latitude": latitude, "longitude": longitude,
            "current": "temperature_2m,weather_code,is_day",
            "timezone": "auto", "timeformat": "unixtime", "forecast_days": 1,
        })
        current = payload.get("current")
        if not isinstance(current, dict):
            raise WeatherError("天气服务没有返回当前天气。")
        temperature, timestamp = current.get("temperature_2m"), current.get("time")
        if not finite_number(temperature, -100, 100) or not finite_number(timestamp, 0, 10**11):
            raise WeatherError("天气服务返回的温度或时间无效。")
        is_day = current.get("is_day")
        if isinstance(is_day, bool) or is_day not in (0, 1):
            raise WeatherError("天气服务返回的昼夜状态无效。")
        result = {
            "city": display_city,
            "temperature": round(temperature, 1),
            "condition": weather_condition(current.get("weather_code")),
            "isDay": is_day == 1,
            "updatedAt": int(timestamp * 1000),
            "source": "Open-Meteo",
        }
        if len(WEATHER_CACHE) >= WEATHER_CACHE_LIMIT:
            WEATHER_CACHE.pop(next(iter(WEATHER_CACHE)))
        WEATHER_CACHE[key] = (time.monotonic(), result)
        return dict(result)


class PreviewHandler(SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=str(PREVIEW_DIRECTORY), **kwargs)

    def end_headers(self):
        self.send_header("Cache-Control", "no-store, max-age=0")
        self.send_header("Pragma", "no-cache")
        self.send_header("Expires", "0")
        self.send_header("X-Content-Type-Options", "nosniff")
        super().end_headers()

    def _health(self, include_body):
        payload = json.dumps(
            {
                "application": APPLICATION,
                "version": SERVICE_VERSION,
                "directory": str(PREVIEW_DIRECTORY),
            },
            ensure_ascii=False,
        ).encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        if include_body:
            self.wfile.write(payload)

    def _weather(self):
        try:
            query = urllib.parse.urlsplit(self.path).query
            if len(query) > 512:
                raise WeatherError("天气查询参数过长。", 400)
            parameters = urllib.parse.parse_qs(
                query, keep_blank_values=True, max_num_fields=8, errors="strict"
            )
            if set(parameters) - {"city"} or len(parameters.get("city", [])) > 1:
                raise WeatherError("天气查询只接受一个 city 城市名称。", 400)
            result, status = get_current_weather(parameters.get("city", ["汕头"])[0]), 200
        except WeatherError as error:
            result, status = {"error": str(error)}, error.status
        except (ValueError, UnicodeError):
            result, status = {"error": "天气查询参数无效。"}, 400
        raw = json.dumps(result, ensure_ascii=False).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(raw)))
        self.end_headers()
        self.wfile.write(raw)

    def do_GET(self):
        if urllib.parse.urlsplit(self.path).path == "/health":
            self._health(include_body=True)
        elif urllib.parse.urlsplit(self.path).path == "/weather":
            self._weather()
        else:
            super().do_GET()

    def do_HEAD(self):
        if urllib.parse.urlsplit(self.path).path == "/health":
            self._health(include_body=False)
        else:
            super().do_HEAD()

    def translate_path(self, path):
        # Reject traversal and hidden files, including percent-encoded variants.
        try:
            decoded = urllib.parse.unquote(
                urllib.parse.urlsplit(path).path, errors="strict"
            )
            parts = decoded.split("/")
            if "\x00" in decoded or any(part.startswith(".") for part in parts if part):
                return str(PREVIEW_DIRECTORY / ".unavailable")
            candidate = (PREVIEW_DIRECTORY / decoded.lstrip("/")).resolve()
            candidate.relative_to(PREVIEW_DIRECTORY)
            return str(candidate)
        except (UnicodeError, ValueError, OSError):
            return str(PREVIEW_DIRECTORY / ".unavailable")

    def list_directory(self, path):
        self.send_error(404, "Directory listing is disabled")
        return None

    def log_message(self, format, *args):
        # Normal successful asset requests do not need terminal noise.
        if len(args) > 1 and str(args[1]) in ("200", "304"):
            return
        super().log_message(format, *args)


def is_our_service(port):
    # Disable environment proxies for this local-only identity check.
    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
    try:
        with opener.open(f"http://{HOST}:{port}/health", timeout=0.6) as response:
            if response.status != 200:
                return False
            payload = json.loads(response.read(4096).decode("utf-8"))
        return (
            isinstance(payload, dict)
            and payload.get("version") == SERVICE_VERSION
            and payload.get("application") == APPLICATION
            and payload.get("directory") == str(PREVIEW_DIRECTORY)
        )
    except (OSError, ValueError, urllib.error.URLError):
        return False


def open_browser(url):
    try:
        if sys.platform == "darwin":
            result = subprocess.run(["open", url], check=False)
            if result.returncode:
                print("浏览器没有自动打开，请复制上面的地址手动打开。", flush=True)
        else:
            import webbrowser

            if not webbrowser.open(url):
                print("请复制上面的地址，在浏览器中打开。", flush=True)
    except OSError:
        print("浏览器没有自动打开，请复制上面的地址手动打开。", flush=True)


def main():
    parser = argparse.ArgumentParser(description="启动变色龙电子宠物电脑测试版。")
    parser.add_argument("--port", type=int, default=8768, help="起始端口（默认 8768）")
    parser.add_argument("--no-open", action="store_true", help="不自动打开浏览器")
    args = parser.parse_args()
    if not 1024 <= args.port <= 65535:
        parser.error("端口需在 1024 到 65535 之间。")
    if not (PREVIEW_DIRECTORY / "index.html").is_file():
        parser.exit(1, "未找到 preview/index.html，请先确认电脑测试版文件完整。\n")

    mimetypes.add_type("text/javascript", ".mjs")
    mimetypes.add_type("text/javascript", ".js")
    server = None
    for port in range(args.port, min(args.port + 5, 65536)):
        try:
            server = ThreadingHTTPServer((HOST, port), PreviewHandler)
            break
        except OSError as error:
            if error.errno != errno.EADDRINUSE:
                parser.exit(1, f"无法启动本地服务：{error}\n")
            if is_our_service(port):
                url = f"http://{HOST}:{port}/"
                print(f"电脑测试版已在另一终端运行，复用地址：{url}", flush=True)
                print("请保留原来的服务终端；在原终端按 Control + C 可停止。", flush=True)
                if not args.no_open:
                    open_browser(url)
                return 0
            print(f"端口 {port} 已被其他程序使用，尝试下一个端口。", flush=True)
    if server is None:
        parser.exit(1, "连续 5 个端口均不可用，请通过 --port 指定其他起始端口。\n")

    url = f"http://{HOST}:{server.server_port}/"
    print(f"变色龙电子宠物电脑测试版：{url}", flush=True)
    print("保持此终端窗口开启，在这里按 Control + C 停止服务。", flush=True)
    print("仅提供电脑功能预览，不会连接开发板或执行烧录。", flush=True)
    try:
        if not args.no_open:
            open_browser(url)
        server.serve_forever()
    except KeyboardInterrupt:
        print("\n电脑测试版服务已停止。", flush=True)
    finally:
        server.server_close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
