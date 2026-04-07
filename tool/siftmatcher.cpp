#include "siftmatcher.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <set>
#include <vector>

namespace {
constexpr int kOrbNFeatures = 2000;
constexpr float kOrbScaleFactor = 1.2f;
constexpr int kOrbNLevels = 8;
constexpr int kOrbEdgeThreshold = 31;
constexpr int kOrbFirstLevel = 0;
constexpr int kOrbWtaK = 2;
constexpr int kOrbPatchSize = 31;
constexpr int kOrbFastThreshold = 20;

constexpr float kRatioThreshold = 0.75f;
constexpr double kRansacReprojThreshold = 5.0;
constexpr double kRansacConfidence = 0.995;
constexpr int kRansacMaxIters = 2000;
constexpr int kMinInliers = 12;
constexpr double kMinInlierRatio = 0.25;

cv::Mat keypointsToMat(const std::vector<cv::KeyPoint> &keypoints)
{
    if (keypoints.empty()) {
        return {};
    }

    cv::Mat keypointsXY(static_cast<int>(keypoints.size()), 2, CV_32F);
    for (int i = 0; i < static_cast<int>(keypoints.size()); ++i) {
        keypointsXY.at<float>(i, 0) = keypoints[static_cast<size_t>(i)].pt.x;
        keypointsXY.at<float>(i, 1) = keypoints[static_cast<size_t>(i)].pt.y;
    }
    return keypointsXY;
}

bool isKeypointsMatValid(const cv::Mat &mat)
{
    return !mat.empty() && mat.type() == CV_32F && mat.cols == 2;
}

cv::Point2f pointAt(const cv::Mat &mat, int row)
{
    return {mat.at<float>(row, 0), mat.at<float>(row, 1)};
}
}

void SiftMatcher::saveDescriptors(const std::string &filename,
                                  const std::vector<cv::Mat> &descriptors,
                                  const std::vector<cv::Mat> &keypointsXY,
                                  const std::vector<std::string> &boardIds)
{
    cv::FileStorage fs(filename, cv::FileStorage::WRITE);

    fs << "image_count" << static_cast<int>(descriptors.size());
    for (size_t i = 0; i < descriptors.size(); ++i) {
        fs << "image_name_" + std::to_string(i) << boardIds[i];
        fs << "descriptor_" + std::to_string(i) << descriptors[i];
        fs << "keypoints_xy_" + std::to_string(i) << keypointsXY[i];
    }

    fs.release();
}

void SiftMatcher::loadDescriptors(const std::string &filename,
                                  std::vector<cv::Mat> &descriptors,
                                  std::vector<cv::Mat> &keypointsXY,
                                  std::vector<std::string> &boardIds)
{
    descriptors.clear();
    keypointsXY.clear();
    boardIds.clear();

    cv::FileStorage fs(filename, cv::FileStorage::READ);
    if (!fs.isOpened()) {
        return;
    }

    int imageCount = 0;
    fs["image_count"] >> imageCount;
    for (int i = 0; i < imageCount; ++i) {
        cv::Mat descriptor;
        cv::Mat keypointsMat;
        std::string name;

        fs["image_name_" + std::to_string(i)] >> name;
        fs["descriptor_" + std::to_string(i)] >> descriptor;
        fs["keypoints_xy_" + std::to_string(i)] >> keypointsMat;

        descriptors.push_back(descriptor);
        keypointsXY.push_back(keypointsMat);
        boardIds.push_back(name);
    }

    fs.release();
}

SiftMatcher::SiftMatcher()
{
    if (!checkGPU()) {
        std::cout << "Warning: GPU not available, falling back to CPU implementation" << std::endl;
        return;
    }

    try {
        gpu_detector = cv::cuda::ORB::create(
            kOrbNFeatures,
            kOrbScaleFactor,
            kOrbNLevels,
            kOrbEdgeThreshold,
            kOrbFirstLevel,
            kOrbWtaK,
            cv::ORB::HARRIS_SCORE,
            kOrbPatchSize,
            kOrbFastThreshold);
    } catch (const cv::Exception &) {
        gpu_detector.release();
    }
}

SiftMatcher::~SiftMatcher()
{
    gpu_detector.release();
}

void SiftMatcher::setDatabasePath(const std::string &filename)
{
    if (!filename.empty()) {
        m_databasePath = filename;
    }
}

bool SiftMatcher::checkGPU()
{
    try {
        const int deviceCount = cv::cuda::getCudaEnabledDeviceCount();
        if (deviceCount <= 0) {
            return false;
        }

        cv::cuda::DeviceInfo deviceInfo;
        if (!deviceInfo.isCompatible()) {
            return false;
        }

        cv::cuda::setDevice(0);
        return true;
    } catch (const cv::Exception &) {
        return false;
    }
}

