#include "pcb_extract.h"

PCBExtract::PCBExtract() {};
PCBExtract::~PCBExtract() {};

cv::Mat PCBExtract::extract(const cv::Mat& image, const std::string& model_name) {
	cv::Mat warped, overlay, mask, edges;
	bool success = detectAndCropPcb(image, warped, overlay, mask, edges);
	if (success) {
		return warped;
	}
	return cv::Mat();
}

bool PCBExtract::extractWithHomography(const cv::Mat &image, cv::Mat &warped, cv::Mat &H, cv::Size &warpedSize) {
	if (image.empty()) return false;
	cv::Mat mask;
	mask = createGreenMask(image);
	std::vector<cv::Point2f> quad;
	if (!findQuadFromMask(mask, quad)) return false;
	// 规范四边形点顺序
	auto rect = orderPointsClockwise(quad);
	const cv::Point2f &tl = rect[0];
	const cv::Point2f &tr = rect[1];
	const cv::Point2f &br = rect[2];
	const cv::Point2f &bl = rect[3];
	double widthA = cv::norm(br - bl);
	double widthB = cv::norm(tr - tl);
	double heightA = cv::norm(tr - br);
	double heightB = cv::norm(tl - bl);
	int maxWidth = static_cast<int>(std::max(widthA, widthB));
	int maxHeight = static_cast<int>(std::max(heightA, heightB));
	if (maxWidth <= 0 || maxHeight <= 0) return false;
	std::vector<cv::Point2f> dstPts{
		cv::Point2f(0.f, 0.f),
		cv::Point2f(static_cast<float>(maxWidth - 1), 0.f),
		cv::Point2f(static_cast<float>(maxWidth - 1), static_cast<float>(maxHeight - 1)),
		cv::Point2f(0.f, static_cast<float>(maxHeight - 1))
	};
	H = cv::getPerspectiveTransform(rect.data(), dstPts.data());
	cv::warpPerspective(image, warped, H, cv::Size(maxWidth, maxHeight));
	warpedSize = warped.size();
	return !warped.empty();
}

bool PCBExtract::extractWithHomography(const cv::Mat &image,
                                      cv::Mat &warped,
                                      cv::Mat &H,
                                      cv::Size &warpedSize,
                                      std::array<cv::Point2f,4> *orderedQuad)
{
	if (image.empty()) return false;
	cv::Mat mask;
	mask = createGreenMask(image);
	std::vector<cv::Point2f> quad;
	if (!findQuadFromMask(mask, quad)) return false;
	// 规范四边形点顺序
	auto rect = orderPointsClockwise(quad);
	if (orderedQuad) {
		*orderedQuad = rect;
	}
	const cv::Point2f &tl = rect[0];
	const cv::Point2f &tr = rect[1];
	const cv::Point2f &br = rect[2];
	const cv::Point2f &bl = rect[3];
	double widthA = cv::norm(br - bl);
	double widthB = cv::norm(tr - tl);
	double heightA = cv::norm(tr - br);
	double heightB = cv::norm(tl - bl);
	int maxWidth = static_cast<int>(std::max(widthA, widthB));
	int maxHeight = static_cast<int>(std::max(heightA, heightB));
	if (maxWidth <= 0 || maxHeight <= 0) return false;
	std::vector<cv::Point2f> dstPts{
		cv::Point2f(0.f, 0.f),
		cv::Point2f(static_cast<float>(maxWidth - 1), 0.f),
		cv::Point2f(static_cast<float>(maxWidth - 1), static_cast<float>(maxHeight - 1)),
		cv::Point2f(0.f, static_cast<float>(maxHeight - 1))
	};
	H = cv::getPerspectiveTransform(rect.data(), dstPts.data());
	cv::warpPerspective(image, warped, H, cv::Size(maxWidth, maxHeight));
	warpedSize = warped.size();
	return !warped.empty();
}

double PCBExtract::computeMedianGray(const cv::Mat &gray) {
	CV_Assert(gray.type() == CV_8UC1);
	int histSize = 256;
	float range[] = {0.f, 256.f};
	const float *histRange = {range};
	cv::Mat hist;
	cv::calcHist(&gray, 1, 0, cv::Mat(), hist, 1, &histSize, &histRange, true, false);
	// cumulative
	double total = static_cast<double>(gray.total());
	double cumulative = 0.0;
	for (int i = 0; i < 256; ++i) {
		cumulative += hist.at<float>(i);
		if (cumulative >= total * 0.5) return static_cast<double>(i);
	}
	return 127.0;
}

