"""
Crux Pipeline A — shared constants.

Keep this file the single source of truth for class IDs, names,
and colours so the model, dataloaders, and UE5 exporter never drift.
"""

from dataclasses import dataclass
from typing import Dict, List, Tuple

# ---------------------------------------------------------------------------
# Semantic classes
# ---------------------------------------------------------------------------

CLASS_NAMES: List[str] = [
    "concrete",          # 0  — paths, walls, foundations
    "asphalt",           # 1  — roads, parking lots
    "grass_vegetation",  # 2  — parks, verges, planters
    "metal_structural",  # 3  — beams, railings, grates, lamp posts
    "metal_litter",      # 4  — cans, foil, bottle caps (gameplay objects)
    "wood",              # 5  — benches, pallets, crates
    "water",             # 6  — puddles, drains, fountains
    "glass",             # 7  — windows, bottles
    "gravel_dirt",       # 8  — unpaved surfaces, soil
    "unknown",           # 9  — low-confidence catch-all
]

NUM_CLASSES: int = len(CLASS_NAMES)

# Map class name → int ID
CLASS_TO_ID: Dict[str, int] = {name: i for i, name in enumerate(CLASS_NAMES)}

# Colour palette for visualisation (R, G, B) 0-255
CLASS_COLOURS: List[Tuple[int, int, int]] = [
    (180, 180, 180),   # concrete  — grey
    (60,  60,  60),    # asphalt   — dark grey
    (80,  160, 80),    # grass     — green
    (140, 100, 60),    # metal_structural — bronze
    (220, 180, 50),    # metal_litter     — gold
    (160, 100, 40),    # wood      — brown
    (80,  160, 220),   # water     — blue
    (200, 230, 255),   # glass     — light blue
    (180, 140, 100),   # gravel    — tan
    (255, 0,   255),   # unknown   — magenta (flags for QA)
]

# Unreal Engine 5 material slot names (must match your UE5 material layer setup)
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
# Input feature layout
# Feature vector per point:  [x, y, z, intensity, r, g, b, nx, ny, nz, density]
# ---------------------------------------------------------------------------

FEATURE_NAMES: List[str] = [
    "x", "y", "z",          # 0-2  XYZ coordinates (normalised per-batch)
    "intensity",             # 3    LIDAR return intensity (0-1 normalised)
    "r", "g", "b",           # 4-6  co-registered RGB (0-1 normalised)
    "nx", "ny", "nz",        # 7-9  estimated surface normal
    "density",               # 10   local point density (log-normalised)
]

NUM_FEATURES: int = len(FEATURE_NAMES)  # 11

# ---------------------------------------------------------------------------
# Dataset remapping tables
# Map public dataset class IDs → Crux class IDs.
# Points that map to -1 are ignored during training (masked in loss).
# ---------------------------------------------------------------------------

# SemanticKITTI 19-class → Crux 10-class
SEMANTICKITTI_REMAP: Dict[int, int] = {
    0:  9,  # unlabeled  → unknown
    1:  9,  # outlier    → unknown
    10: 9,  # car        → ignore (vehicle, not a surface) — mapped to unknown
    11: 9,  # bicycle    → unknown
    13: 9,  # bus        → unknown
    15: 9,  # motorcycle → unknown
    16: 9,  # on-rails   → unknown
    18: 9,  # truck      → unknown
    20: 9,  # other-veh  → unknown
    30: 9,  # person     → unknown (privacy: stripped during preprocessing)
    31: 9,  # bicyclist  → unknown
    32: 9,  # motorcycl  → unknown
    40: 1,  # road       → asphalt
    44: 1,  # parking    → asphalt (surface-level)
    48: 0,  # sidewalk   → concrete
    49: 8,  # other-grnd → gravel_dirt
    50: 0,  # building   → concrete
    51: 3,  # fence      → metal_structural
    52: 9,  # other-str  → unknown
    60: 1,  # lane-mark  → asphalt (treat as road)
    70: 2,  # vegetation → grass_vegetation
    71: 2,  # trunk      → grass_vegetation
    72: 8,  # terrain    → gravel_dirt
    80: 3,  # pole       → metal_structural
    81: 3,  # traf-sign  → metal_structural
    99: 9,  # other-obj  → unknown
    252: 9, # moving car → unknown
    256: 9, # moving bic → unknown
    253: 9, # moving per → unknown
    254: 9, # moving mot → unknown
    255: 9, # moving oth → unknown
    257: 9, # moving bus → unknown
    258: 9, # moving trk → unknown
    259: 9, # moving oth → unknown
}

# Toronto-3D 8-class → Crux 10-class
TORONTO3D_REMAP: Dict[int, int] = {
    0: 9,  # unclassified → unknown
    1: 1,  # road         → asphalt
    2: 3,  # road marking → asphalt
    3: 2,  # natural      → grass_vegetation
    4: 0,  # building     → concrete
    5: 3,  # utility line → metal_structural
    6: 3,  # pole         → metal_structural
    7: 9,  # car          → unknown
    8: 0,  # fence        → concrete (mix; most are concrete/masonry in Toronto)
}

# S3DIS 13-class → Crux 10-class  (indoor dataset, useful for object diversity)
S3DIS_REMAP: Dict[int, int] = {
    0:  0,  # ceiling  → concrete
    1:  0,  # floor    → concrete
    2:  0,  # wall     → concrete
    3:  3,  # beam     → metal_structural
    4:  3,  # column   → metal_structural
    5:  7,  # window   → glass
    6:  3,  # door     → metal_structural
    7:  5,  # table    → wood
    8:  5,  # chair    → wood
    9:  9,  # sofa     → unknown
    10: 5,  # bookcase → wood
    11: 9,  # board    → unknown
    12: 9,  # clutter  → unknown
}

# ---------------------------------------------------------------------------
# Confidence threshold for active learning flagging
# Points below this score are sent to the labeling queue.
# ---------------------------------------------------------------------------

CONFIDENCE_THRESHOLD: float = 0.75

# Points above this threshold are treated as high-quality auto-labels.
HIGH_CONFIDENCE_THRESHOLD: float = 0.95

# ---------------------------------------------------------------------------
# Voxel downsample resolution (metres) used during preprocessing.
# 0.05m = 5cm — balances detail vs. memory for drone outdoor scans.
# ---------------------------------------------------------------------------

VOXEL_SIZE: float = 0.05  # metres

# KNN neighbourhood size for normal estimation and local density
KNN_NEIGHBOURS: int = 16
