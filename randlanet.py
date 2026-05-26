"""
src/models/randlanet.py — RandLA-Net for large-scale point cloud segmentation.

Paper: "RandLA-Net: Efficient Semantic Segmentation of Large-Scale Point Clouds"
       Hu et al., CVPR 2020.  https://arxiv.org/abs/1911.11236

Architecture:
  Input (N, 11) → 4× encoder stages (random sampling + LFA) →
  4× decoder stages (nearest-neighbour upsample) → MLP head → (N, NUM_CLASSES)

Each Local Feature Aggregation (LFA) block:
  Relative-point encoding → N×K attentive pooling → dilated residual block
"""

from __future__ import annotations

import torch
import torch.nn as nn
import torch.nn.functional as F

from src.constants import NUM_CLASSES, NUM_FEATURES


# ---------------------------------------------------------------------------
# Building blocks
# ---------------------------------------------------------------------------

class SharedMLP(nn.Sequential):
    """1-D convolution acting as a per-point shared MLP."""

    def __init__(self, in_ch: int, *channels: int, bn: bool = True, act: bool = True):
        layers: list[nn.Module] = []
        for out_ch in channels:
            layers.append(nn.Conv1d(in_ch, out_ch, 1, bias=not bn))
            if bn:
                layers.append(nn.BatchNorm1d(out_ch))
            if act:
                layers.append(nn.LeakyReLU(0.2, inplace=True))
            in_ch = out_ch
        super().__init__(*layers)


