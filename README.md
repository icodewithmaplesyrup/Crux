# Crux — Pipeline A: 3D Semantic Segmentation

Drone LIDAR point cloud → per-point material labels → surface region polygons → Unreal 5 material assignment.

## Architecture overview

```
Raw scan (.las/.laz)
    └─► preprocessing.py        # noise filter, voxel downsample, normal estimation
        └─► feature_extractor.py # XYZ + intensity + RGB + normals + density
            └─► randlanet.py     # RandLA-Net forward pass
                └─► postprocess.py # confidence filter, region aggregation, polygon export
                    └─► ue5_export.py # JSON/CSV for UE5 material assignment layer
```

## Target classes (v1)

| ID | Class | Notes |
|----|-------|-------|
| 0 | concrete | Pathways, walls, foundations |
| 1 | asphalt | Roads, parking — similar geometry to concrete, RGB disambiguates |
| 2 | grass/vegetation | Parks, verges |
| 3 | metal_structural | Beams, railings, grates |
| 4 | metal_litter | Cans, foil, small objects — different gameplay implications |
| 5 | wood | Benches, crates, pallets |
| 6 | water | Puddles, drains, fountains |
| 7 | glass | Windows, bottles |
| 8 | gravel_dirt | Unpaved surfaces |
| 9 | unknown | Low-confidence catch-all |

## Quick start

```bash
# 1. Install deps
pip install -r requirements.txt

# 2. Download pretrain datasets
python scripts/download_datasets.py --datasets semantickitti toronto3d s3dis

# 3. Pretrain on public data
python scripts/train.py --config configs/pretrain.yaml

# 4. Fine-tune on your labeled Crux scan
python scripts/train.py --config configs/finetune.yaml --checkpoint checkpoints/pretrain_best.pth

# 5. Run inference on a new scan
python scripts/infer.py --input scans/plaza.laz --checkpoint checkpoints/finetune_best.pth --output outputs/plaza_labeled.las

# 6. Active learning: surface uncertain points for labeling
python scripts/active_learning.py --input scans/new_area.laz --checkpoint checkpoints/finetune_best.pth --budget 5000
```

## Training data strategy

1. **Pretrain** on SemanticKITTI + Toronto-3D + S3DIS (~150k labeled scenes)
2. **Manual label** one small Crux scan area with CloudCompare or Semantic Segmentation Editor
3. **Fine-tune** for 20–40 epochs with lower LR and class-balanced sampling
4. **Active learning loop**: model flags uncertain points → human labels → retrain → repeat

## Directory layout

```
crux_pipeline/
├── configs/            # YAML training configs
├── scripts/            # CLI entry points
├── src/
│   ├── data/           # Dataset loaders, augmentation, normalization
│   ├── models/         # RandLA-Net + PointNet++ (baseline)
│   ├── training/       # Trainer, loss functions, metrics
│   ├── inference/      # Batch inference, confidence scoring
│   ├── active_learning/# Uncertainty sampling, labeling queue
│   └── utils/          # LAS I/O, export, logging
├── tests/
└── requirements.txt
```
