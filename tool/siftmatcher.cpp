#include "siftmatcher.h"
#include <iostream>
#include <set>
#include <cmath>
#include <algorithm>

void SiftMatcher::saveDescriptors(const std::string& filename,
                                  const std::vector<cv::Mat>& descriptors,
                                  const std::vector<std::string>& board_id) {
    cv::FileStorage fs(filename, cv::FileStorage::WRITE);

    fs << "image_count" << (int)descriptors.size();

    for (size_t i = 0; i < descriptors.size(); i++) {
        fs << "image_name_" + std::to_string(i) << board_id[i];
        fs << "descriptor_" + std::to_string(i) << descriptors[i];
    }

    fs.release();
}

void SiftMatcher::loadDescriptors(const std::string& filename,
                                  std::vector<cv::Mat>& descriptors,
                                  std::vector<std::string>& board_id) {
    cv::FileStorage fs(filename, cv::FileStorage::READ);

    int image_count;
    fs["image_count"] >> image_count;

    for (int i = 0; i < image_count; i++) {
        cv::Mat descriptor;
        std::string name;

        fs["image_name_" + std::to_string(i)] >> name;
        fs["descriptor_" + std::to_string(i)] >> descriptor;

        descriptors.push_back(descriptor);
        board_id.push_back(name);
    }

    fs.release();
}

SiftMatcher::SiftMatcher() {
    if (checkGPU()) {
        try {
            gpu_detector = cv::cuda::ORB::create(
                500,    // nfeatures
                1.2f,   // scaleFactor
                8,      // nlevels
                31,     // edgeThreshold
                0,      // firstLevel
                2,      // WTA_K
                cv::ORB::HARRIS_SCORE,
                31      // patchSize
            );
            gpu_matcher = cv::cuda::DescriptorMatcher::createBFMatcher(cv::NORM_HAMMING);
        } catch (const cv::Exception& e) {
            gpu_detector.release();
            gpu_matcher.release();
        }
    } else {
        std::cout << "Warning: GPU not available, falling back to CPU implementation" << std::endl;
    }
}

SiftMatcher::~SiftMatcher() {
    gpu_detector.release();
    gpu_matcher.release();
}

bool SiftMatcher::checkGPU() {
    try {
        int deviceCount = cv::cuda::getCudaEnabledDeviceCount();
        if (deviceCount == 0) {
            std::cout << "No CUDA devices found" << std::endl;
            return false;
        }
        
        cv::cuda::DeviceInfo deviceInfo;
        if (!deviceInfo.isCompatible()) {
            std::cout << "CUDA device is not compatible" << std::endl;
            return false;
        }
        
        cv::cuda::setDevice(0);
        std::cout << "Using CUDA device: " << deviceInfo.name() << std::endl;
        return true;
    } catch (const cv::Exception& e) {
        std::cout << "CUDA initialization failed: " << e.what() << std::endl;
        return false;
    }
}

cv::Mat SiftMatcher::extractDescriptor(const cv::Mat& image, int rotation) {
    cv::Mat extracted = PCB_EXTRACT->extract(image, "PCB");
    if (extracted.empty()) {
        std::cout << "Warning: Failed to extract PCB from image, using original input" << std::endl;
        extracted = image.clone();
        if (extracted.empty()) {
            return cv::Mat();
        }
    }
    // 转换为灰度图
    cv::Mat gray;
    cv::cvtColor(extracted, gray, cv::COLOR_BGR2GRAY);
    
    // 图像预处理：增强对比度和去噪
    cv::Mat enhanced;
    cv::equalizeHist(gray, enhanced); // 直方图均衡化
    
    // 高斯滤波去噪
    cv::Mat denoised;
    cv::GaussianBlur(enhanced, denoised, cv::Size(3, 3), 0);
    
    // 应用旋转
    cv::Mat processed = denoised;
    if (rotation >= 0) {
        switch (rotation) {
            case 0: cv::rotate(processed, processed, cv::ROTATE_90_CLOCKWISE); break;
            case 1: cv::rotate(processed, processed, cv::ROTATE_180); break;
            case 2: cv::rotate(processed, processed, cv::ROTATE_90_COUNTERCLOCKWISE); break;
        }
    }

    if (gpu_detector) {
        try {
            cv::cuda::GpuMat gpu_image(processed);
            cv::cuda::GpuMat gpu_descriptors;
            std::vector<cv::KeyPoint> keypoints;

            gpu_detector->detectAndCompute(gpu_image, cv::cuda::GpuMat(), keypoints, gpu_descriptors);

            cv::Mat descriptors;
            gpu_descriptors.download(descriptors);
            
            std::cout << "GPU extracted " << keypoints.size() << " keypoints" << std::endl;
            return descriptors;
        } catch (const cv::Exception& e) {
            std::cout << "GPU feature extraction failed: " << e.what() << std::endl;
        }
    }

    // 降级到CPU实现 - 同样使用ORB保持一致性
    cv::Ptr<cv::ORB> orb = cv::ORB::create(
        500,        // nfeatures
        1.2f,       // scaleFactor
        8,          // nlevels
        31,         // edgeThreshold
        0,          // firstLevel
        2,          // WTA_K
        cv::ORB::HARRIS_SCORE,
        31,         // patchSize
        20          // fastThreshold
    );
    
    std::vector<cv::KeyPoint> keypoints;
    cv::Mat descriptors;
    orb->detectAndCompute(processed, cv::noArray(), keypoints, descriptors);
    
    std::cout << "CPU extracted " << keypoints.size() << " keypoints" << std::endl;
    return descriptors;
}

