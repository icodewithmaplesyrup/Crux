"""
src/data/datasets.py — PyTorch Dataset wrappers for all training sources.

All datasets return (features, labels, xyz) where:
  features : float32 (N, NUM_FEATURES)
  labels   : int64   (N,)              — Crux class IDs
  xyz      : float32 (N, 3)            — normalised world-space coords
"""

from __future__ import annotations

import logging
import random
from pathlib import Path
from typing import Dict, List, Optional, Tuple

import numpy as np
import torch
from torch.utils.data import Dataset, ConcatDataset

from src.constants import (
    NUM_FEATURES,
    SEMANTICKITTI_REMAP,
    TORONTO3D_REMAP,
    S3DIS_REMAP,
    VOXEL_SIZE,
)

logger = logging.getLogger(__name__)

# Max points sampled per scene (random crop for memory budget)
MAX_POINTS: int = 65_536


def _remap_labels(labels: np.ndarray, remap: Dict[int, int]) -> np.ndarray:
    out = np.full_like(labels, fill_value=9)  # default → unknown
    for src, tgt in remap.items():
        out[labels == src] = tgt
    return out


def _random_crop(features: np.ndarray, labels: np.ndarray, max_pts: int) -> Tuple[np.ndarray, np.ndarray]:
    N = features.shape[0]
    if N <= max_pts:
        return features, labels
    idx = np.random.choice(N, max_pts, replace=False)
    return features[idx], labels[idx]


def _augment(features: np.ndarray) -> np.ndarray:
    """On-the-fly augmentations: random rotation, jitter, scale."""
    features = features.copy()
    xyz = features[:, :3]

    # Random Z-axis rotation
    theta = np.random.uniform(0, 2 * np.pi)
    c, s  = np.cos(theta), np.sin(theta)
    R = np.array([[c, -s, 0], [s, c, 0], [0, 0, 1]], dtype=np.float32)
    xyz = xyz @ R.T

    # Random scale (±10%)
    scale = np.random.uniform(0.9, 1.1)
    xyz  *= scale

    # Gaussian jitter
    xyz += np.random.normal(0, 0.01, xyz.shape).astype(np.float32)

    features[:, :3] = xyz

    # Intensity / colour jitter
    features[:, 3]   = np.clip(features[:, 3] + np.random.normal(0, 0.02), 0, 1)
    features[:, 4:7] = np.clip(features[:, 4:7] + np.random.normal(0, 0.02, (len(features), 3)), 0, 1)

    return features


# ---------------------------------------------------------------------------
# Crux scan dataset (your own labelled + unlabelled scans)
# ---------------------------------------------------------------------------

class CruxScanDataset(Dataset):
    """
    Loads all .las files from a directory.  Expects pre-processed files
    written by las_io.load_las() (i.e. the feature vector is already
    extracted and saved as companion .npy files).

    If a companion *_labels.npy file is absent, labels are all set to 9
    (unknown) — those scenes can still be used for active-learning inference.
    """

    def __init__(
        self,
        scan_dir: str | Path,
        augment: bool = False,
        max_points: int = MAX_POINTS,
    ):
        self.scan_dir  = Path(scan_dir)
        self.augment   = augment
        self.max_points = max_points
        self.feat_files = sorted(self.scan_dir.glob("*_features.npy"))
        if not self.feat_files:
            raise FileNotFoundError(
                f"No *_features.npy files found in {scan_dir}. "
                "Run scripts/preprocess.py first."
            )
        logger.info(f"CruxScanDataset: {len(self.feat_files)} scenes in {scan_dir}")

    def __len__(self) -> int:
        return len(self.feat_files)

    def __getitem__(self, idx: int) -> Tuple[torch.Tensor, torch.Tensor, torch.Tensor]:
        feat_path  = self.feat_files[idx]
        label_path = feat_path.parent / feat_path.name.replace("_features.npy", "_labels.npy")

        features = np.load(feat_path)
        labels   = np.load(label_path) if label_path.exists() else np.full(len(features), 9, dtype=np.int32)

        features, labels = _random_crop(features, labels, self.max_points)

        if self.augment:
            features = _augment(features)

        xyz = features[:, :3].copy()

        return (
            torch.from_numpy(features).float(),
            torch.from_numpy(labels).long(),
            torch.from_numpy(xyz).float(),
        )


