#ifndef ROIEMBEDDINGMATCHER_H
#define ROIEMBEDDINGMATCHER_H

#include <memory>
#include <string>
#include <vector>

#include <QString>
#include <onnxruntime/onnxruntime_cxx_api.h>
#include <opencv2/opencv.hpp>

#include "../tool/pcb_extract.h"

#define ROI_EMBEDDING_MATCHER (RoiEmbeddingMatcher::getInstance())

class RoiEmbeddingMatcher
{
public:
    struct MatchResult {
        std::string boardId;
        int templateCount = 0;
        int bestRotation = 0;
        double matchScore = 0.0;

        bool operator<(const MatchResult &other) const
        {
            return matchScore > other.matchScore;
        }
    };

    static RoiEmbeddingMatcher *getInstance()
    {
        static RoiEmbeddingMatcher instance;
        return &instance;
    }

    RoiEmbeddingMatcher(const RoiEmbeddingMatcher &) = delete;
    RoiEmbeddingMatcher &operator=(const RoiEmbeddingMatcher &) = delete;

    void setDatabasePath(const std::string &filename);
    void setModelPath(const QString &modelPath);

    std::vector<MatchResult> matchImage(const cv::Mat &inputImage,
                                        bool imageIsPcbCrop = false);
    void appendToDatabase(const std::vector<std::string> &boardIds,
                          const std::vector<cv::Mat> &images,
                          bool imagesArePcbCrops = false);
    bool removeFromDatabase(const std::string &boardId);

private:
    struct EmbeddingRecord {
        std::string boardId;
        int rotation = 0;
        cv::Mat embedding;
    };

    RoiEmbeddingMatcher();
    ~RoiEmbeddingMatcher() = default;

    QString defaultModelPath() const;
    void ensureSession();
    bool sessionReady() const;

    cv::Mat prepareRoi(const cv::Mat &image, bool imageIsPcbCrop) const;
    cv::Mat rotateImage(const cv::Mat &image, int rotation) const;
    cv::Mat extractEmbedding(const cv::Mat &imageBgr);

    static void saveDatabase(const std::string &filename,
                             const std::vector<EmbeddingRecord> &records);
    static std::vector<EmbeddingRecord> loadDatabase(const std::string &filename);
    static double cosineSimilarity(const cv::Mat &lhs, const cv::Mat &rhs);

    std::unique_ptr<Ort::Env> m_env;
    Ort::SessionOptions m_sessionOptions;
    std::unique_ptr<Ort::Session> m_session;
    Ort::MemoryInfo m_memoryInfo;

    std::string m_databasePath = "embeddings_database.yml";
    QString m_modelPath;
    std::string m_inputName;
    std::string m_outputName;
    int64_t m_inputWidth = 224;
    int64_t m_inputHeight = 224;
};

#endif // ROIEMBEDDINGMATCHER_H