SiftMatcher::MatchResult SiftMatcher::computeMatchScore(const cv::Mat& queryDesc,
                                                        const cv::Mat& trainDesc,
                                                        const std::string& board_id) {
    std::vector<cv::DMatch> matches;
    
    try {
        if (gpu_matcher) {
            cv::cuda::GpuMat gpu_queryDesc(queryDesc);
            cv::cuda::GpuMat gpu_trainDesc(trainDesc);
            
            gpu_matcher->match(gpu_queryDesc, gpu_trainDesc, matches);
        } else {
            cv::Ptr<cv::BFMatcher> matcher = cv::BFMatcher::create(cv::NORM_HAMMING);
            matcher->match(queryDesc, trainDesc, matches);
        }
    } catch (const cv::Exception& e) {
        std::cout << "Matching failed for " << board_id << ": " << e.what() << std::endl;
        return {board_id, 0, 0};
    }

    if (matches.empty()) {
        return {board_id, 0, 0};
    }

    // 找到最小和最大距离
    double minDist = matches[0].distance;
    double maxDist = matches[0].distance;
    for (const auto& match : matches) {
        minDist = std::min(minDist, (double)match.distance);
        maxDist = std::max(maxDist, (double)match.distance);
    }

    // 使用更严格的阈值
    // 对于ORB HAMMING距离，使用更保守的阈值
    double threshold = std::max(2.5 * minDist, 35.0); // 更严格的阈值
    
    std::vector<cv::DMatch> goodMatches;
    for (const auto& match : matches) {
        if (match.distance <= threshold) {
            goodMatches.push_back(match);
        }
    }

    // 输出调试信息
    std::cout << "  Board " << board_id << ": " 
              << matches.size() << " total matches, "
              << goodMatches.size() << " good matches (threshold: " << threshold 
              << ", min_dist: " << minDist << ", max_dist: " << maxDist << ")" << std::endl;

    // 如果好匹配太少，直接返回低分
    if (goodMatches.size() < 10) {
        std::cout << "    Too few good matches, returning low score" << std::endl;
        return {board_id, (int)goodMatches.size(), 0};
    }

    // 计算匹配分数
    double matchScore = 0;
    if (!goodMatches.empty()) {
        // 计算平均匹配距离
        double avgDistance = 0;
        for (const auto& match : goodMatches) {
            avgDistance += match.distance;
        }
        avgDistance /= goodMatches.size();
        
        // 计算距离的标准差，用于评估匹配一致性
        double distanceVariance = 0;
        for (const auto& match : goodMatches) {
            double diff = match.distance - avgDistance;
            distanceVariance += diff * diff;
        }
        distanceVariance /= goodMatches.size();
        double distanceStdDev = std::sqrt(distanceVariance);
        
        // 归一化分数：距离越小，分数越高
        double qualityScore = 1.0 / (1.0 + avgDistance / 30.0); // 更严格的归一化
        
        // 一致性分数：标准差越小，匹配越一致
        double consistencyScore = 1.0 / (1.0 + distanceStdDev / 20.0);
        
        // 最终分数：结合匹配数量、质量和一致性
        matchScore = goodMatches.size() * qualityScore * consistencyScore;
        
        // 对于高质量匹配给予奖励
        if (goodMatches.size() > 30 && avgDistance < 25.0 && distanceStdDev < 15.0) {
            matchScore *= 1.8; // 高质量匹配奖励
        }
        
        // 对于大量匹配给予额外奖励
        if (goodMatches.size() > 80) {
            matchScore *= 1.3;
        }
        
        // 惩罚匹配质量差的情况
        if (avgDistance > 40.0 || distanceStdDev > 25.0) {
            matchScore *= 0.5; // 质量差的匹配惩罚
        }
        
        std::cout << "    Avg distance: " << avgDistance 
                  << ", Std dev: " << distanceStdDev
                  << ", Quality score: " << qualityScore 
                  << ", Consistency: " << consistencyScore
                  << ", Final score: " << matchScore << std::endl;
    }

    return {board_id, (int)goodMatches.size(), matchScore};
}

