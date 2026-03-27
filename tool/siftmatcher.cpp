#include "siftmatcher.h"
#include <iostream>
#include <set>
#include <cmath>
#include <algorithm>
#include <unordered_map>

void SiftMatcher::saveDescriptors(const std::string& filename,
                                  const std::vector<cv::Mat>& descriptors,
                                  const std::vector<std::string>& board_id) {
    cv::FileStorage fs(filename, cv::FileStorage::WRITE);

    fs << "image_count" << static_cast<int>(descriptors.size());

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

    int image_count = 0;
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
                1800,
                1.2f,
                8,
                31,
                0,
                2,
                cv::ORB::HARRIS_SCORE,
                31
            );
            gpu_matcher = cv::cuda::DescriptorMatcher::createBFMatcher(cv::NORM_HAMMING);
        } catch (const cv::Exception&) {
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

void SiftMatcher::setDatabasePath(const std::string& filename) {
    if (!filename.empty()) {
        m_databasePath = filename;
    }
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

cv::Mat SiftMatcher::extractDescriptor(const cv::Mat& image, int rotation, bool imageIsPcbCrop) {
    cv::Mat extracted;
    if (imageIsPcbCrop) {
        extracted = image.clone();
    } else {
        extracted = PCB_EXTRACT->extract(image, "PCB");
        if (extracted.empty()) {
            std::cout << "Warning: Failed to extract PCB from image, using original input" << std::endl;
            extracted = image.clone();
        }
    }
    if (extracted.empty()) {
        return cv::Mat();
    }

    cv::Mat gray;
    cv::cvtColor(extracted, gray, cv::COLOR_BGR2GRAY);

    cv::Mat enhanced;
    cv::equalizeHist(gray, enhanced);

    cv::Mat denoised;
    cv::GaussianBlur(enhanced, denoised, cv::Size(3, 3), 0);

    cv::Mat processed = denoised;
    if (rotation >= 0) {
        switch (rotation) {
        case 0: cv::rotate(processed, processed, cv::ROTATE_90_CLOCKWISE); break;
        case 1: cv::rotate(processed, processed, cv::ROTATE_180); break;
        case 2: cv::rotate(processed, processed, cv::ROTATE_90_COUNTERCLOCKWISE); break;
        default: break;
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

    cv::Ptr<cv::ORB> orb = cv::ORB::create(
        1800,
        1.2f,
        8,
        31,
        0,
        2,
        cv::ORB::HARRIS_SCORE,
        31,
        20
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
    if (queryDesc.empty() || trainDesc.empty()) {
        return {board_id, 0, 0};
    }

    auto scoreMatches = [&](const std::vector<cv::DMatch>& goodMatches,
                            const char *tag) -> MatchResult {
        if (goodMatches.empty()) {
            return {board_id, 0, 0};
        }

        double minDist = goodMatches[0].distance;
        double maxDist = goodMatches[0].distance;
        for (const auto& match : goodMatches) {
            minDist = std::min(minDist, static_cast<double>(match.distance));
            maxDist = std::max(maxDist, static_cast<double>(match.distance));
        }

        std::cout << "  Board " << board_id << " [" << tag << "]: "
                  << goodMatches.size()
                  << " good matches (min_dist: " << minDist
                  << ", max_dist: " << maxDist << ")" << std::endl;

        if (goodMatches.size() < 8) {
            std::cout << "    Too few good matches, returning low score" << std::endl;
            return {board_id, static_cast<int>(goodMatches.size()), 0};
        }

        double avgDistance = 0.0;
        for (const auto& match : goodMatches) {
            avgDistance += match.distance;
        }
        avgDistance /= goodMatches.size();

        double distanceVariance = 0.0;
        for (const auto& match : goodMatches) {
            const double diff = match.distance - avgDistance;
            distanceVariance += diff * diff;
        }
        distanceVariance /= goodMatches.size();
        const double distanceStdDev = std::sqrt(distanceVariance);

        const double queryCoverage = static_cast<double>(goodMatches.size()) / std::max(1, queryDesc.rows);
        const double trainCoverage = static_cast<double>(goodMatches.size()) / std::max(1, trainDesc.rows);
        const double coverageScore = std::min(queryCoverage, trainCoverage);

        const double qualityScore = 1.0 / (1.0 + avgDistance / 24.0);
        const double consistencyScore = 1.0 / (1.0 + distanceStdDev / 20.0);
        const double countScore = std::min(1.0, static_cast<double>(goodMatches.size()) / 80.0);
        const double coverageBoost = std::min(1.0, coverageScore * 6.0);

        double normalizedScore =
            countScore * 0.35 +
            qualityScore * 0.25 +
            consistencyScore * 0.20 +
            coverageBoost * 0.20;

        if (goodMatches.size() > 24 && avgDistance < 24.0 && distanceStdDev < 12.0) {
            normalizedScore += 0.08;
        }
        if (coverageScore < 0.04) {
            normalizedScore *= 0.55;
        }
        if (avgDistance > 40.0 || distanceStdDev > 25.0) {
            normalizedScore *= 0.5;
        }

        const double matchScore = std::clamp(normalizedScore, 0.0, 1.0) * 100.0;

        std::cout << "    Avg distance: " << avgDistance
                  << ", Std dev: " << distanceStdDev
                  << ", Coverage: " << coverageScore
                  << ", Count score: " << countScore
                  << ", Quality score: " << qualityScore
                  << ", Consistency: " << consistencyScore
                  << ", Final score: " << matchScore << std::endl;

        return {board_id, static_cast<int>(goodMatches.size()), matchScore};
    };

    std::vector<std::vector<cv::DMatch>> forwardMatches;
    std::vector<std::vector<cv::DMatch>> reverseMatches;
    try {
        cv::Ptr<cv::BFMatcher> matcher = cv::BFMatcher::create(cv::NORM_HAMMING, false);
        matcher->knnMatch(queryDesc, trainDesc, forwardMatches, 2);
        matcher->knnMatch(trainDesc, queryDesc, reverseMatches, 2);
    } catch (const cv::Exception& e) {
        std::cout << "Matching failed for " << board_id << ": " << e.what() << std::endl;
        return {board_id, 0, 0};
    }

    constexpr float kRatioThreshold = 0.75f;
    std::unordered_map<int, cv::DMatch> reverseBest;
    for (const auto& knn : reverseMatches) {
        if (knn.size() < 2) {
            continue;
        }
        if (knn[0].distance >= kRatioThreshold * knn[1].distance) {
            continue;
        }
        reverseBest[knn[0].queryIdx] = knn[0];
    }

    std::vector<cv::DMatch> goodMatches;
    goodMatches.reserve(forwardMatches.size());
    for (const auto& knn : forwardMatches) {
        if (knn.size() < 2) {
            continue;
        }
        const cv::DMatch& best = knn[0];
        const cv::DMatch& second = knn[1];
        if (best.distance >= kRatioThreshold * second.distance) {
            continue;
        }

        const auto reverseIt = reverseBest.find(best.trainIdx);
        if (reverseIt == reverseBest.end()) {
            continue;
        }
        if (reverseIt->second.trainIdx != best.queryIdx) {
            continue;
        }

        goodMatches.push_back(best);
    }

    if (goodMatches.empty()) {
        std::cout << "  Board " << board_id << " [ratio_cross]: 0 good matches, fallback to loose matcher" << std::endl;
    } else {
        std::cout << "  Board " << board_id << ": "
                  << forwardMatches.size() << " knn pairs, "
                  << goodMatches.size() << " ratio+cross matches" << std::endl;
    }

    MatchResult strictResult = scoreMatches(goodMatches, "ratio_cross");

    std::vector<cv::DMatch> fallbackMatches;
    try {
        cv::Ptr<cv::BFMatcher> matcher = cv::BFMatcher::create(cv::NORM_HAMMING, false);
        matcher->match(queryDesc, trainDesc, fallbackMatches);
    } catch (const cv::Exception& e) {
        std::cout << "Fallback matching failed for " << board_id << ": " << e.what() << std::endl;
        return strictResult;
    }

    if (fallbackMatches.empty()) {
        return strictResult;
    }

    double minDist = fallbackMatches[0].distance;
    for (const auto& match : fallbackMatches) {
        minDist = std::min(minDist, static_cast<double>(match.distance));
    }

    const double looseThreshold = std::max(2.5 * minDist, 35.0);
    std::vector<cv::DMatch> looseGoodMatches;
    for (const auto& match : fallbackMatches) {
        if (match.distance <= looseThreshold) {
            looseGoodMatches.push_back(match);
        }
    }

    MatchResult looseResult = scoreMatches(looseGoodMatches, "loose_fallback");
    if (strictResult.matchScore <= 0.0 && looseResult.matchScore > 0.0) {
        return looseResult;
    }
    if (looseResult.matchScore > strictResult.matchScore * 1.2) {
        return looseResult;
    }
    if (strictResult.matchScore > 0.0) {
        return strictResult;
    }
    return looseResult;
}

std::vector<SiftMatcher::MatchResult> SiftMatcher::matchImage(const cv::Mat& inputImage,
                                                              bool imageIsPcbCrop) {
    std::vector<cv::Mat> loadedDescriptors;
    std::vector<std::string> loadedNames;
    loadDescriptors(m_databasePath, loadedDescriptors, loadedNames);

    cv::Mat queryDesc = extractDescriptor(inputImage, -1, imageIsPcbCrop);
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

        if (result.goodMatchCount > 0) {
            std::cout << "Match with " << result.boardId
                      << ": " << result.goodMatchCount << " good matches, score: "
                      << result.matchScore << std::endl;
        }
    }

    std::sort(matchResults.begin(), matchResults.end(), [](const MatchResult& a, const MatchResult& b) {
        return a.matchScore > b.matchScore;
    });

    std::vector<MatchResult> results;
    std::set<std::string> processedBoardIds;

    for (const auto& result : matchResults) {
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

void SiftMatcher::appendToDatabase(const std::vector<std::string>& boardid,
                                   const std::vector<cv::Mat>& images,
                                   bool imagesArePcbCrops) {
    std::vector<cv::Mat> descriptors;
    std::vector<std::string> boardids;

    if (boardid.size() != images.size()) {
        return;
    }
    try {
        loadDescriptors(m_databasePath, descriptors, boardids);
    } catch (const cv::Exception&) {
        std::cout << "Creating new database" << std::endl;
    }

    for (int i = 0; i < boardid.size(); ++i) {
        for (int rotation = -1; rotation < 3; rotation++) {
            cv::Mat desc = extractDescriptor(images[i], rotation, imagesArePcbCrops);
            if (!desc.empty()) {
                descriptors.push_back(desc);
                boardids.push_back(boardid[i]);
            }
        }
    }

    saveDescriptors(m_databasePath, descriptors, boardids);
}

bool SiftMatcher::removeFromDatabase(const std::string& board_id) {
    std::vector<cv::Mat> descriptors;
    std::vector<std::string> imageNames;
    bool found = false;

    try {
        loadDescriptors(m_databasePath, descriptors, imageNames);

        std::vector<cv::Mat> newDescriptors;
        std::vector<std::string> newImageNames;

        for (size_t i = 0; i < imageNames.size(); i++) {
            if (imageNames[i] != board_id) {
                newDescriptors.push_back(descriptors[i]);
                newImageNames.push_back(imageNames[i]);
            } else {
                found = true;
            }
        }

        if (found) {
            saveDescriptors(m_databasePath, newDescriptors, newImageNames);
        }
        return found;

    } catch (const cv::Exception& e) {
        std::cout << "Error while removing image from database: " << e.what() << std::endl;
        return false;
    }
}
