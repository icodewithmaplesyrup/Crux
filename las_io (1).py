"""
src/utils/las_io.py — LAS/LAZ read/write helpers and preprocessing.

Handles:
  - Reading .las / .laz files produced by drone LIDAR scanners
  - Extracting the 11-feature vector (XYZ + intensity + RGB + normals + density)
  - Voxel downsampling to reduce point count to a manageable size
  - Writing labelled point clouds back to .las for QA review in CloudCompare
"""

from __future__ import annotations

import logging
from pathlib import Path
from typing import Optional, Tuple

import laspy
import numpy as np
import open3d as o3d

from src.constants import (
    FEATURE_NAMES,
    KNN_NEIGHBOURS,
    NUM_FEATURES,
    VOXEL_SIZE,
)

logger = logging.getLogger(__name__)


# ---------------------------------------------------------------------------
# Reading
# ---------------------------------------------------------------------------

def load_las(path: str | Path, voxel_size: float = VOXEL_SIZE) -> Tuple[np.ndarray, Optional[np.ndarray]]:
    """
    Load a .las or .laz file, optionally voxel-downsample it, and return:
      - features: float32 array (N, 11)  — XYZ + intensity + RGB + nx,ny,nz + density
      - labels:   int32 array  (N,)      — class IDs, or None if the file has no labels

    The function is intentionally lenient: if RGB or intensity channels are
    missing from the file (some scanners omit them) it fills those fields with
    zeros rather than crashing.  Surface normals are always estimated from geometry.
    """
    path = Path(path)
    logger.info(f"Loading {path.name} …")

    las = laspy.read(str(path))

    # --- XYZ ---
    xyz = np.stack([las.x, las.y, las.z], axis=1).astype(np.float64)

    # --- Intensity (0-1 normalised) ---
    if hasattr(las, "intensity"):
        intensity = np.array(las.intensity, dtype=np.float32)
        # Raw LIDAR intensity is typically uint16 (0-65535)
        intensity = intensity / 65535.0
    else:
        logger.warning("No intensity channel found; filling with 0.")
        intensity = np.zeros(len(xyz), dtype=np.float32)

    # --- RGB (0-1 normalised) ---
    has_rgb = all(hasattr(las, ch) for ch in ("red", "green", "blue"))
    if has_rgb:
        r = np.array(las.red,   dtype=np.float32) / 65535.0
        g = np.array(las.green, dtype=np.float32) / 65535.0
        b = np.array(las.blue,  dtype=np.float32) / 65535.0
    else:
        logger.warning("No RGB channels found; filling with 0. "
                       "Classification accuracy will be lower — ensure "
                       "co-registered camera data is imported.")
        r = g = b = np.zeros(len(xyz), dtype=np.float32)

    # --- Labels (optional) ---
    if hasattr(las, "classification"):
        labels = np.array(las.classification, dtype=np.int32)
    else:
        labels = None

    # --- Build Open3D point cloud for geometric operations ---
    pcd = o3d.geometry.PointCloud()
    pcd.points = o3d.utility.Vector3dVector(xyz)

    # --- Voxel downsample ---
    if voxel_size > 0:
        logger.info(f"  Downsampling with voxel_size={voxel_size}m …")
        n_before = len(pcd.points)
        pcd, _, trace = pcd.voxel_down_sample_and_trace(voxel_size, pcd.get_min_bound(), pcd.get_max_bound())
        # trace maps each voxel → list of original point indices; take first
        keep_idx = np.array([idxs[0] for idxs in trace])
        xyz = np.asarray(pcd.points, dtype=np.float32)
        intensity = intensity[keep_idx]
        r, g, b = r[keep_idx], g[keep_idx], b[keep_idx]
        if labels is not None:
            labels = labels[keep_idx]
        logger.info(f"  {n_before:,} → {len(xyz):,} points after downsampling.")

    # --- Estimate surface normals ---
    pcd.points = o3d.utility.Vector3dVector(xyz)
    pcd.estimate_normals(
        search_param=o3d.geometry.KDTreeSearchParamKNN(knn=KNN_NEIGHBOURS)
    )
    pcd.orient_normals_towards_camera_location(camera_location=[0, 0, 100])
    normals = np.asarray(pcd.normals, dtype=np.float32)

    # --- Estimate local point density ---
    # Vectorized radius search via sklearn BallTree — avoids a Python-level
    # loop over every point, which times out on large drone scans (5-50M pts).
    # density = neighbour count in a 0.5m sphere, log-normalised.
    from sklearn.neighbors import BallTree
    radius = 0.5  # metres
    ball_tree = BallTree(xyz, metric="euclidean")
    counts = ball_tree.query_radius(xyz, r=radius, count_only=True)
    density = np.log1p(counts).astype(np.float32)

    # --- Stack into (N, 11) feature matrix ---
    features = np.stack(
        [xyz[:, 0], xyz[:, 1], xyz[:, 2],
         intensity,
         r, g, b,
         normals[:, 0], normals[:, 1], normals[:, 2],
         density],
        axis=1,
    ).astype(np.float32)

    assert features.shape[1] == NUM_FEATURES, \
        f"Expected {NUM_FEATURES} features, got {features.shape[1]}"

    return features, labels