SiftMatcher::FeatureData SiftMatcher::extractFeatures(const cv::Mat &image,
                                                      int rotation,
                                                      bool imageIsPcbCrop)
{
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
        return {};
    }

    cv::Mat gray;
    if (extracted.channels() == 1) {
        gray = extracted.clone();
    } else {
        cv::cvtColor(extracted, gray, cv::COLOR_BGR2GRAY);
    }

    cv::Mat processed = gray;
    if (rotation >= 0) {
        switch (rotation) {
        case 0:
            cv::rotate(processed, processed, cv::ROTATE_90_CLOCKWISE);
            break;
        case 1:
            cv::rotate(processed, processed, cv::ROTATE_180);
            break;
        case 2:
            cv::rotate(processed, processed, cv::ROTATE_90_COUNTERCLOCKWISE);
            break;
        default:
            break;
        }
    }

    if (gpu_detector) {
        try {
            cv::cuda::GpuMat gpuImage(processed);
            cv::cuda::GpuMat gpuDescriptors;
            std::vector<cv::KeyPoint> keypoints;

            gpu_detector->detectAndCompute(gpuImage, cv::cuda::GpuMat(), keypoints, gpuDescriptors);
            cv::Mat descriptors;
            gpuDescriptors.download(descriptors);

            if (!descriptors.empty() && keypoints.size() >= 4) {
                return {descriptors, keypointsToMat(keypoints)};
            }
        } catch (const cv::Exception &) {
        }
    }

    cv::Ptr<cv::ORB> orb = cv::ORB::create(
        kOrbNFeatures,
        kOrbScaleFactor,
        kOrbNLevels,
        kOrbEdgeThreshold,
        kOrbFirstLevel,
        kOrbWtaK,
        cv::ORB::HARRIS_SCORE,
        kOrbPatchSize,
        kOrbFastThreshold);

    std::vector<cv::KeyPoint> keypoints;
    cv::Mat descriptors;
    orb->detectAndCompute(processed, cv::noArray(), keypoints, descriptors);
    if (descriptors.empty() || keypoints.size() < 4) {
        return {};
    }

    return {descriptors, keypointsToMat(keypoints)};
}

cv::Mat SiftMatcher::extractDescriptor(const cv::Mat &image, int rotation, bool imageIsPcbCrop)
{
    return extractFeatures(image, rotation, imageIsPcbCrop).descriptors;
}

SiftMatcher::MatchResult SiftMatcher::computeMatchScore(const FeatureData &query,
                                                        const FeatureData &train,
                                                        const std::string &boardId)
{
    MatchResult result;
    result.boardId = boardId;

    if (query.descriptors.empty() || train.descriptors.empty()
        || !isKeypointsMatValid(query.keypointsXY)
        || !isKeypointsMatValid(train.keypointsXY)) {
        return result;
    }

    std::vector<std::vector<cv::DMatch>> knnMatches;
    try {
        cv::Ptr<cv::BFMatcher> matcher = cv::BFMatcher::create(cv::NORM_HAMMING, false);
        matcher->knnMatch(query.descriptors, train.descriptors, knnMatches, 2);
    } catch (const cv::Exception &e) {
        std::cout << "Matching failed for " << boardId << ": " << e.what() << std::endl;
        return result;
    }

    std::vector<cv::DMatch> goodMatches;
    goodMatches.reserve(knnMatches.size());
    for (const auto &pair : knnMatches) {
        if (pair.size() < 2) {
            continue;
        }

        const cv::DMatch &best = pair[0];
        const cv::DMatch &second = pair[1];
        if (second.distance <= 0.0f) {
            continue;
        }
        if ((best.distance / second.distance) < kRatioThreshold) {
            goodMatches.push_back(best);
        }
    }

    result.goodMatchCount = static_cast<int>(goodMatches.size());
    if (result.goodMatchCount < 4) {
        return result;
    }

    std::vector<cv::Point2f> queryPoints;
    std::vector<cv::Point2f> trainPoints;
    queryPoints.reserve(goodMatches.size());
    trainPoints.reserve(goodMatches.size());
    for (const auto &match : goodMatches) {
        queryPoints.push_back(pointAt(query.keypointsXY, match.queryIdx));
        trainPoints.push_back(pointAt(train.keypointsXY, match.trainIdx));
    }

    cv::Mat inlierMask;
    cv::Mat homography;
    try {
        homography = cv::findHomography(queryPoints,
                                        trainPoints,
                                        cv::RANSAC,
                                        kRansacReprojThreshold,
                                        inlierMask,
                                        kRansacMaxIters,
                                        kRansacConfidence);
    } catch (const cv::Exception &e) {
        std::cout << "RANSAC failed for " << boardId << ": " << e.what() << std::endl;
        return result;
    }

    if (homography.empty() || inlierMask.empty()) {
        return result;
    }

    result.inlierCount = cv::countNonZero(inlierMask);
    result.inlierRatio = static_cast<double>(result.inlierCount) / std::max(1, result.goodMatchCount);
    result.rejected = (result.inlierCount < kMinInliers) || (result.inlierRatio < kMinInlierRatio);
    result.matchScore = result.rejected ? 0.0 : result.inlierRatio * 100.0;

    std::cout << "Board " << boardId
              << ": good=" << result.goodMatchCount
              << ", inliers=" << result.inlierCount
              << ", inlier_ratio=" << result.inlierRatio
              << ", rejected=" << (result.rejected ? "true" : "false")
              << std::endl;

    return result;
}

