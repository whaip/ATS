import argparse
import json
import math
import os
import random
from pathlib import Path

import cv2
import numpy as np
import torch
import torch.nn as nn
import torch.nn.functional as F
from torch.utils.data import DataLoader, Dataset
from torchvision import models


ROTATIONS = (0, 90, 180, 270)
IMAGENET_MEAN = torch.tensor([0.485, 0.456, 0.406], dtype=torch.float32).view(3, 1, 1)
IMAGENET_STD = torch.tensor([0.229, 0.224, 0.225], dtype=torch.float32).view(3, 1, 1)


def parse_args():
    p = argparse.ArgumentParser(description="Train ROI embedding model for board retrieval")
    p.add_argument("--boards-json", required=True, type=str)
    p.add_argument("--output-model", required=True, type=str)
    p.add_argument("--output-db", required=True, type=str)
    p.add_argument("--epochs", type=int, default=12)
    p.add_argument("--batch-size", type=int, default=16)
    p.add_argument("--embedding-dim", type=int, default=256)
    p.add_argument("--lr", type=float, default=1e-4)
    p.add_argument("--seed", type=int, default=42)
    return p.parse_args()


def seed_everything(seed: int):
    random.seed(seed)
    np.random.seed(seed)
    torch.manual_seed(seed)
    torch.cuda.manual_seed_all(seed)


