# Board Embedding Training

这个目录用于板卡 ROI-Embedding 模型训练，不参与主程序底层设备逻辑。

## 输入

- 板卡数据库：`board_db/boards.json`
- 板卡图像：`board_db/images`

训练脚本会根据 `boards.json` 中的 `imagePath` 自动读取图像，并先做 ROI 提取。

## 输出

- ONNX 模型：`model/board_embedding.onnx`
- 向量库：`board_db/embeddings_database.yml`

## 运行方式

主程序中的“训练识别模型”按钮会在后台启动：

```powershell
python BoardManager/EmbeddingTraining/train_board_embedding.py --boards-json ... --output-model ... --output-db ...
```

如果需要指定 Python 解释器，可设置环境变量：

```powershell
$env:ATS_PYTHON="D:\ProgramData\anaconda3\python.exe"
```

## Python 依赖

- `torch`
- `torchvision`
- `opencv-python`
- `numpy`
- `onnx`