# ---------------------------------------------------------------------------
# SemanticKITTI dataset
# ---------------------------------------------------------------------------

class SemanticKITTIDataset(Dataset):
    """
    Reads SemanticKITTI binary point clouds (.bin) + label files (.label).
    Directory layout expected:
        root/
          sequences/
            00/velodyne/000000.bin   <- XYZIntensity float32×4
            00/labels/000000.label   <- uint32 per point
    """

    def __init__(
        self,
        root: str | Path,
        split: str = "train",        # "train" | "val" | "test"
        augment: bool = False,
        max_points: int = MAX_POINTS,
    ):
        self.augment    = augment
        self.max_points = max_points

        SPLITS = {
            "train": list(range(11)),
            "val":   [8],
            "test":  list(range(11, 22)),
        }
        seqs = SPLITS[split]
        root = Path(root)

        self.samples: List[Tuple[Path, Path]] = []
        for seq in seqs:
            vel_dir   = root / "sequences" / f"{seq:02d}" / "velodyne"
            label_dir = root / "sequences" / f"{seq:02d}" / "labels"
            if not vel_dir.exists():
                continue
            for fp in sorted(vel_dir.glob("*.bin")):
                lp = label_dir / fp.with_suffix(".label").name
                self.samples.append((fp, lp))

        logger.info(f"SemanticKITTI {split}: {len(self.samples):,} scans")

    def __len__(self) -> int:
        return len(self.samples)

    def __getitem__(self, idx: int):
        fp, lp = self.samples[idx]
        pts    = np.fromfile(fp, dtype=np.float32).reshape(-1, 4)
        xyz, intensity = pts[:, :3], pts[:, 3]

        if lp.exists():
            raw_labels  = np.fromfile(lp, dtype=np.uint32) & 0xFFFF
            labels      = _remap_labels(raw_labels.astype(np.int32), SEMANTICKITTI_REMAP)
        else:
            labels = np.full(len(xyz), 9, dtype=np.int32)

        # Build feature vector (no RGB for KITTI, no normals — fill zeros)
        N = len(xyz)
        features = np.zeros((N, NUM_FEATURES), dtype=np.float32)
        features[:, :3]  = xyz
        features[:, 3]   = np.clip(intensity, 0, 1)
        # normals and density left as 0 — filled during fine-tune preprocessing

        features, labels = _random_crop(features, labels, self.max_points)
        if self.augment:
            features = _augment(features)

        xyz_out = features[:, :3].copy()
        return (
            torch.from_numpy(features).float(),
            torch.from_numpy(labels).long(),
            torch.from_numpy(xyz_out).float(),
        )


# ---------------------------------------------------------------------------
# Toronto-3D dataset
# ---------------------------------------------------------------------------

class Toronto3DDataset(Dataset):
    """
    Reads Toronto-3D .ply files.  Each file contains a single block.
    Expected fields: x, y, z, intensity, r, g, b, scalar_Label (1-indexed).
    """

    def __init__(
        self,
        root: str | Path,
        split: str = "train",
        augment: bool = False,
        max_points: int = MAX_POINTS,
    ):
        self.augment    = augment
        self.max_points = max_points
        root = Path(root)
        all_files = sorted(root.glob("*.ply"))
        # Hold out L002 for validation (standard Toronto-3D split)
        if split == "val":
            self.files = [f for f in all_files if "L002" in f.name]
        else:
            self.files = [f for f in all_files if "L002" not in f.name]
        logger.info(f"Toronto3D {split}: {len(self.files)} blocks")

    def __len__(self) -> int:
        return len(self.files)

    def __getitem__(self, idx: int):
        from plyfile import PlyData
        ply    = PlyData.read(str(self.files[idx]))
        verts  = ply["vertex"]

        xyz       = np.stack([verts["x"], verts["y"], verts["z"]], axis=1).astype(np.float32)
        intensity = np.array(verts["intensity"], dtype=np.float32) / 65535.0 if "intensity" in verts.dtype.names else np.zeros(len(xyz), np.float32)
        r = np.array(verts["red"],   dtype=np.float32) / 255.0 if "red" in verts.dtype.names else np.zeros(len(xyz), np.float32)
        g = np.array(verts["green"], dtype=np.float32) / 255.0 if "green" in verts.dtype.names else np.zeros(len(xyz), np.float32)
        b = np.array(verts["blue"],  dtype=np.float32) / 255.0 if "blue" in verts.dtype.names else np.zeros(len(xyz), np.float32)

        raw_labels = np.array(verts["scalar_Label"], dtype=np.int32) - 1  # 1-indexed → 0-indexed
        labels     = _remap_labels(raw_labels, TORONTO3D_REMAP)

        N = len(xyz)
        features = np.zeros((N, NUM_FEATURES), dtype=np.float32)
        features[:, :3]  = xyz
        features[:, 3]   = intensity
        features[:, 4:7] = np.stack([r, g, b], axis=1)

        features, labels = _random_crop(features, labels, self.max_points)
        if self.augment:
            features = _augment(features)

        return (
            torch.from_numpy(features).float(),
            torch.from_numpy(labels).long(),
            torch.from_numpy(features[:, :3].copy()).float(),
        )


