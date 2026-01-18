#ifndef PCB_EXTRACT_H
#define PCB_EXTRACT_H

#include <opencv2/opencv.hpp>
#include <filesystem>
#include <vector>
#include <array>

#define PCB_EXTRACT (PCBExtract::getInstance())

class PCBExtract {
public:
    static PCBExtract* getInstance() {
        static PCBExtract instance;
        return &instance;
    }

    PCBExtract(const PCBExtract&) = delete;
    PCBExtract& operator=(const PCBExtract&) = delete;
    ~PCBExtract();
    cv::Mat extract(const cv::Mat& image, const std::string& model_name);
    // 提供带单应矩阵的提取接口：返回是否成功、透视变换后的图像以及从原图到透视图的变换矩阵
    // H: 原图 -> 透视后的PCB图；warpedSize 为透视图尺寸
    bool extractWithHomography(const cv::Mat &image, cv::Mat &warped, cv::Mat &H, cv::Size &warpedSize);
    bool extractWithHomography(const cv::Mat &image,
                              cv::Mat &warped,
                              cv::Mat &H,
                              cv::Size &warpedSize,
                              std::array<cv::Point2f,4> *orderedQuad);

private:
    PCBExtract();
    double computeMedianGray(const cv::Mat &gray);
    cv::Mat autoCanny(const cv::Mat &gray, double sigma = 0.33);
    cv::Mat createGreenMask(const cv::Mat &bgr);
    cv::Mat createPcbMask(const cv::Mat &bgr);
    std::array<cv::Point2f,4> orderPointsClockwise(const std::vector<cv::Point2f> &pts);
    cv::Mat fourPointWarp(const cv::Mat &image, const std::vector<cv::Point2f> &pts);
    bool findQuadFromMask(const cv::Mat &mask, std::vector<cv::Point2f> &quad, double minAreaRatio = 0.05);
    bool detectAndCropPcb(const cv::Mat &image, cv::Mat &warped, cv::Mat &overlay, cv::Mat &mask, cv::Mat &edges);
};

#endif