cv::Mat PCBExtract::autoCanny(const cv::Mat &gray, double sigma) {
	CV_Assert(gray.type() == CV_8UC1);
	double med = computeMedianGray(gray);
	int lower = static_cast<int>(std::max(0.0, (1.0 - sigma) * med));
	int upper = static_cast<int>(std::min(255.0, (1.0 + sigma) * med));
	cv::Mat edges;
	cv::Canny(gray, edges, lower, upper);
	return edges;
}

cv::Mat PCBExtract::createPcbMask(const cv::Mat &bgr) {
    // HSV green range (slightly wider and lower S/V thresholds)
    cv::Mat hsv;
    cv::cvtColor(bgr, hsv, cv::COLOR_BGR2HSV);
    cv::Scalar lower1(30, 25, 30);
    cv::Scalar upper1(90, 255, 255);
    cv::Mat mask_hsv;
    cv::inRange(hsv, lower1, upper1, mask_hsv);

    // Green-dominance in BGR: G significantly higher than R,B (robust under highlights)
    std::vector<cv::Mat> ch; cv::split(bgr, ch);
    cv::Mat gdom1 = (ch[1] > ch[0] + 10) & (ch[1] > ch[2] + 10);
    cv::Mat gdom2 = (ch[1] > 60);
    cv::Mat mask_gdom; cv::bitwise_and(gdom1, gdom2, mask_gdom);

    // Combine
    cv::Mat mask; cv::bitwise_or(mask_hsv, mask_gdom, mask);

    // Adaptive morphology based on image size
    int k = std::max(5, std::min(151, std::min(bgr.rows, bgr.cols) / 50));
    if ((k % 2) == 0) k += 1; // ensure odd size
    cv::Mat kernel = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(k, k));
    // Close gaps then slightly dilate to reach board edges
    cv::morphologyEx(mask, mask, cv::MORPH_CLOSE, kernel, cv::Point(-1,-1), 1);
    cv::dilate(mask, mask, kernel, cv::Point(-1,-1), 1);
    return mask;
}

cv::Mat PCBExtract::createGreenMask(const cv::Mat &bgr) {
	// HSV green range (slightly wider and lower S/V thresholds)
	cv::Mat hsv;
	cv::cvtColor(bgr, hsv, cv::COLOR_BGR2HSV);
	cv::Scalar lower1(30, 25, 30);
	cv::Scalar upper1(90, 255, 255);
	cv::Mat mask_hsv;
	cv::inRange(hsv, lower1, upper1, mask_hsv);

	// Green-dominance in BGR: G significantly higher than R,B (robust under highlights)
	std::vector<cv::Mat> ch; cv::split(bgr, ch);
	cv::Mat gdom1 = (ch[1] > ch[0] + 10) & (ch[1] > ch[2] + 10);
	cv::Mat gdom2 = (ch[1] > 60);
	cv::Mat mask_gdom; cv::bitwise_and(gdom1, gdom2, mask_gdom);

	// Combine
	cv::Mat mask; cv::bitwise_or(mask_hsv, mask_gdom, mask);

	// Adaptive morphology based on image size
	int k = std::max(5, std::min(151, std::min(bgr.rows, bgr.cols) / 50));
	if ((k % 2) == 0) k += 1; // ensure odd size
	cv::Mat kernel = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(k, k));
	// Close gaps then slightly dilate to reach board edges
	cv::morphologyEx(mask, mask, cv::MORPH_CLOSE, kernel, cv::Point(-1,-1), 1);
	cv::dilate(mask, mask, kernel, cv::Point(-1,-1), 1);
	return mask;
}

std::array<cv::Point2f,4> PCBExtract::orderPointsClockwise(const std::vector<cv::Point2f> &pts) {
	CV_Assert(pts.size() == 4);
	std::array<cv::Point2f,4> rect{};
	double minSum = 1e18, maxSum = -1e18, minDiff = 1e18, maxDiff = -1e18;
	int tl = -1, br = -1, tr = -1, bl = -1;
	for (int i = 0; i < 4; ++i) {
		double s = pts[i].x + pts[i].y;
		double d = pts[i].x - pts[i].y;
		if (s < minSum) { minSum = s; tl = i; }
		if (s > maxSum) { maxSum = s; br = i; }
		if (d < minDiff) { minDiff = d; tr = i; }
		if (d > maxDiff) { maxDiff = d; bl = i; }
	}
	rect[0] = pts[tl]; // top-left
	rect[1] = pts[tr]; // top-right
	rect[2] = pts[br]; // bottom-right
	rect[3] = pts[bl]; // bottom-left
	return rect;
}

