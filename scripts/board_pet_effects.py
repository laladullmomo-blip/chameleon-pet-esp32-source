"""Extract the teen's baked-in hearts for the board's one-heart overlay.

This only removes small, detached pink decorations from the six approved
teen pet frames. It never writes PNGs or changes the largest body component.
The desktop originals remain the source of truth for the actual pose.
"""
from __future__ import annotations

import hashlib
from collections import deque

from PIL import Image


def rgba_hash(image: Image.Image) -> str:
    return hashlib.sha256(image.convert("RGBA").tobytes()).hexdigest()


def alpha_components(image: Image.Image) -> list[set[tuple[int, int]]]:
    rgba = image.convert("RGBA")
    width, height = rgba.size
    pixels = rgba.load()
    remaining = {(x, y) for y in range(height) for x in range(width)
                 if pixels[x, y][3]}
    components: list[set[tuple[int, int]]] = []
    while remaining:
        start = remaining.pop()
        points = {start}
        queue = deque([start])
        while queue:
            x, y = queue.popleft()
            for point in ((x-1, y), (x+1, y), (x, y-1), (x, y+1)):
                if point in remaining:
                    remaining.remove(point)
                    points.add(point)
                    queue.append(point)
        components.append(points)
    return sorted(components, key=len, reverse=True)


def is_pink(pixel: tuple[int, int, int, int]) -> bool:
    red, green, blue, alpha = pixel
    return bool(alpha and red * 2 > green * 3 and blue * 5 > green * 6)


def strip_teen_pet_hearts(image: Image.Image) -> tuple[Image.Image, dict]:
    """Return a board-only RGBA copy and an inspectable extraction record."""
    source = image.convert("RGBA")
    if source.size != (128, 128):
        raise ValueError("Only approved 128x128 teen pet frames may be filtered")
    components = alpha_components(source)
    if not components or len(components[0]) < 3000:
        raise ValueError("Expected a complete teen body as the main component")
    body = components[0]
    source_pixels = source.load()
    if any(is_pink(source_pixels[x, y]) for x, y in body):
        raise ValueError("Pink pixels connected to the body must not be removed")
    removed: set[tuple[int, int]] = set()
    for component in components[1:]:
        pink = {point for point in component if is_pink(source_pixels[point])}
        if not pink:
            continue
        left = min(x for x, _ in component)
        top = min(y for _, y in component)
        right = max(x for x, _ in component)
        bottom = max(y for _, y in component)
        if (pink != component or len(component) > 200 or left < 64 or bottom > 70
                or right-left >= 22 or bottom-top >= 22):
            raise ValueError("Unexpected pink component; do not alter approved art")
        removed.update(component)
    if not 50 <= len(removed) <= 120:
        raise ValueError("Expected the existing detached teen pet heart decorations")
    board = source.copy()
    board_pixels = board.load()
    for point in removed:
        board_pixels[point] = (0, 0, 0, 0)
    if any(board_pixels[point] != source_pixels[point] for point in body):
        raise AssertionError("Heart extraction changed a body pixel")
    bounds = [min(x for x, _ in removed), min(y for _, y in removed),
              max(x for x, _ in removed)+1, max(y for _, y in removed)+1]
    return board, {
        "effect": "teen-pet-floating-heart-extraction",
        "source_rgba_sha256": rgba_hash(source),
        "board_rgba_sha256": rgba_hash(board),
        "removed_pixels": len(removed),
        "removed_bounds": bounds,
        "body_pixels": len(body),
        "body_component_unchanged": True,
        "source_files_unchanged": True,
    }