class RelativePointEncoding(nn.Module):
    """
    Encodes relative geometry between a centre point and its K neighbours.
    Concatenates: [pi, pj, pi-pj, ||pi-pj||] → 10-D, then lifts to d_out.
    """

    def __init__(self, d_out: int):
        super().__init__()
        self.mlp = SharedMLP(10, d_out // 2, d_out)

    def forward(self, xyz: torch.Tensor, knn_xyz: torch.Tensor) -> torch.Tensor:
        """
        xyz     : (B, N, 3)
        knn_xyz : (B, N, K, 3)
        returns : (B, d_out, N, K)
        """
        B, N, K, _ = knn_xyz.shape
        pi = xyz.unsqueeze(2).expand_as(knn_xyz)          # (B, N, K, 3)
        diff = pi - knn_xyz
        dist = torch.norm(diff, dim=-1, keepdim=True)      # (B, N, K, 1)
        rel  = torch.cat([pi, knn_xyz, diff, dist], dim=-1)  # (B, N, K, 10)
        # Reshape for Conv1d: treat each (point, neighbour) pair as a "point"
        rel  = rel.view(B, N * K, 10).permute(0, 2, 1)    # (B, 10, N*K)
        out  = self.mlp(rel)                                # (B, d_out, N*K)
        return out.view(B, -1, N, K)                        # (B, d_out, N, K)


class AttentivePooling(nn.Module):
    """Soft attention over K neighbours → (B, d_out, N)."""

    def __init__(self, in_ch: int, d_out: int):
        super().__init__()
        self.score_fn = nn.Sequential(
            nn.Conv2d(in_ch, in_ch, 1, bias=False),
            nn.Softmax(dim=-1),
        )
        self.mlp = SharedMLP(in_ch, d_out)

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        """x: (B, C, N, K) → (B, d_out, N)"""
        scores  = self.score_fn(x)               # (B, C, N, K)
        pooled  = (x * scores).sum(dim=-1)       # (B, C, N)
        return self.mlp(pooled)                  # (B, d_out, N)


class LocalFeatureAggregation(nn.Module):
    """
    One LFA block: relative encoding → two attentive pooling steps →
    dilated residual connection.
    """

    def __init__(self, in_ch: int, d_out: int, k: int = 16):
        super().__init__()
        self.k = k
        self.rpe    = RelativePointEncoding(d_out)
        self.pool1  = AttentivePooling(d_out + in_ch, d_out // 2)
        self.rpe2   = RelativePointEncoding(d_out // 2)
        self.pool2  = AttentivePooling(d_out // 2 + d_out // 2, d_out)
        self.short  = SharedMLP(in_ch, d_out, bn=True, act=False)
        self.lrelu  = nn.LeakyReLU(0.2, inplace=True)

    def forward(
        self,
        xyz: torch.Tensor,
        feats: torch.Tensor,
        knn_idx: torch.Tensor,
    ) -> torch.Tensor:
        """
        xyz     : (B, N, 3)
        feats   : (B, C, N)
        knn_idx : (B, N, K)
        """
        B, C, N = feats.shape
        K = knn_idx.shape[-1]

        knn_xyz = _gather(xyz.permute(0, 2, 1), knn_idx)   # (B, 3, N, K)
        knn_xyz = knn_xyz.permute(0, 2, 3, 1)              # (B, N, K, 3)

        knn_f   = _gather(feats, knn_idx)                   # (B, C, N, K)

        # First aggregation
        rel1   = self.rpe(xyz, knn_xyz)                     # (B, d_out, N, K)
        cat1   = torch.cat([rel1, knn_f], dim=1)            # (B, d_out+C, N, K)
        agg1   = self.pool1(cat1)                           # (B, d_out//2, N)

        # Second aggregation
        knn_f2 = _gather(agg1, knn_idx)
        rel2   = self.rpe2(xyz, knn_xyz)
        cat2   = torch.cat([rel2, knn_f2], dim=1)
        agg2   = self.pool2(cat2)                           # (B, d_out, N)

        # Residual
        return self.lrelu(agg2 + self.short(feats))


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _gather(x: torch.Tensor, idx: torch.Tensor) -> torch.Tensor:
    """
    x   : (B, C, N)  or  (B, C, N) expanded to (B, C, N, K)
    idx : (B, N, K)
    returns (B, C, N, K)
    """
    B, C, N = x.shape
    K = idx.shape[-1]
    idx_exp = idx.unsqueeze(1).expand(B, C, -1, K)           # (B, C, N, K)
    x_exp   = x.unsqueeze(-1).expand(B, C, N, K)             # won't work directly
    # Need to gather along the point dimension
    idx_flat = idx.reshape(B, -1)                             # (B, N*K)
    x_flat   = x                                              # (B, C, N)
    out = torch.gather(
        x_flat.unsqueeze(-1).expand(B, C, N, K).reshape(B, C, N * K),
        2,
        idx_flat.unsqueeze(1).expand(B, C, N * K),
    )
    return out.view(B, C, idx.shape[1], K)


def _fps(xyz: torch.Tensor, n_sample: int) -> torch.Tensor:
    """
    Farthest point sampling.
    xyz      : (B, N, 3)
    n_sample : M
    returns  : (B, M) indices
    """
    B, N, _ = xyz.shape
    device = xyz.device
    idx     = torch.zeros(B, n_sample, dtype=torch.long, device=device)
    dist    = torch.full((B, N), float("inf"), device=device)
    # Start from a random point per batch
    cur = torch.randint(0, N, (B,), device=device)
    for i in range(n_sample):
        idx[:, i] = cur
        cur_xyz   = xyz[torch.arange(B), cur, :].unsqueeze(1)  # (B, 1, 3)
        d = ((xyz - cur_xyz) ** 2).sum(-1)
        dist      = torch.min(dist, d)
        cur       = dist.argmax(dim=1)
    return idx


def _knn(xyz: torch.Tensor, k: int) -> torch.Tensor:
    """
    Brute-force KNN.
    xyz : (B, N, 3)
    returns (B, N, k) — indices of k nearest neighbours (excluding self).
    """
    # Pairwise squared distances
    diff  = xyz.unsqueeze(2) - xyz.unsqueeze(1)           # (B, N, N, 3)
    dist2 = (diff ** 2).sum(-1)                            # (B, N, N)
    # Exclude self by setting diagonal to large value
    B, N, _ = dist2.shape
    eye = torch.eye(N, dtype=torch.bool, device=xyz.device).unsqueeze(0)
    dist2.masked_fill_(eye, float("inf"))
    return dist2.topk(k, largest=False).indices            # (B, N, k)


# ---------------------------------------------------------------------------
# Full RandLA-Net
# ---------------------------------------------------------------------------

class RandLANet(nn.Module):
    """
    RandLA-Net encoder–decoder for per-point semantic segmentation.

    Stages: 4× downsample by 4× at each level → bottleneck → 4× upsample.
    Feature dims follow the paper: 8→16→64→128→256→512 at bottleneck.
    """

    def __init__(
        self,
        in_ch: int   = NUM_FEATURES,
        num_classes: int = NUM_CLASSES,
        d_base: int  = 8,      # base channel multiplier
        k: int       = 16,     # KNN neighbours per LFA
        decimation: int = 4,   # random subsampling ratio per stage
    ):
        super().__init__()
        self.k          = k
        self.decimation = decimation

        # Stem: lift raw features to d_base dims
        self.stem = SharedMLP(in_ch, d_base)

        # Encoder (4 stages)
        self.enc1 = LocalFeatureAggregation(d_base,       d_base * 2,  k)
        self.enc2 = LocalFeatureAggregation(d_base * 2,   d_base * 8,  k)
        self.enc3 = LocalFeatureAggregation(d_base * 8,   d_base * 16, k)
        self.enc4 = LocalFeatureAggregation(d_base * 16,  d_base * 32, k)

        # Bottleneck MLP
        self.bottleneck = SharedMLP(d_base * 32, d_base * 64)

        # Decoder (4 stages) — channel dims mirror the encoder skip connections
        self.dec4 = SharedMLP(d_base * 64 + d_base * 32, d_base * 32)
        self.dec3 = SharedMLP(d_base * 32 + d_base * 16, d_base * 16)
        self.dec2 = SharedMLP(d_base * 16 + d_base * 8,  d_base * 8)
        self.dec1 = SharedMLP(d_base * 8  + d_base * 2,  d_base * 4)

        # Segmentation head
        self.head = nn.Sequential(
            SharedMLP(d_base * 4, d_base * 4),
            nn.Dropout(0.5),
            nn.Conv1d(d_base * 4, num_classes, 1),
        )

    def forward(self, xyz: torch.Tensor, feats: torch.Tensor) -> torch.Tensor:
        """
        xyz   : (B, N, 3)   — world-space XYZ (normalised)
        feats : (B, N, C)   — full feature vector
        returns logits (B, num_classes, N)
        """
        B, N, _ = xyz.shape
        f = feats.permute(0, 2, 1)   # (B, C, N)
        f = self.stem(f)              # (B, d_base, N)

        # ---- Encoder ----
        knn1 = _knn(xyz, self.k)
        f1   = self.enc1(xyz, f, knn1)                     # (B, d_base*2, N)

        # Subsample
        n1   = N // self.decimation
        s1   = _fps(xyz, n1)                               # (B, n1)
        xyz1 = xyz[torch.arange(B).unsqueeze(1), s1]       # (B, n1, 3)
        f1s  = f1[torch.arange(B).unsqueeze(1).expand(B, n1),
                   :,
                   s1.unsqueeze(1).expand(B, f1.shape[1], n1).permute(0,2,1)
                  ]  # simple gather handled below
        f1s  = _take(f1, s1)

        knn2 = _knn(xyz1, self.k)
        f2   = self.enc2(xyz1, f1s, knn2)

        n2   = n1 // self.decimation
        s2   = _fps(xyz1, n2)
        xyz2 = xyz1[torch.arange(B).unsqueeze(1), s2]
        f2s  = _take(f2, s2)

        knn3 = _knn(xyz2, self.k)
        f3   = self.enc3(xyz2, f2s, knn3)

        n3   = n2 // self.decimation
        s3   = _fps(xyz2, n3)
        xyz3 = xyz2[torch.arange(B).unsqueeze(1), s3]
        f3s  = _take(f3, s3)

        knn4 = _knn(xyz3, self.k)
        f4   = self.enc4(xyz3, f3s, knn4)

        n4   = n3 // self.decimation
        s4   = _fps(xyz3, n4)
        xyz4 = xyz3[torch.arange(B).unsqueeze(1), s4]
        f4s  = _take(f4, s4)

        # ---- Bottleneck ----
        fb = self.bottleneck(f4s)                          # (B, d*64, n4)

        # ---- Decoder (nearest-neighbour upsample + skip) ----
        fb_up = _nn_upsample(fb, xyz4, xyz3)
        f_d4  = self.dec4(torch.cat([fb_up, f4], dim=1))

        f_d4_up = _nn_upsample(f_d4, xyz3, xyz2)
        f_d3    = self.dec3(torch.cat([f_d4_up, f3], dim=1))

        f_d3_up = _nn_upsample(f_d3, xyz2, xyz1)
        f_d2    = self.dec2(torch.cat([f_d3_up, f2], dim=1))

        f_d2_up = _nn_upsample(f_d2, xyz1, xyz)
        f_d1    = self.dec1(torch.cat([f_d2_up, f1], dim=1))

        return self.head(f_d1)   # (B, num_classes, N)


def _take(feats: torch.Tensor, idx: torch.Tensor) -> torch.Tensor:
    """feats (B,C,N), idx (B,M) → (B,C,M)"""
    B, C, N = feats.shape
    M = idx.shape[1]
    return feats.gather(2, idx.unsqueeze(1).expand(B, C, M))


def _nn_upsample(
    feats: torch.Tensor,
    src_xyz: torch.Tensor,
    tgt_xyz: torch.Tensor,
) -> torch.Tensor:
    """
    Nearest-neighbour upsample: for each point in tgt_xyz find its
    closest point in src_xyz and copy its features.

    feats   : (B, C, M)
    src_xyz : (B, M, 3)
    tgt_xyz : (B, N, 3)
    returns : (B, C, N)
    """
    B, C, M = feats.shape
    N = tgt_xyz.shape[1]
    diff  = tgt_xyz.unsqueeze(2) - src_xyz.unsqueeze(1)   # (B, N, M, 3)
    dist2 = (diff ** 2).sum(-1)                            # (B, N, M)
    nn_idx = dist2.argmin(dim=-1)                          # (B, N)
    return feats.gather(2, nn_idx.unsqueeze(1).expand(B, C, N))
