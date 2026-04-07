#ifndef SIFTMATCHER_H
#define SIFTMATCHER_H

#include <opencv2/core/core.hpp>
#include <opencv2/cudafeatures2d.hpp>
#include <opencv2/features2d.hpp>
#include <opencv2/imgproc/imgproc.hpp>

#include <string>
#include <vector>

#include "pcb_extract.h"

#define SIFT_MATCHER (SiftMatcher::getInstance())

class SiftMatcher {
public:
    struct MatchResult {
        std::string boardId;
        int goodMatchCount = 0;
        int inlierCount = 0;
        double inlierRatio = 0.0;
        double matchScore = 0.0;
        bool rejected = true;

        bool operator<(const MatchResult &other) const
        {
            if (inlierCount != other.inlierCount) {
                return inlierCount > other.inlierCount;
            }
            if (inlierRatio != other.inlierRatio) {
                return inlierRatio > other.inlierRatio;
            }
            return matchScore > other.matchScore;
        }
    };

    static SiftMatcher *getInstance()
    {
        static SiftMatcher instance;
        return &instance;
    }

    SiftMatcher(const SiftMatcher &) = delete;
    SiftMatcher &operator=(const SiftMatcher &) = delete;

    static void saveDescriptors(const std::string &filename,
                                const std::vector<cv::Mat> &descriptors,
                                const std::vector<cv::Mat> &keypointsXY,
                                const std::vector<std::string> &boardIds);

    static void loadDescriptors(const std::string &filename,
                                std::vector<cv::Mat> &descriptors,
                                std::vector<cv::Mat> &keypointsXY,
                                std::vector<std::string> &boardIds);

    cv::Mat extractDescriptor(const cv::Mat &image,
                              int rotation = -1,
                              bool imageIsPcbCrop = false);

    std::vector<MatchResult> matchImage(const cv::Mat &inputImage,
                                        bool imageIsPcbCrop = false);

    void setDatabasePath(const std::string &filename);
    bool checkGPU();

    void appendToDatabase(const std::vector<std::string> &boardIds,
                          const std::vector<cv::Mat> &images,
                          bool imagesArePcbCrops = false);

    bool removeFromDatabase(const std::string &boardId);

private:
    struct FeatureData {
        cv::Mat descriptors;
        cv::Mat keypointsXY;
    };

    explicit SiftMatcher();
    ~SiftMatcher();

    FeatureData extractFeatures(const cv::Mat &image,
                                int rotation = -1,
                                bool imageIsPcbCrop = false);

    MatchResult computeMatchScore(const FeatureData &query,
                                  const FeatureData &train,
                                  const std::string &boardId);

    cv::Ptr<cv::cuda::ORB> gpu_detector;
    std::string m_databasePath = "descriptors_database.yml";
};

#endif // SIFTMATCHER_H
