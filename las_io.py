"""
src/utils/las_io.py — LAS/LAZ read/write helpers and preprocessing.

Handles:
  - Reading .las / .laz files produced by drone LIDAR scanners
  - Extracting the 11-feature vector (XYZ + intensity + RGB + normals + density)
  - Voxel downsampling to reduce point count
  - Writing labelled point clouds back to .las for QA in CloudCompare
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


def load_las(
    path: str | Path,
    voxel_size: float = VOXEL_SIZE,
) -> Tuple[np.ndarray, Optional[np.ndarray]]:
    """
    Load a .las/.laz file, optionally downsample, and return:
      features : float32 (N, 11) — XYZ + intensity + RGB + nx,ny,nz + density
      labels   : int32   (N,)    — class IDs, or None if file has no labels
    """
    path = Path(path)
    logger.info(f"Loading {path.name} …")
    las = laspy.read(str(path))

    xyz = np.stack([las.x, las.y, las.z], axis=1).astype(np.float64)

    if hasattr(las, "intensity"):
        intensity = np.array(las.intensity, dtype=np.float32) / 65535.0
    else:
        logger.warning("No intensity channel; filling with 0.")
        intensity = np.zeros(len(xyz), dtype=np.float32)

    if all(hasattr(las, ch) for ch in ("red", "green", "blue")):
        r = np.array(las.red,   dtype=np.float32) / 65535.0
        g = np.array(las.green, dtype=np.float32) / 65535.0
        b = np.array(las.blue,  dtype=np.float32) / 65535.0
    else:
        logger.warning("No RGB channels; filling with 0. Accuracy will be lower.")
        r = g = b = np.zeros(len(xyz), dtype=np.float32)

    labels = np.array(las.classification, dtype=np.int32) if hasattr(las, "classification") else None

    pcd = o3d.geometry.PointCloud()
    pcd.points = o3d.utility.Vector3dVector(xyz)

    if voxel_size > 0:
        n_before = len(pcd.points)
        pcd, _, trace = pcd.voxel_down_sample_and_trace(
            voxel_size, pcd.get_min_bound(), pcd.get_max_bound()
        )
        keep_idx = np.array([idxs[0] for idxs in trace])
        xyz       = np.asarray(pcd.points, dtype=np.float32)
        intensity = intensity[keep_idx]
        r, g, b   = r[keep_idx], g[keep_idx], b[keep_idx]
        if labels is not None:
            labels = labels[keep_idx]
        logger.info(f"  {n_before:,} → {len(xyz):,} points after downsampling.")

    pcd.points = o3d.utility.Vector3dVector(xyz)
    pcd.estimate_normals(
        search_param=o3d.geometry.KDTreeSearchParamKNN(knn=KNN_NEIGHBOURS)
    )
    pcd.orient_normals_towards_camera_location(camera_location=[0, 0, 100])
    normals = np.asarray(pcd.normals, dtype=np.float32)

    from sklearn.neighbors import BallTree
    ball_tree = BallTree(xyz, metric="euclidean")
    counts    = ball_tree.query_radius(xyz, r=0.5, count_only=True)
    density   = np.log1p(counts).astype(np.float32)

    features = np.stack(
        [xyz[:, 0], xyz[:, 1], xyz[:, 2],
         intensity,
         r, g, b,
         normals[:, 0], normals[:, 1], normals[:, 2],
         density],
        axis=1,
    ).astype(np.float32)

    assert features.shape[1] == NUM_FEATURES
    return features, labels


def normalise_xyz_inplace(features: np.ndarray) -> Tuple[np.ndarray, float]:
    """Subtract centroid + scale XYZ so scene ≈ [-1,1]. Returns (mean, scale)."""
    xyz   = features[:, :3]
    mean  = xyz.mean(axis=0)
    scale = xyz.std() + 1e-8
    features[:, :3] = (xyz - mean) / scale
    return mean, scale


def save_labelled_las(
    output_path: str | Path,
    features: np.ndarray,
    labels: np.ndarray,
    confidences: Optional[np.ndarray] = None,
    xyz_mean: Optional[np.ndarray] = None,
    xyz_scale: float = 1.0,
) -> None:
    """Write a labelled point cloud to .las for CloudCompare QA."""
    from src.constants import CLASS_COLOURS

    output_path = Path(output_path)
    output_path.parent.mkdir(parents=True, exist_ok=True)

    xyz = features[:, :3].copy()
    if xyz_mean is not None:
        xyz = xyz * xyz_scale + xyz_mean

    header  = laspy.LasHeader(point_format=2, version="1.4")
    las_out = laspy.LasData(header=header)
    las_out.x = xyz[:, 0]
    las_out.y = xyz[:, 1]
    las_out.z = xyz[:, 2]
    las_out.classification = labels.astype(np.uint8)

    colours = np.array([CLASS_COLOURS[l] for l in labels], dtype=np.uint16) * 257
    las_out.red, las_out.green, las_out.blue = colours[:, 0], colours[:, 1], colours[:, 2]

    if confidences is not None:
        las_out.intensity = (confidences * 65535).astype(np.uint16)

    las_out.write(str(output_path))
    logger.info(f"Saved labelled cloud → {output_path}  ({len(xyz):,} pts)")
