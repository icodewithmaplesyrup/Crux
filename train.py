"""Minimal training entrypoint for Crux RandLA-Net.

Examples:
  python train.py --scan-dir data/crux_scans --epochs 30 --batch-size 2
  python train.py --scan-dir data/crux_scans --raw-scan-dir scans_raw --epochs 30
"""

from __future__ import annotations

import argparse
from pathlib import Path

import numpy as np
import torch
import torch.nn.functional as F
from torch.utils.data import DataLoader, random_split

from datasets import CruxScanDataset
from las_io import load_las, normalise_xyz_inplace
from randlanet import RandLANet


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(description="Train RandLA-Net on local Crux features")
    p.add_argument("--scan-dir", type=str, required=True, help="Directory for *_features.npy / *_labels.npy")
    p.add_argument("--raw-scan-dir", type=str, default=None, help="Optional folder with raw .las/.laz to preprocess")
    p.add_argument("--epochs", type=int, default=30)
    p.add_argument("--batch-size", type=int, default=2)
    p.add_argument("--lr", type=float, default=1e-3)
    p.add_argument("--num-workers", type=int, default=0)
    p.add_argument("--val-split", type=float, default=0.2)
    p.add_argument("--device", type=str, default="cuda" if torch.cuda.is_available() else "cpu")
    p.add_argument("--save-dir", type=str, default="checkpoints")
    return p.parse_args()


def preprocess_raw_scans(raw_scan_dir: Path, scan_dir: Path) -> None:
    scan_dir.mkdir(parents=True, exist_ok=True)
    raw_files = sorted(list(raw_scan_dir.glob("*.las")) + list(raw_scan_dir.glob("*.laz")))
    if not raw_files:
        raise FileNotFoundError(f"No .las/.laz files found in {raw_scan_dir}")

    for fp in raw_files:
        base = fp.stem
        feat_out = scan_dir / f"{base}_features.npy"
        lab_out = scan_dir / f"{base}_labels.npy"
        if feat_out.exists() and lab_out.exists():
            continue

        features, labels = load_las(fp)
        normalise_xyz_inplace(features)
        np.save(feat_out, features.astype(np.float32))
        if labels is not None:
            np.save(lab_out, labels.astype(np.int32))
        print(f"preprocessed: {fp.name} -> {feat_out.name}")


def run_epoch(model, loader, optimizer, device, train: bool) -> float:
    model.train(train)
    total_loss = 0.0
    steps = 0

    for features, labels, xyz in loader:
        features = features.to(device)
        labels = labels.to(device)
        xyz = xyz.to(device)

        logits = model(xyz, features)
        loss = F.cross_entropy(logits, labels)

        if train:
            optimizer.zero_grad(set_to_none=True)
            loss.backward()
            optimizer.step()

        total_loss += float(loss.item())
        steps += 1

    return total_loss / max(steps, 1)


def main() -> None:
    args = parse_args()
    device = torch.device(args.device)
    scan_dir = Path(args.scan_dir)

    feature_files = sorted(scan_dir.glob("*_features.npy")) if scan_dir.exists() else []
    if not feature_files:
        if args.raw_scan_dir is None:
            raise FileNotFoundError(
                f"No *_features.npy files found in {scan_dir}. "
                "Pass --raw-scan-dir to auto-preprocess .las/.laz files first."
            )
        preprocess_raw_scans(Path(args.raw_scan_dir), scan_dir)

    full_ds = CruxScanDataset(scan_dir, augment=True)
    val_size = max(1, int(len(full_ds) * args.val_split))
    train_size = max(1, len(full_ds) - val_size)
    if train_size + val_size > len(full_ds):
        val_size = len(full_ds) - train_size

    train_ds, val_ds = random_split(full_ds, [train_size, val_size])
    train_loader = DataLoader(train_ds, batch_size=args.batch_size, shuffle=True, num_workers=args.num_workers)
    val_loader = DataLoader(val_ds, batch_size=args.batch_size, shuffle=False, num_workers=args.num_workers)

    model = RandLANet().to(device)
    optimizer = torch.optim.Adam(model.parameters(), lr=args.lr)

    save_dir = Path(args.save_dir)
    save_dir.mkdir(parents=True, exist_ok=True)

    best_val = float("inf")
    for epoch in range(1, args.epochs + 1):
        train_loss = run_epoch(model, train_loader, optimizer, device, train=True)
        val_loss = run_epoch(model, val_loader, optimizer, device, train=False)

        print(f"epoch={epoch:03d} train_loss={train_loss:.4f} val_loss={val_loss:.4f}")

        ckpt = {
            "epoch": epoch,
            "model_state": model.state_dict(),
            "optimizer_state": optimizer.state_dict(),
            "train_loss": train_loss,
            "val_loss": val_loss,
            "args": vars(args),
        }
        torch.save(ckpt, save_dir / "last.pth")
        if val_loss < best_val:
            best_val = val_loss
            torch.save(ckpt, save_dir / "best.pth")


if __name__ == "__main__":
    main()
