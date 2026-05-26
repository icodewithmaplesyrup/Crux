"""
Crux Pipeline A — shared constants.

Single source of truth for class IDs, names, colours, and tuning params.
"""

from dataclasses import dataclass
from typing import Dict, List, Tuple

# ---------------------------------------------------------------------------
# Semantic classes
# ---------------------------------------------------------------------------

CLASS_NAMES: List[str] = [
    "concrete",          # 0 — paths, walls, foundations
    "asphalt",           # 1 — roads, parking lots
    "grass_vegetation",  # 2 — parks, verges, planters
    "metal_structural",  # 3 — beams, railings, grates, lamp posts
    "metal_litter",      # 4 — cans, foil, bottle caps (gameplay objects)
    "wood",              # 5 — benches, pallets, crates
    "water",             # 6 — puddles, drains, fountains
    "glass",             # 7 — windows, bottles
    "gravel_dirt",       # 8 — unpaved surfaces, soil
    "unknown",           # 9 — low-confidence catch-all
]

NUM_CLASSES: int = len(CLASS_NAMES)
CLASS_TO_ID: Dict[str, int] = {name: i for i, name in enumerate(CLASS_NAMES)}

CLASS_COLOURS: List[Tuple[int, int, int]] = [
    (180, 180, 180),  # concrete
    (60,  60,  60),   # asphalt
    (80,  160, 80),   # grass
    (140, 100, 60),   # metal_structural
    (220, 180, 50),   # metal_litter
    (160, 100, 40),   # wood
    (80,  160, 220),  # water
    (200, 230, 255),  # glass
    (180, 140, 100),  # gravel
    (255, 0,   255),  # unknown — magenta flags QA
]

CLASS_TO_UE5_MATERIAL: Dict[int, str] = {
    0: "M_Concrete",
    1: "M_Asphalt",
    2: "M_GrassVegetation",
    3: "M_MetalStructural",
    4: "M_MetalLitter",
    5: "M_Wood",
    6: "M_Water",
    7: "M_Glass",
    8: "M_GravelDirt",
    9: "M_Unknown",
}

# ---------------------------------------------------------------------------
# Feature layout  [x, y, z, intensity, r, g, b, nx, ny, nz, density]
# ---------------------------------------------------------------------------

FEATURE_NAMES: List[str] = [
    "x", "y", "z",
    "intensity",
    "r", "g", "b",
    "nx", "ny", "nz",
    "density",
]
NUM_FEATURES: int = len(FEATURE_NAMES)  # 11

# ---------------------------------------------------------------------------
# Dataset remapping tables  (public ID → Crux ID, -1 = ignored in loss)
# ---------------------------------------------------------------------------

SEMANTICKITTI_REMAP: Dict[int, int] = {
    0: 9, 1: 9, 10: 9, 11: 9, 13: 9, 15: 9, 16: 9, 18: 9, 20: 9,
    30: 9, 31: 9, 32: 9,
    40: 1, 44: 1, 48: 0, 49: 8, 50: 0, 51: 3, 52: 9,
    60: 1, 70: 2, 71: 2, 72: 8, 80: 3, 81: 3, 99: 9,
    252: 9, 253: 9, 254: 9, 255: 9, 256: 9, 257: 9, 258: 9, 259: 9,
}

TORONTO3D_REMAP: Dict[int, int] = {
    0: 9, 1: 1, 2: 1, 3: 2, 4: 0, 5: 3, 6: 3, 7: 9, 8: 0,
}

S3DIS_REMAP: Dict[int, int] = {
    0: 0, 1: 0, 2: 0, 3: 3, 4: 3, 5: 7, 6: 3,
    7: 5, 8: 5, 9: 9, 10: 5, 11: 9, 12: 9,
}

# ---------------------------------------------------------------------------
# Thresholds / geometry
# ---------------------------------------------------------------------------

CONFIDENCE_THRESHOLD: float = 0.75
HIGH_CONFIDENCE_THRESHOLD: float = 0.95
VOXEL_SIZE: float = 0.05    # metres
KNN_NEIGHBOURS: int = 16
