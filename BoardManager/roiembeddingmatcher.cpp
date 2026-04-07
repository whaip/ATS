#include "roiembeddingmatcher.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStringList>

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <numeric>

namespace {
QString resolveModelPath(const QString &relativePath)
{
    const QString appDir = QCoreApplication::instance()
        ? QCoreApplication::applicationDirPath()
        : QDir::currentPath();

    QStringList candidates;
    QDir probeDir(appDir);
    for (int depth = 0; depth < 8; ++depth) {
        candidates << probeDir.filePath(relativePath);
        if (!probeDir.cdUp()) {
            break;
        }
    }

    for (const QString &candidate : candidates) {
        if (QFileInfo::exists(candidate)) {
            return QDir::cleanPath(candidate);
        }
    }
    return QDir(appDir).filePath(relativePath);
}

cv::Mat l2Normalize(const cv::Mat &vector)
{
    if (vector.empty()) {
        return {};
    }

    cv::Mat flattened = vector.reshape(1, 1).clone();
    flattened.convertTo(flattened, CV_32F);
    const double norm = cv::norm(flattened, cv::NORM_L2);
    if (norm <= 1e-12) {
        return {};
    }
    flattened /= norm;
    return flattened;
}
}

RoiEmbeddingMatcher::RoiEmbeddingMatcher()
    : m_env(std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "board_embedding_matcher"))
    , m_sessionOptions()
    , m_session()
    , m_memoryInfo(Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeCPU))
    , m_modelPath(defaultModelPath())
{
    m_sessionOptions.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_EXTENDED);
    OrtStatus *status = OrtSessionOptionsAppendExecutionProvider_CUDA(m_sessionOptions, 0);
    if (status != nullptr) {
        Ort::GetApi().ReleaseStatus(status);
    }
}

QString RoiEmbeddingMatcher::defaultModelPath() const
{
    return resolveModelPath(QStringLiteral("model/board_embedding.onnx"));
}

void RoiEmbeddingMatcher::setDatabasePath(const std::string &filename)
{
    if (!filename.empty()) {
        m_databasePath = filename;
    }
}

void RoiEmbeddingMatcher::setModelPath(const QString &modelPath)
{
    if (modelPath.trimmed().isEmpty()) {
        return;
    }
    m_modelPath = modelPath;
    m_session.reset();
    m_inputName.clear();
    m_outputName.clear();
}

void RoiEmbeddingMatcher::ensureSession()
{
    if (m_session || m_modelPath.trimmed().isEmpty() || !QFileInfo::exists(m_modelPath)) {
        return;
    }

    m_session = std::make_unique<Ort::Session>(*m_env, m_modelPath.toStdWString().c_str(), m_sessionOptions);
    Ort::AllocatorWithDefaultOptions allocator;
    Ort::AllocatedStringPtr inputName = m_session->GetInputNameAllocated(0, allocator);
    Ort::AllocatedStringPtr outputName = m_session->GetOutputNameAllocated(0, allocator);
    if (inputName) {
        m_inputName = inputName.get();
    }
    if (outputName) {
        m_outputName = outputName.get();
    }

    const auto inputShape = m_session->GetInputTypeInfo(0).GetTensorTypeAndShapeInfo().GetShape();
    if (inputShape.size() >= 4) {
        if (inputShape[2] > 0) {
            m_inputHeight = inputShape[2];
        }
        if (inputShape[3] > 0) {
            m_inputWidth = inputShape[3];
        }
    }
}

bool RoiEmbeddingMatcher::sessionReady() const
{
    return m_session != nullptr && !m_inputName.empty() && !m_outputName.empty();
}

cv::Mat RoiEmbeddingMatcher::prepareRoi(const cv::Mat &image, bool imageIsPcbCrop) const
{
    if (image.empty()) {
        return {};
    }
    if (imageIsPcbCrop) {
        return image.clone();
    }

    cv::Mat roi = PCB_EXTRACT->extract(image, "PCB");
    if (!roi.empty()) {
        return roi;
    }
    return image.clone();
}