cv::Mat PCBExtract::fourPointWarp(const cv::Mat &image, const std::vector<cv::Point2f> &pts) {
	auto rect = orderPointsClockwise(pts);
	const cv::Point2f &tl = rect[0];
	const cv::Point2f &tr = rect[1];
	const cv::Point2f &br = rect[2];
	const cv::Point2f &bl = rect[3];
	double widthA = cv::norm(br - bl);
	double widthB = cv::norm(tr - tl);
	double heightA = cv::norm(tr - br);
	double heightB = cv::norm(tl - bl);
	int maxWidth = static_cast<int>(std::max(widthA, widthB));
	int maxHeight = static_cast<int>(std::max(heightA, heightB));
	std::vector<cv::Point2f> dstPts{
		cv::Point2f(0.f, 0.f),
		cv::Point2f(static_cast<float>(maxWidth - 1), 0.f),
		cv::Point2f(static_cast<float>(maxWidth - 1), static_cast<float>(maxHeight - 1)),
		cv::Point2f(0.f, static_cast<float>(maxHeight - 1))
	};
	cv::Mat M = cv::getPerspectiveTransform(rect.data(), dstPts.data());
	cv::Mat warped;
	cv::warpPerspective(image, warped, M, cv::Size(maxWidth, maxHeight));
	return warped;
}

bool PCBExtract::findQuadFromMask(const cv::Mat &mask, std::vector<cv::Point2f> &quad, double minAreaRatio) {
	std::vector<std::vector<cv::Point>> contours;
	cv::findContours(mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
	if (contours.empty()) return false;
	double imageArea = static_cast<double>(mask.rows * mask.cols);
	// choose the largest valid contour
	const auto &largest = *std::max_element(contours.begin(), contours.end(), [](const auto &a, const auto &b){
		return cv::contourArea(a) < cv::contourArea(b);
	});
	double largestArea = cv::contourArea(largest);
	if (largestArea < minAreaRatio * imageArea) return false;

	// Candidate 1: approx quad
	double peri = cv::arcLength(largest, true);
	std::vector<cv::Point> approx;
	cv::approxPolyDP(largest, approx, 0.02 * peri, true);

	// Candidate 2: min area rectangle of the largest contour
	cv::RotatedRect rr = cv::minAreaRect(largest);
	cv::Point2f boxPts[4];
	rr.points(boxPts);
	std::vector<cv::Point> boxPtsI; for (auto &p : boxPts) boxPtsI.emplace_back(cv::Point((int)p.x,(int)p.y));
	double rectArea = rr.size.area();

	bool useApprox = (approx.size() == 4);
	if (useApprox) {
		double approxArea = std::fabs(cv::contourArea(approx));
		// if approx quad too small vs rect (e.g., only half PCB), fallback to rect
		if (rectArea > 0.0 && (approxArea / rectArea) < 0.75) {
			useApprox = false;
		}
	}

	if (useApprox) {
		quad.clear();
		for (const auto &p : approx) quad.emplace_back(static_cast<float>(p.x), static_cast<float>(p.y));
		return true;
	} else {
		quad.assign(boxPts, boxPts + 4);
		return true;
	}
}

bool PCBExtract::detectAndCropPcb(const cv::Mat &image, cv::Mat &warped, cv::Mat &overlay, cv::Mat &mask, cv::Mat &edges) {
	overlay = image.clone();
	mask = createPcbMask(image);
	std::vector<cv::Point2f> quad;
	if (!findQuadFromMask(mask, quad)) return false;
	// draw for preview
	std::vector<std::vector<cv::Point>> drawPoly(1);
	for (const auto &p : quad) drawPoly[0].push_back(cv::Point(static_cast<int>(p.x), static_cast<int>(p.y)));
	// resolution-adaptive double stroke: black underlay + red overlay
	int longSide = std::max(overlay.cols, overlay.rows);
	int outerThickness = std::max(8, std::min(32, longSide / 300));
	int innerThickness = std::max(4, std::min(24, longSide / 480));
	cv::polylines(overlay, drawPoly, true, cv::Scalar(0, 0, 0), outerThickness, cv::LINE_AA);
	cv::polylines(overlay, drawPoly, true, cv::Scalar(0, 0, 255), innerThickness, cv::LINE_AA);
	warped = fourPointWarp(image, quad);
	cv::Mat gray;
	cv::cvtColor(warped, gray, cv::COLOR_BGR2GRAY);
	cv::GaussianBlur(gray, gray, cv::Size(5,5), 0.0);
	edges = autoCanny(gray, 0.33);
	return true;
}
