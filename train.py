from __future__ import annotations

import argparse
import logging
import sys
from pathlib import Path

import torch
import torch.nn as nn
from torch.utils.data import DataLoader
from torch.utils.tensorboard import SummaryWriter
from tqdm import tqdm

# Resolve local src imports regardless of execution cwd.
ROOT_DIR = Path(__file__).resolve().parent
SRC_DIR = ROOT_DIR / "src"
for path in [SRC_DIR, SRC_DIR / "data", SRC_DIR / "models", SRC_DIR / "utils"]:
    path_str = str(path)
    if path_str not in sys.path:
        sys.path.insert(0, path_str)

from constants import LOG_DIR, NUM_CLASSES, RUN_DIR, VAL_INTERVAL, WEIGHTS_DIR
from datasets import CruxScanDataset
from randlanet import RandLANet

logger = logging.getLogger(__name__)


def build_dataloader(dataset: CruxScanDataset, batch_size: int, shuffle: bool, num_workers: int) -> DataLoader:
    return DataLoader(
        dataset,
        batch_size=batch_size,
        shuffle=shuffle,
        num_workers=num_workers,
        pin_memory=True,
        persistent_workers=num_workers > 0,
    )


def train(
    dataset_path: Path,
    epochs: int,
    batch_size: int,
    learning_rate: float,
    device: str,
    num_workers: int,
) -> None:
    RUN_DIR.mkdir(parents=True, exist_ok=True)
    WEIGHTS_DIR.mkdir(parents=True, exist_ok=True)
    LOG_DIR.mkdir(parents=True, exist_ok=True)

    logging.basicConfig(
        level=logging.INFO,
        format="%(asctime)s - %(levelname)s - %(message)s",
        handlers=[
            logging.FileHandler(RUN_DIR / "train.log"),
            logging.StreamHandler(),
        ],
    )

    writer = SummaryWriter(log_dir=str(LOG_DIR))

    train_dataset = CruxScanDataset(dataset_path / "train", augment=True)
    val_dataset = CruxScanDataset(dataset_path / "val", augment=False)

    train_loader = build_dataloader(train_dataset, batch_size=batch_size, shuffle=True, num_workers=num_workers)
    val_loader = build_dataloader(val_dataset, batch_size=batch_size, shuffle=False, num_workers=num_workers)

    logger.info("Using device: %s", device)
    model = RandLANet(num_classes=NUM_CLASSES).to(device)

    criterion = nn.CrossEntropyLoss()
    optimizer = torch.optim.Adam(model.parameters(), lr=learning_rate)
    scheduler = torch.optim.lr_scheduler.ExponentialLR(optimizer, gamma=0.95)

    best_val_loss = float("inf")

    for epoch in range(epochs):
        model.train()
        train_loss = 0.0
        train_correct = 0
        train_total = 0

        pbar = tqdm(train_loader, desc=f"Epoch {epoch + 1}/{epochs} [Train]")
        for features, labels, xyz in pbar:
            features = features.to(device, non_blocking=True)
            labels = labels.to(device, non_blocking=True)
            xyz = xyz.to(device, non_blocking=True)

            optimizer.zero_grad(set_to_none=True)
            outputs = model(xyz, features)
            loss = criterion(outputs, labels)
            loss.backward()
            optimizer.step()

            train_loss += loss.item() * features.size(0)
            predicted = outputs.argmax(dim=1)
            train_total += labels.numel()
            train_correct += (predicted == labels).sum().item()

            pbar.set_postfix(loss=f"{loss.item():.4f}")

        epoch_train_loss = train_loss / len(train_dataset)
        epoch_train_acc = train_correct / train_total if train_total > 0 else 0.0

        writer.add_scalar("Loss/Train", epoch_train_loss, epoch)
        writer.add_scalar("Accuracy/Train", epoch_train_acc, epoch)

        logger.info(
            "Epoch %d - Train Loss: %.4f, Train Acc: %.4f",
            epoch + 1,
            epoch_train_loss,
            epoch_train_acc,
        )

        scheduler.step()

        if (epoch + 1) % VAL_INTERVAL == 0:
            model.eval()
            val_loss = 0.0
            val_correct = 0
            val_total = 0

            with torch.no_grad():
                for features, labels, xyz in tqdm(val_loader, desc=f"Epoch {epoch + 1}/{epochs} [Val]"):
                    features = features.to(device, non_blocking=True)
                    labels = labels.to(device, non_blocking=True)
                    xyz = xyz.to(device, non_blocking=True)

                    outputs = model(xyz, features)
                    loss = criterion(outputs, labels)

                    val_loss += loss.item() * features.size(0)
                    predicted = outputs.argmax(dim=1)
                    val_total += labels.numel()
                    val_correct += (predicted == labels).sum().item()

            epoch_val_loss = val_loss / len(val_dataset)
            epoch_val_acc = val_correct / val_total if val_total > 0 else 0.0

            writer.add_scalar("Loss/Val", epoch_val_loss, epoch)
            writer.add_scalar("Accuracy/Val", epoch_val_acc, epoch)

            logger.info(
                "Epoch %d - Val Loss: %.4f, Val Acc: %.4f",
                epoch + 1,
                epoch_val_loss,
                epoch_val_acc,
            )

            if epoch_val_loss < best_val_loss:
                best_val_loss = epoch_val_loss
                torch.save(model.state_dict(), WEIGHTS_DIR / "best_model.pth")
                logger.info("Saved new best model to %s", WEIGHTS_DIR / "best_model.pth")

        torch.save(model.state_dict(), WEIGHTS_DIR / "latest_model.pth")

    writer.close()
    logger.info("Training complete.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Train RandLANet on Crux Dataset")
    parser.add_argument("--dataset", type=str, required=True, help="Path to processed dataset root")
    parser.add_argument("--epochs", type=int, default=100, help="Number of epochs")
    parser.add_argument("--batch_size", type=int, default=4, help="Batch size")
    parser.add_argument("--lr", type=float, default=0.01, help="Learning rate")
    parser.add_argument("--num_workers", type=int, default=4, help="DataLoader worker count")
    parser.add_argument(
        "--device",
        type=str,
        default="cuda" if torch.cuda.is_available() else "cpu",
        help="Device to use (cuda or cpu)",
    )

    args = parser.parse_args()

    train(
        dataset_path=Path(args.dataset),
        epochs=args.epochs,
        batch_size=args.batch_size,
        learning_rate=args.lr,
        device=args.device,
        num_workers=args.num_workers,
    )
