from __future__ import annotations

import sys
import os

# ---------------------------------------------------------------------------
# Path Fix: Dynamically add src and its subfolders to Python's search path
# ---------------------------------------------------------------------------
current_dir = os.path.dirname(os.path.abspath(__file__))
src_path = os.path.join(current_dir, 'src')

if src_path not in sys.path:
    sys.path.insert(0, src_path)

# Also explicitly add the subdirectories so direct imports work everywhere
for folder in ['data', 'models', 'utils']:
    subfolder_path = os.path.join(src_path, folder)
    if subfolder_path not in sys.path:
        sys.path.insert(0, subfolder_path)
# ---------------------------------------------------------------------------

import argparse
import logging
from pathlib import Path

import torch
import torch.nn as nn
from torch.utils.data import DataLoader
from torch.utils.tensorboard import SummaryWriter
from tqdm import tqdm

# These imports will now resolve perfectly from the src/ directory
from datasets import CruxScanDataset
from randlanet import RandLANet
from constants import (
    LOG_DIR,
    NUM_CLASSES,
    RUN_DIR,
    VAL_INTERVAL,
    WEIGHTS_DIR,
)

logger = logging.getLogger(__name__)


def train(
    dataset_path: Path,
    epochs: int,
    batch_size: int,
    learning_rate: float,
    device: str,
) -> None:
    # Set up directories
    RUN_DIR.mkdir(parents=True, exist_ok=True)
    WEIGHTS_DIR.mkdir(parents=True, exist_ok=True)
    LOG_DIR.mkdir(parents=True, exist_ok=True)

    # Set up logging
    logging.basicConfig(
        level=logging.INFO,
        format="%(asctime)s - %(levelname)s - %(message)s",
        handlers=[
            logging.FileHandler(RUN_DIR / "train.log"),
            logging.StreamHandler(),
        ],
    )
    writer = SummaryWriter(log_dir=str(LOG_DIR))

    logger.info("Loading datasets...")
    train_dataset = CruxScanDataset(dataset_path / "train", split="train")
    val_dataset = CruxScanDataset(dataset_path / "val", split="val")

    train_loader = DataLoader(
        train_dataset,
        batch_size=batch_size,
        shuffle=True,
        num_workers=4,
        pin_memory=True,
    )
    val_loader = DataLoader(
        val_dataset,
        batch_size=batch_size,
        shuffle=False,
        num_workers=4,
        pin_memory=True,
    )

    logger.info(f"Using device: {device}")
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

        pbar = tqdm(train_loader, desc=f"Epoch {epoch+1}/{epochs} [Train]")
        for batch in pbar:
            points = batch["points"].to(device)
            labels = batch["labels"].to(device)

            optimizer.zero_grad()
            outputs = model(points)
            loss = criterion(outputs, labels)
            loss.backward()
            optimizer.step()

            train_loss += loss.item() * points.size(0)
            _, predicted = torch.max(outputs, 1)
            train_total += labels.numel()
            train_correct += (predicted == labels).sum().item()

            pbar.set_postfix({"loss": loss.item()})

        epoch_train_loss = train_loss / len(train_dataset)
        epoch_train_acc = train_correct / train_total

        writer.add_scalar("Loss/Train", epoch_train_loss, epoch)
        writer.add_scalar("Accuracy/Train", epoch_train_acc, epoch)

        logger.info(
            f"Epoch {epoch+1} - Train Loss: {epoch_train_loss:.4f}, Train Acc: {epoch_train_acc:.4f}"
        )

        scheduler.step()

        if (epoch + 1) % VAL_INTERVAL == 0:
            model.eval()
            val_loss = 0.0
            val_correct = 0
            val_total = 0

            with torch.no_grad():
                for batch in tqdm(val_loader, desc=f"Epoch {epoch+1}/{epochs} [Val]"):
                    points = batch["points"].to(device)
                    labels = batch["labels"].to(device)

                    outputs = model(points)
                    loss = criterion(outputs, labels)

                    val_loss += loss.item() * points.size(0)
                    _, predicted = torch.max(outputs, 1)
                    val_total += labels.numel()
                    val_correct += (predicted == labels).sum().item()

            epoch_val_loss = val_loss / len(val_dataset)
            epoch_val_acc = val_correct / val_total

            writer.add_scalar("Loss/Val", epoch_val_loss, epoch)
            writer.add_scalar("Accuracy/Val", epoch_val_acc, epoch)

            logger.info(
                f"Epoch {epoch+1} - Val Loss: {epoch_val_loss:.4f}, Val Acc: {epoch_val_acc:.4f}"
            )

            # Save best weights
            if epoch_val_loss < best_val_loss:
                best_val_loss = epoch_val_loss
                torch.save(model.state_dict(), WEIGHTS_DIR / "best_model.pth")
                logger.info(f"Saved new best model to {WEIGHTS_DIR / 'best_model.pth'}")

        # Save latest weights
        torch.save(model.state_dict(), WEIGHTS_DIR / "latest_model.pth")

    writer.close()
    logger.info("Training complete.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Train RandLANet on Crux Dataset")
    parser.add_argument(
        "--dataset",
        type=str,
        required=True,
        help="Path to the processed dataset directory",
    )
    parser.add_argument("--epochs", type=int, default=100, help="Number of epochs")
    parser.add_argument("--batch_size", type=int, default=4, help="Batch size")
    parser.add_argument("--lr", type=float, default=0.01, help="Learning rate")
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
    )