def create_green_mask(bgr: np.ndarray) -> np.ndarray:
    hsv = cv2.cvtColor(bgr, cv2.COLOR_BGR2HSV)
    mask_hsv = cv2.inRange(hsv, (30, 25, 30), (90, 255, 255))

    b, g, r = cv2.split(bgr)
    gdom1 = (g > (b + 10)).astype(np.uint8) * 255
    gdom2 = (g > (r + 10)).astype(np.uint8) * 255
    gdom3 = (g > 60).astype(np.uint8) * 255
    mask_gdom = cv2.bitwise_and(cv2.bitwise_and(gdom1, gdom2), gdom3)

    mask = cv2.bitwise_or(mask_hsv, mask_gdom)
    k = max(5, min(151, min(bgr.shape[0], bgr.shape[1]) // 50))
    if k % 2 == 0:
        k += 1
    kernel = cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (k, k))
    mask = cv2.morphologyEx(mask, cv2.MORPH_CLOSE, kernel, iterations=1)
    mask = cv2.dilate(mask, kernel, iterations=1)
    return mask


def order_points_clockwise(pts: np.ndarray) -> np.ndarray:
    s = pts.sum(axis=1)
    d = pts[:, 0] - pts[:, 1]
    rect = np.zeros((4, 2), dtype=np.float32)
    rect[0] = pts[np.argmin(s)]
    rect[2] = pts[np.argmax(s)]
    rect[1] = pts[np.argmin(d)]
    rect[3] = pts[np.argmax(d)]
    return rect


def auto_extract_roi(bgr: np.ndarray) -> np.ndarray | None:
    mask = create_green_mask(bgr)
    contours, _ = cv2.findContours(mask, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
    if not contours:
        return None
    largest = max(contours, key=cv2.contourArea)
    if cv2.contourArea(largest) < 0.05 * bgr.shape[0] * bgr.shape[1]:
        return None

    rect = cv2.minAreaRect(largest)
    box = cv2.boxPoints(rect).astype(np.float32)
    ordered = order_points_clockwise(box)

    tl, tr, br, bl = ordered
    width_a = np.linalg.norm(br - bl)
    width_b = np.linalg.norm(tr - tl)
    height_a = np.linalg.norm(tr - br)
    height_b = np.linalg.norm(tl - bl)
    max_w = max(1, int(round(max(width_a, width_b))))
    max_h = max(1, int(round(max(height_a, height_b))))

    dst = np.array(
        [[0, 0], [max_w - 1, 0], [max_w - 1, max_h - 1], [0, max_h - 1]],
        dtype=np.float32,
    )
    H = cv2.getPerspectiveTransform(ordered, dst)
    warped = cv2.warpPerspective(bgr, H, (max_w, max_h))
    return warped if warped.size > 0 else None


def load_board_records(boards_json: Path):
    root = json.loads(boards_json.read_text(encoding="utf-8"))
    boards = []
    for obj in root.get("boards", []):
        board_id = obj.get("boardId", "").strip()
        image_path = obj.get("imagePath", "").strip()
        if not board_id or not image_path:
            continue
        img_path = Path(image_path)
        if not img_path.is_absolute():
            img_path = boards_json.parent / img_path
        boards.append({"boardId": board_id, "imagePath": img_path})
    return boards


def preprocess_for_model(bgr: np.ndarray, size: int = 224) -> torch.Tensor:
    resized = cv2.resize(bgr, (size, size), interpolation=cv2.INTER_LINEAR)
    rgb = cv2.cvtColor(resized, cv2.COLOR_BGR2RGB)
    tensor = torch.from_numpy(rgb.transpose(2, 0, 1)).float() / 255.0
    tensor = (tensor - IMAGENET_MEAN) / IMAGENET_STD
    return tensor


def augment_bgr(bgr: np.ndarray) -> np.ndarray:
    out = bgr.copy()
    alpha = random.uniform(0.9, 1.1)
    beta = random.uniform(-12.0, 12.0)
    out = cv2.convertScaleAbs(out, alpha=alpha, beta=beta)
    if random.random() < 0.25:
        out = cv2.GaussianBlur(out, (3, 3), 0)
    return out


class BoardRoiDataset(Dataset):
    def __init__(self, boards_json: Path, image_size: int = 224, repeats: int = 24):
        self.image_size = image_size
        self.class_to_index = {}
        self.samples = []
        boards = load_board_records(boards_json)
        for board in boards:
            image = cv2.imdecode(np.fromfile(str(board["imagePath"]), dtype=np.uint8), cv2.IMREAD_COLOR)
            if image is None:
                continue
            roi = auto_extract_roi(image)
            if roi is None:
                continue
            label = board["boardId"]
            class_index = self.class_to_index.setdefault(label, len(self.class_to_index))
            self.samples.append({"boardId": label, "classIndex": class_index, "roi": roi})
        self.repeats = max(1, repeats)

    def __len__(self):
        return len(self.samples) * self.repeats

    def __getitem__(self, index):
        sample = self.samples[index % len(self.samples)]
        roi = sample["roi"]
        rotation = random.choice(ROTATIONS)
        if rotation == 90:
            roi = cv2.rotate(roi, cv2.ROTATE_90_CLOCKWISE)
        elif rotation == 180:
            roi = cv2.rotate(roi, cv2.ROTATE_180)
        elif rotation == 270:
            roi = cv2.rotate(roi, cv2.ROTATE_90_COUNTERCLOCKWISE)
        roi = augment_bgr(roi)
        return preprocess_for_model(roi, self.image_size), sample["classIndex"]


class BoardEmbeddingNet(nn.Module):
    def __init__(self, class_count: int, embedding_dim: int):
        super().__init__()
        backbone = models.mobilenet_v3_large(weights=models.MobileNet_V3_Large_Weights.IMAGENET1K_V1)
        self.features = backbone.features
        self.pool = nn.AdaptiveAvgPool2d(1)
        in_features = backbone.classifier[0].in_features
        self.embedding = nn.Linear(in_features, embedding_dim)
        self.classifier = nn.Linear(embedding_dim, class_count)

    def forward(self, x):
        x = self.features(x)
        x = self.pool(x).flatten(1)
        emb = self.embedding(x)
        emb = F.normalize(emb, p=2, dim=1)
        logits = self.classifier(emb)
        return logits, emb


class BoardEmbeddingExport(nn.Module):
    def __init__(self, trained_model: BoardEmbeddingNet):
        super().__init__()
        self.features = trained_model.features
        self.pool = trained_model.pool
        self.embedding = trained_model.embedding

    def forward(self, x):
        x = self.features(x)
        x = self.pool(x).flatten(1)
        x = self.embedding(x)
        return F.normalize(x, p=2, dim=1)


def train_model(dataset: BoardRoiDataset, epochs: int, batch_size: int, embedding_dim: int, lr: float):
    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    print(f"[train] device={device}", flush=True)
    model = BoardEmbeddingNet(len(dataset.class_to_index), embedding_dim).to(device)
    loader = DataLoader(dataset, batch_size=batch_size, shuffle=True, num_workers=0)
    optimizer = torch.optim.AdamW(model.parameters(), lr=lr, weight_decay=1e-4)
    criterion = nn.CrossEntropyLoss()

    model.train()
    for epoch in range(epochs):
        running_loss = 0.0
        correct = 0
        total = 0
        for images, labels in loader:
            images = images.to(device)
            labels = labels.to(device)

            optimizer.zero_grad(set_to_none=True)
            logits, _ = model(images)
            loss = criterion(logits, labels)
            loss.backward()
            optimizer.step()

            running_loss += loss.item() * images.size(0)
            correct += (logits.argmax(dim=1) == labels).sum().item()
            total += images.size(0)

        avg_loss = running_loss / max(total, 1)
        acc = correct / max(total, 1)
        print(f"[train] epoch={epoch + 1}/{epochs} loss={avg_loss:.4f} acc={acc:.4f}", flush=True)
    return model.cpu(), dataset


@torch.no_grad()
def export_onnx(model: BoardEmbeddingNet, output_path: Path, image_size: int = 224):
    export_model = BoardEmbeddingExport(model).eval()
    dummy = torch.randn(1, 3, image_size, image_size, dtype=torch.float32)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    try:
        torch.onnx.export(
            export_model,
            dummy,
            str(output_path),
            input_names=["input"],
            output_names=["embedding"],
            opset_version=17,
            do_constant_folding=True,
            dynamo=False,
        )
    except TypeError:
        torch.onnx.export(
            export_model,
            dummy,
            str(output_path),
            input_names=["input"],
            output_names=["embedding"],
            opset_version=17,
            do_constant_folding=True,
        )


@torch.no_grad()
def rebuild_embedding_db(model: BoardEmbeddingNet, boards_json: Path, output_db: Path, image_size: int = 224):
    export_model = BoardEmbeddingExport(model).eval()
    records = []
    for board in load_board_records(boards_json):
        image = cv2.imdecode(np.fromfile(str(board["imagePath"]), dtype=np.uint8), cv2.IMREAD_COLOR)
        if image is None:
            continue
        roi = auto_extract_roi(image)
        if roi is None:
            continue

        for rotation in ROTATIONS:
            rotated = roi
            if rotation == 90:
                rotated = cv2.rotate(roi, cv2.ROTATE_90_CLOCKWISE)
            elif rotation == 180:
                rotated = cv2.rotate(roi, cv2.ROTATE_180)
            elif rotation == 270:
                rotated = cv2.rotate(roi, cv2.ROTATE_90_COUNTERCLOCKWISE)
            tensor = preprocess_for_model(rotated, image_size).unsqueeze(0)
            embedding = export_model(tensor).squeeze(0).cpu().numpy().astype(np.float32)
            records.append((board["boardId"], rotation, embedding.reshape(1, -1)))

    output_db.parent.mkdir(parents=True, exist_ok=True)
    fs = cv2.FileStorage(str(output_db), cv2.FILE_STORAGE_WRITE)
    fs.write("entry_count", len(records))
    for idx, (board_id, rotation, embedding) in enumerate(records):
        fs.write(f"board_id_{idx}", board_id)
        fs.write(f"rotation_{idx}", int(rotation))
        fs.write(f"embedding_{idx}", embedding)
    fs.release()


def main():
    args = parse_args()
    seed_everything(args.seed)

    boards_json = Path(args.boards_json)
    output_model = Path(args.output_model)
    output_db = Path(args.output_db)

    dataset = BoardRoiDataset(boards_json, repeats=24)
    if not dataset.samples:
        raise RuntimeError("No valid ROI samples found in boards.json/images")

    print(f"[dataset] classes={len(dataset.class_to_index)} samples={len(dataset.samples)}", flush=True)
    model, dataset = train_model(dataset, args.epochs, args.batch_size, args.embedding_dim, args.lr)
    export_onnx(model, output_model, dataset.image_size)
    rebuild_embedding_db(model, boards_json, output_db, dataset.image_size)
    print(f"[done] onnx={output_model}", flush=True)
    print(f"[done] embeddings_db={output_db}", flush=True)


if __name__ == "__main__":
    main()