std::vector<SiftMatcher::MatchResult> SiftMatcher::matchImage(const cv::Mat& inputImage) {
    std::vector<cv::Mat> loadedDescriptors;
    std::vector<std::string> loadedNames;
    loadDescriptors("descriptors_database.yml", loadedDescriptors, loadedNames);

    cv::Mat queryDesc = extractDescriptor(inputImage);
    if (queryDesc.empty()) {
        std::cout << "Warning: Failed to extract descriptors from input image" << std::endl;
        return {};
    }
    
    std::cout << "Query image descriptors: " << queryDesc.rows << " features" << std::endl;
    std::cout << "Database contains: " << loadedDescriptors.size() << " descriptor sets" << std::endl;

    std::vector<MatchResult> matchResults;
    for (size_t i = 0; i < loadedDescriptors.size(); i++) {
        MatchResult result = computeMatchScore(queryDesc, loadedDescriptors[i], loadedNames[i]);
        matchResults.push_back(result);
        
        // 输出每个匹配的详细信息
        if (result.goodMatchCount > 0) {
            std::cout << "Match with " << result.boardId 
                      << ": " << result.goodMatchCount << " good matches, score: " 
                      << result.matchScore << std::endl;
        }
    }

    // 按匹配分数降序排序
    std::sort(matchResults.begin(), matchResults.end(), [](const MatchResult& a, const MatchResult& b) {
        return a.matchScore > b.matchScore;
    });
    
    // 改进的去重逻辑：为每个board_id选择最佳匹配结果
    std::vector<MatchResult> results;
    std::set<std::string> processedBoardIds;
    
    for(const auto& result : matchResults) {
        // 如果这个board_id还没有被处理过，添加到结果中
        if (processedBoardIds.find(result.boardId) == processedBoardIds.end()) {
            results.push_back(result);
            processedBoardIds.insert(result.boardId);
            std::cout << "Best match for " << result.boardId 
                      << ": score " << result.matchScore 
                      << " (" << result.goodMatchCount << " matches)" << std::endl;
        }
    }
    return results;
}

void SiftMatcher::appendToDatabase(const std::vector<std::string>& boardid, const std::vector<cv::Mat>& images) {
    // 首先加载现有数据库
    std::vector<cv::Mat> descriptors;
    std::vector<std::string> boardids;
    
    if(boardid.size() != images.size()) return;
    try {
        loadDescriptors("descriptors_database.yml", descriptors, boardids);
    } catch (const cv::Exception& e) {
        std::cout << "Creating new database" << std::endl;
    }

    // 添加新图片的描述符
    for(int i = 0; i < boardid.size(); ++i)
    {
        for (int rotation = -1; rotation < 3; rotation++) {
            cv::Mat desc = extractDescriptor(images[i], rotation);
            if (!desc.empty()) {
                descriptors.push_back(desc);

                std::string board_id = boardid[i];
                boardids.push_back(board_id);

                std::string rotationStr;
                switch(rotation) {
                case -1: rotationStr = "Original"; break;
                case 0: rotationStr = "90°"; break;
                case 1: rotationStr = "180°"; break;
                case 2: rotationStr = "270°"; break;
                }
            }
        }
    }

    saveDescriptors("descriptors_database.yml", descriptors, boardids);
}

bool SiftMatcher::removeFromDatabase(const std::string& board_id) {
    std::vector<cv::Mat> descriptors;
    std::vector<std::string> imageNames;
    bool found = false;

    try {
        // 加载现有数据库
        loadDescriptors("descriptors_database.yml", descriptors, imageNames);

        // 创建新的向量来存储保留的数据
        std::vector<cv::Mat> newDescriptors;
        std::vector<std::string> newImageNames;

        // 遍历所有条目，跳过要删除的图片
        for (size_t i = 0; i < imageNames.size(); i++) {
            if (imageNames[i] != board_id) {
                newDescriptors.push_back(descriptors[i]);
                newImageNames.push_back(imageNames[i]);
            } else {
                found = true;
            }
        }

        if (found) {
            saveDescriptors("descriptors_database.yml", newDescriptors, newImageNames);
        }
        return found;

    } catch (const cv::Exception& e) {
        std::cout << "Error while removing image from database: " << e.what() << std::endl;
        return false;
    }
}