# ---------------------------------------------------------------------------
# Normalisation
# ---------------------------------------------------------------------------

def normalise_xyz_inplace(features: np.ndarray) -> Tuple[np.ndarray, np.ndarray]:
    """
    Subtract centroid and scale XYZ (columns 0-2) so the scene fits roughly
    in [-1, 1].  Returns (mean, scale) so inference can undo the transform
    when exporting world-space coordinates.
    """
    xyz = features[:, :3]
    mean = xyz.mean(axis=0)
    scale = xyz.std()
    features[:, :3] = (xyz - mean) / (scale + 1e-8)
    return mean, scale


# ---------------------------------------------------------------------------
# Writing labelled .las files (for QA review and active-learning export)
# ---------------------------------------------------------------------------

def save_labelled_las(
    output_path: str | Path,
    features: np.ndarray,
    labels: np.ndarray,
    confidences: Optional[np.ndarray] = None,
    xyz_mean: Optional[np.ndarray] = None,
    xyz_scale: float = 1.0,
) -> None:
    """
    Write a labelled point cloud to .las format.

    - classification field  ← predicted class ID
    - intensity field        ← confidence score (scaled to uint16) if provided
    - RGB fields            ← colourised by class using CLASS_COLOURS palette

    The file can be opened in CloudCompare for visual QA.
    """
    from src.constants import CLASS_COLOURS

    output_path = Path(output_path)
    output_path.parent.mkdir(parents=True, exist_ok=True)

    # Undo XYZ normalisation if transform was applied
    xyz = features[:, :3].copy()
    if xyz_mean is not None:
        xyz = xyz * xyz_scale + xyz_mean

    # Build .las header
    header = laspy.LasHeader(point_format=2, version="1.4")
    las_out = laspy.LasData(header=header)
    las_out.x = xyz[:, 0]
    las_out.y = xyz[:, 1]
    las_out.z = xyz[:, 2]

    las_out.classification = labels.astype(np.uint8)

    # Colour by class
    colours = np.array([CLASS_COLOURS[l] for l in labels], dtype=np.uint16)
    colours = colours * 257  # scale 0-255 → 0-65535
    las_out.red   = colours[:, 0]
    las_out.green = colours[:, 1]
    las_out.blue  = colours[:, 2]

    # Encode confidence as intensity (optional)
    if confidences is not None:
        las_out.intensity = (confidences * 65535).astype(np.uint16)

    las_out.write(str(output_path))
    logger.info(f"Saved labelled cloud → {output_path}  ({len(xyz):,} points)")
