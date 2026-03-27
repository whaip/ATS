#ifndef SiftMatcher_H
#define SiftMatcher_H

#include <opencv2/opencv.hpp>
#include <opencv2/core/core.hpp>
#include <opencv2/imgproc/imgproc.hpp>
#include <opencv2/features2d.hpp>
#include <opencv2/cudafeatures2d.hpp>
#include <vector>
#include <string>
#include "pcb_extract.h"

#define SIFT_MATCHER (SiftMatcher::getInstance())

class SiftMatcher {
public:
    // 匹配结果结构体
    struct MatchResult {
        std::string boardId;
        int goodMatchCount;
        double matchScore;

        bool operator < (const MatchResult& other) const {
            return matchScore > other.matchScore;
        }
    };

    // 构造函数和析构函数
    static SiftMatcher* getInstance() {
        static SiftMatcher instance;
        return &instance;
    }

    SiftMatcher(const SiftMatcher&) = delete;
    SiftMatcher& operator=(const SiftMatcher&) = delete;
    // 保存描述符到文件
    static void saveDescriptors(const std::string& filename,
                                const std::vector<cv::Mat>& descriptors,
                                const std::vector<std::string>& board_id);

    // 从文件加载描述符
    static void loadDescriptors(const std::string& filename,
                                std::vector<cv::Mat>& descriptors,
                                std::vector<std::string>& board_id);

    // 提取特征描述符
    cv::Mat extractDescriptor(const cv::Mat& image,
                              int rotation = -1,
                              bool imageIsPcbCrop = false);

    std::vector<MatchResult> matchImage(const cv::Mat& inputImage,
                                        bool imageIsPcbCrop = false);

    void setDatabasePath(const std::string& filename);

    // 检查GPU是否可用
    bool checkGPU();

    // 添加新的函数来追加图片到数据库
    void appendToDatabase(const std::vector<std::string>& boardid,
                          const std::vector<cv::Mat>& images,
                          bool imagesArePcbCrops = false);

    // 从数据库中删除指定图片的描述符
    bool removeFromDatabase(const std::string& DeleteImage);

private:
    explicit SiftMatcher();
    ~SiftMatcher();
    cv::Ptr<cv::cuda::ORB> gpu_detector;
    cv::Ptr<cv::cuda::DescriptorMatcher> gpu_matcher;
    std::string m_databasePath = "descriptors_database.yml";
    
    // 计算两个描述符之间的匹配分数
    MatchResult computeMatchScore(const cv::Mat& queryDesc,
                                const cv::Mat& trainDesc,
                                const std::string& trainImagePath);
};

#endif // SiftMatcher_H