std::vector<SiftMatcher::MatchResult> SiftMatcher::matchImage(const cv::Mat &inputImage,
                                                              bool imageIsPcbCrop)
{
    std::vector<cv::Mat> loadedDescriptors;
    std::vector<cv::Mat> loadedKeypointsXY;
    std::vector<std::string> loadedNames;
    loadDescriptors(m_databasePath, loadedDescriptors, loadedKeypointsXY, loadedNames);

    FeatureData query = extractFeatures(inputImage, -1, imageIsPcbCrop);
    if (query.descriptors.empty() || !isKeypointsMatValid(query.keypointsXY)) {
        std::cout << "Warning: Failed to extract valid ORB features from query image" << std::endl;
        return {};
    }

    std::vector<MatchResult> matchResults;
    int invalidTemplateCount = 0;
    for (size_t i = 0; i < loadedDescriptors.size(); ++i) {
        FeatureData train;
        train.descriptors = loadedDescriptors[i];
        if (i < loadedKeypointsXY.size()) {
            train.keypointsXY = loadedKeypointsXY[i];
        }

        if (train.descriptors.empty() || !isKeypointsMatValid(train.keypointsXY)) {
            ++invalidTemplateCount;
            continue;
        }

        MatchResult result = computeMatchScore(query, train, loadedNames[i]);
        if (!result.rejected && result.inlierCount > 0) {
            matchResults.push_back(result);
        }
    }

    if (invalidTemplateCount > 0) {
        std::cout << "Warning: " << invalidTemplateCount
                  << " template entries are missing keypoints_xy. Rebuild descriptors_database.yml"
                  << std::endl;
    }

    std::sort(matchResults.begin(), matchResults.end());

    std::vector<MatchResult> results;
    std::set<std::string> processedBoardIds;
    for (const auto &result : matchResults) {
        if (processedBoardIds.insert(result.boardId).second) {
            results.push_back(result);
        }
    }
    return results;
}

void SiftMatcher::appendToDatabase(const std::vector<std::string> &boardIds,
                                   const std::vector<cv::Mat> &images,
                                   bool imagesArePcbCrops)
{
    if (boardIds.size() != images.size()) {
        return;
    }

    std::vector<cv::Mat> descriptors;
    std::vector<cv::Mat> keypointsXY;
    std::vector<std::string> names;
    loadDescriptors(m_databasePath, descriptors, keypointsXY, names);

    for (size_t i = 0; i < boardIds.size(); ++i) {
        for (int rotation = -1; rotation < 3; ++rotation) {
            FeatureData features = extractFeatures(images[i], rotation, imagesArePcbCrops);
            if (features.descriptors.empty() || !isKeypointsMatValid(features.keypointsXY)) {
                continue;
            }
            descriptors.push_back(features.descriptors);
            keypointsXY.push_back(features.keypointsXY);
            names.push_back(boardIds[i]);
        }
    }

    saveDescriptors(m_databasePath, descriptors, keypointsXY, names);
}

bool SiftMatcher::removeFromDatabase(const std::string &boardId)
{
    std::vector<cv::Mat> descriptors;
    std::vector<cv::Mat> keypointsXY;
    std::vector<std::string> names;
    loadDescriptors(m_databasePath, descriptors, keypointsXY, names);

    bool found = false;
    std::vector<cv::Mat> newDescriptors;
    std::vector<cv::Mat> newKeypointsXY;
    std::vector<std::string> newNames;

    for (size_t i = 0; i < names.size(); ++i) {
        if (names[i] == boardId) {
            found = true;
            continue;
        }
        newDescriptors.push_back(i < descriptors.size() ? descriptors[i] : cv::Mat());
        newKeypointsXY.push_back(i < keypointsXY.size() ? keypointsXY[i] : cv::Mat());
        newNames.push_back(names[i]);
    }

    if (found) {
        saveDescriptors(m_databasePath, newDescriptors, newKeypointsXY, newNames);
    }
    return found;
}