# ---------------------------------------------------------------------------
# S3DIS dataset
# ---------------------------------------------------------------------------

class S3DISDataset(Dataset):
    """
    Reads Stanford S3DIS in the standard .npy preprocessed format:
    each file is (N, 9): x y z r g b nx ny nz  + a separate _labels.npy.
    """

    def __init__(
        self,
        root: str | Path,
        fold: int = 5,            # held-out area (1-6)
        split: str = "train",
        augment: bool = False,
        max_points: int = MAX_POINTS,
    ):
        self.augment    = augment
        self.max_points = max_points
        root = Path(root)

        area_dirs = sorted(root.glob("Area_*/"))
        if split == "val":
            area_dirs = [d for d in area_dirs if f"Area_{fold}" in d.name]
        else:
            area_dirs = [d for d in area_dirs if f"Area_{fold}" not in d.name]

        self.files: List[Tuple[Path, Path]] = []
        for area in area_dirs:
            for fp in sorted(area.glob("*_features.npy")):
                lp = fp.parent / fp.name.replace("_features.npy", "_labels.npy")
                if lp.exists():
                    self.files.append((fp, lp))

        logger.info(f"S3DIS fold={fold} {split}: {len(self.files)} rooms")

    def __len__(self) -> int:
        return len(self.files)

    def __getitem__(self, idx: int):
        fp, lp   = self.files[idx]
        features = np.load(fp).astype(np.float32)
        raw_lab  = np.load(lp).astype(np.int32)
        labels   = _remap_labels(raw_lab, S3DIS_REMAP)

        # Pad / trim to NUM_FEATURES
        if features.shape[1] < NUM_FEATURES:
            pad = np.zeros((len(features), NUM_FEATURES - features.shape[1]), np.float32)
            features = np.concatenate([features, pad], axis=1)
        else:
            features = features[:, :NUM_FEATURES]

        features, labels = _random_crop(features, labels, self.max_points)
        if self.augment:
            features = _augment(features)

        return (
            torch.from_numpy(features).float(),
            torch.from_numpy(labels).long(),
            torch.from_numpy(features[:, :3].copy()).float(),
        )


# ---------------------------------------------------------------------------
# Convenience builder
# ---------------------------------------------------------------------------

def build_pretrain_dataset(cfg) -> ConcatDataset:
    """Build a combined pretrain dataset from omegaconf config."""
    parts = []
    if cfg.data.semantickitti.enabled:
        parts.append(SemanticKITTIDataset(cfg.data.semantickitti.root, split="train", augment=True))
    if cfg.data.toronto3d.enabled:
        parts.append(Toronto3DDataset(cfg.data.toronto3d.root, split="train", augment=True))
    if cfg.data.s3dis.enabled:
        parts.append(S3DISDataset(cfg.data.s3dis.root, split="train", augment=True))
    if not parts:
        raise ValueError("No datasets enabled in config.")
    return ConcatDataset(parts)


def build_finetune_dataset(cfg, split: str = "train") -> CruxScanDataset:
    return CruxScanDataset(cfg.data.crux.scan_dir, augment=(split == "train"))