cv::Mat RoiEmbeddingMatcher::rotateImage(const cv::Mat &image, int rotation) const
{
    cv::Mat rotated = image.clone();
    switch (rotation) {
    case 90:
        cv::rotate(rotated, rotated, cv::ROTATE_90_CLOCKWISE);
        break;
    case 180:
        cv::rotate(rotated, rotated, cv::ROTATE_180);
        break;
    case 270:
        cv::rotate(rotated, rotated, cv::ROTATE_90_COUNTERCLOCKWISE);
        break;
    default:
        break;
    }
    return rotated;
}

cv::Mat RoiEmbeddingMatcher::extractEmbedding(const cv::Mat &imageBgr)
{
    ensureSession();
    if (!sessionReady() || imageBgr.empty()) {
        return {};
    }

    cv::Mat resized;
    cv::resize(imageBgr, resized, cv::Size(static_cast<int>(m_inputWidth), static_cast<int>(m_inputHeight)));
    cv::Mat rgb;
    cv::cvtColor(resized, rgb, cv::COLOR_BGR2RGB);
    rgb.convertTo(rgb, CV_32F, 1.0 / 255.0);

    std::vector<cv::Mat> channels;
    cv::split(rgb, channels);
    static constexpr float kMean[3] = {0.485f, 0.456f, 0.406f};
    static constexpr float kStd[3] = {0.229f, 0.224f, 0.225f};

    std::vector<float> inputTensorValues;
    inputTensorValues.reserve(static_cast<size_t>(m_inputWidth * m_inputHeight * 3));
    for (int c = 0; c < 3; ++c) {
        channels[c] = (channels[c] - kMean[c]) / kStd[c];
        const float *begin = channels[c].ptr<float>(0);
        const float *end = begin + static_cast<ptrdiff_t>(channels[c].total());
        inputTensorValues.insert(inputTensorValues.end(), begin, end);
    }

    const std::array<int64_t, 4> inputShape = {1, 3, m_inputHeight, m_inputWidth};
    Ort::Value inputTensor = Ort::Value::CreateTensor<float>(m_memoryInfo,
                                                             inputTensorValues.data(),
                                                             inputTensorValues.size(),
                                                             inputShape.data(),
                                                             inputShape.size());

    const char *inputNames[] = {m_inputName.c_str()};
    const char *outputNames[] = {m_outputName.c_str()};
    std::vector<Ort::Value> outputs = m_session->Run(Ort::RunOptions{nullptr},
                                                     inputNames,
                                                     &inputTensor,
                                                     1,
                                                     outputNames,
                                                     1);
    if (outputs.empty()) {
        return {};
    }

    Ort::Value &output = outputs.front();
    const auto tensorInfo = output.GetTensorTypeAndShapeInfo();
    const std::vector<int64_t> shape = tensorInfo.GetShape();
    size_t elementCount = 1;
    for (const int64_t dim : shape) {
        if (dim > 0) {
            elementCount *= static_cast<size_t>(dim);
        }
    }
    if (elementCount == 0) {
        return {};
    }

    float *data = output.GetTensorMutableData<float>();
    cv::Mat embedding(1, static_cast<int>(elementCount), CV_32F, data);
    return l2Normalize(embedding);
}

void RoiEmbeddingMatcher::saveDatabase(const std::string &filename,
                                       const std::vector<EmbeddingRecord> &records)
{
    cv::FileStorage fs(filename, cv::FileStorage::WRITE);
    fs << "entry_count" << static_cast<int>(records.size());
    for (size_t i = 0; i < records.size(); ++i) {
        fs << "board_id_" + std::to_string(i) << records[i].boardId;
        fs << "rotation_" + std::to_string(i) << records[i].rotation;
        fs << "embedding_" + std::to_string(i) << records[i].embedding;
    }
    fs.release();
}

std::vector<RoiEmbeddingMatcher::EmbeddingRecord> RoiEmbeddingMatcher::loadDatabase(const std::string &filename)
{
    std::vector<EmbeddingRecord> records;
    cv::FileStorage fs(filename, cv::FileStorage::READ);
    if (!fs.isOpened()) {
        return records;
    }

    int count = 0;
    fs["entry_count"] >> count;
    records.reserve(std::max(0, count));
    for (int i = 0; i < count; ++i) {
        EmbeddingRecord record;
        fs["board_id_" + std::to_string(i)] >> record.boardId;
        fs["rotation_" + std::to_string(i)] >> record.rotation;
        fs["embedding_" + std::to_string(i)] >> record.embedding;
        record.embedding = l2Normalize(record.embedding);
        if (!record.boardId.empty() && !record.embedding.empty()) {
            records.push_back(record);
        }
    }
    fs.release();
    return records;
}

double RoiEmbeddingMatcher::cosineSimilarity(const cv::Mat &lhs, const cv::Mat &rhs)
{
    if (lhs.empty() || rhs.empty() || lhs.cols != rhs.cols) {
        return -1.0;
    }
    return lhs.dot(rhs);
}

std::vector<RoiEmbeddingMatcher::MatchResult> RoiEmbeddingMatcher::matchImage(const cv::Mat &inputImage,
                                                                              bool imageIsPcbCrop)
{
    cv::Mat roi = prepareRoi(inputImage, imageIsPcbCrop);
    cv::Mat queryEmbedding = extractEmbedding(roi);
    if (queryEmbedding.empty()) {
        return {};
    }

    const std::vector<EmbeddingRecord> records = loadDatabase(m_databasePath);
    std::map<std::string, MatchResult> bestByBoard;
    for (const auto &record : records) {
        const double similarity = cosineSimilarity(queryEmbedding, record.embedding);
        auto &slot = bestByBoard[record.boardId];
        if (slot.boardId.empty()) {
            slot.boardId = record.boardId;
        }
        slot.templateCount += 1;
        if (similarity > slot.matchScore) {
            slot.matchScore = similarity;
            slot.bestRotation = record.rotation;
        }
    }

    std::vector<MatchResult> results;
    results.reserve(bestByBoard.size());
    for (const auto &entry : bestByBoard) {
        MatchResult result = entry.second;
        result.matchScore = std::clamp((result.matchScore + 1.0) * 50.0, 0.0, 100.0);
        results.push_back(result);
    }
    std::sort(results.begin(), results.end());
    return results;
}

void RoiEmbeddingMatcher::appendToDatabase(const std::vector<std::string> &boardIds,
                                           const std::vector<cv::Mat> &images,
                                           bool imagesArePcbCrops)
{
    if (boardIds.size() != images.size()) {
        return;
    }

    std::vector<EmbeddingRecord> records = loadDatabase(m_databasePath);
    static const int rotations[] = {0, 90, 180, 270};

    for (size_t i = 0; i < boardIds.size(); ++i) {
        cv::Mat roi = prepareRoi(images[i], imagesArePcbCrops);
        if (roi.empty()) {
            continue;
        }

        for (const int rotation : rotations) {
            cv::Mat rotated = rotateImage(roi, rotation);
            cv::Mat embedding = extractEmbedding(rotated);
            if (embedding.empty()) {
                continue;
            }
            records.push_back({boardIds[i], rotation, embedding});
        }
    }

    saveDatabase(m_databasePath, records);
}

bool RoiEmbeddingMatcher::removeFromDatabase(const std::string &boardId)
{
    std::vector<EmbeddingRecord> records = loadDatabase(m_databasePath);
    const auto it = std::remove_if(records.begin(), records.end(),
                                   [&boardId](const EmbeddingRecord &record) {
                                       return record.boardId == boardId;
                                   });
    const bool removed = (it != records.end());
    if (removed) {
        records.erase(it, records.end());
        saveDatabase(m_databasePath, records);
    }
    return removed;
}
