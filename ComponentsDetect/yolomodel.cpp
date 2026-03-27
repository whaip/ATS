#include "yolomodel.h"
#include <filesystem>
#include <onnxruntime/onnxruntime_cxx_api.h>
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include "../tool/pcb_extract.h"

YOLOModel::YOLOModel()
    : env(std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "yolomodel")),
    session_options(),
    session(),
    memory_info(Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeCPU)),
    model_path(defaultModelPath().toStdWString())
{
    loadModel();
}

YOLOModel::YOLOModel(const QString &modelPath)
    : env(std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "yolomodel")),
      session_options(),
      session(),
      memory_info(Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeCPU))
{
    setModelPath(modelPath);
    loadModel();
}

YOLOModel::~YOLOModel(){
    env.reset();
    session.reset();
}

void YOLOModel::setModelPath(const QString &modelPath)
{
    if (modelPath.isEmpty()) {
        return;
    }
    model_path = modelPath.toStdWString();
}

QString YOLOModel::defaultModelPath()
{
    const QString baseDir = QCoreApplication::instance()
        ? QCoreApplication::applicationDirPath()
        : QDir::currentPath();
    return QDir(baseDir).filePath(QStringLiteral("model/best.onnx"));
}

void YOLOModel::loadModel(){
    std::cout << "session_options init" << std::endl;
    session_options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_EXTENDED);
    
    std::cout << "session_options set" << std::endl;
    OrtStatus* status = OrtSessionOptionsAppendExecutionProvider_CUDA(session_options, 0);
    if (status != nullptr) {
        std::cerr << "CUDA execution provider is not available." << std::endl;
        Ort::GetApi().ReleaseStatus(status);
        status = nullptr;
    } else {
        std::cout << "Using CUDA execution provider." << std::endl;
    }

    try {
        session = std::make_unique<Ort::Session>(*env, model_path.c_str(), session_options);
        std::cout << "session init" << std::endl;
    }
    catch (const Ort::Exception &e) {
        qCritical() << "创建 Ort::Session 失败："
                    << e.what()
                    << "，错误码 =" << e.GetOrtErrorCode();
    }
}

cv::Mat YOLOModel::recognize(const cv::Mat& src_img)
{
    // Backward-compatible API: no longer draws overlays.
    (void)infer(src_img, true, nullptr, nullptr);
    return src_img;
}

cv::Mat YOLOModel::recognize(const cv::Mat& src_img, bool usePcbExtract, int /*pcbQuadThickness*/)
{
    (void)infer(src_img, usePcbExtract, nullptr, nullptr);
    return src_img;
}

std::vector<CompLabel> YOLOModel::infer(const cv::Mat& src_img,
                                       bool usePcbExtract,
                                       std::array<cv::Point2f,4>* pcbQuadOut,
                                       bool* extractedOut)
{
    CompLabels.clear();
    if (extractedOut) {
        *extractedOut = false;
    }
    if (src_img.empty()) {
        return CompLabels;
    }

    cv::Mat working_img = src_img;

    cv::Mat warped, H, Hinv;
    cv::Size warped_size;
    std::array<cv::Point2f,4> pcbQuad{};
    bool extracted = false;

    if (usePcbExtract) {
        try {
            // NOTE: extractWithHomography may modify its input; keep a local clone.
            cv::Mat tmp = working_img.clone();
            extracted = PCB_EXTRACT->extractWithHomography(tmp, warped, H, warped_size, &pcbQuad);
            working_img = tmp;
        } catch (...) {
            extracted = false;
        }
    }

    if (extractedOut) {
        *extractedOut = extracted;
    }
    if (pcbQuadOut) {
        *pcbQuadOut = pcbQuad;
    }

    cv::Mat inference_img;
    if (extracted && !warped.empty() && H.cols == 3 && H.rows == 3) {
        inference_img = warped;
        cv::invert(H, Hinv);
    } else {
        inference_img = working_img;
    }

    cv::Mat resized;
    cv::resize(inference_img, resized, cv::Size(640, 640));
    std::vector<float> img_vector = img2vector(resized);
    std::vector<int64_t> dim = { 1, 3, 640, 640 };
    Ort::Value input_tensor = Ort::Value::CreateTensor<float>(memory_info, img_vector.data(), img_vector.size(), dim.data(), dim.size());

    std::vector<const char*> input_names = { "images" };
    std::vector<const char*> output_names = { "output0" };
    try {
        std::vector<Ort::Value> output_tensors = session->Run(Ort::RunOptions{ nullptr }, input_names.data(), &input_tensor, input_names.size(), output_names.data(), output_names.size());
        float* output = output_tensors[0].GetTensorMutableData<float>();
        auto info = float2vector(output);

        if (extracted && !warped.empty() && Hinv.cols == 3 && Hinv.rows == 3) {
            collect_labels_mapped(info, inference_img.size(), Hinv, working_img.size());
        } else {
            collect_labels_unmapped(info, inference_img.size());
        }
    }
    catch (const Ort::Exception &e) {
        qDebug() << "ONNX Runtime 错误：" << e.what()
                 << "，错误码 =" << e.GetOrtErrorCode();
        CompLabels.clear();
    }

    return CompLabels;
}

void YOLOModel::collect_labels_unmapped(const std::vector<std::vector<float>>& info, const cv::Size& imgSize)
{
    CompLabels.clear();
    const float w = static_cast<float>(imgSize.width);
    const float h = static_cast<float>(imgSize.height);

    for (int i = 0; i < static_cast<int>(info.size()); i++)
    {
        const int cls = static_cast<int>(info[i][5]);
        if(std::find(class_display.begin(), class_display.end(), cls) == class_display.end())
            continue;
        if (cls < 0 || cls >= static_cast<int>(class_name.size()))
            continue;

        const double x1 = info[i][0] * w / 640.0;
        const double y1 = info[i][1] * h / 640.0;
        const double x2 = info[i][2] * w / 640.0;
        const double y2 = info[i][3] * h / 640.0;

        const double xmin = std::min(x1, x2);
        const double ymin = std::min(y1, y2);
        const double xmax = std::max(x1, x2);
        const double ymax = std::max(y1, y2);
        const double bw = xmax - xmin;
        const double bh = ymax - ymin;
        if (bw <= 1.0 || bh <= 1.0) {
            continue;
        }

        CompLabels.push_back(CompLabel(i,
                                      xmin,
                                      ymin,
                                      bw,
                                      bh,
                                      cls,
                                      info[i][4],
                                      QString::fromStdString(class_name[cls]),
                                      "",
                                      QByteArray()));
    }
}

void YOLOModel::collect_labels_mapped(const std::vector<std::vector<float>>& info,
                                     const cv::Size& warped_size,
                                     const cv::Mat& Hinv,
                                     const cv::Size& original_size)
{
    CompLabels.clear();
    const float w640 = 640.0f;
    const float h640 = 640.0f;

    for (int i = 0; i < static_cast<int>(info.size()); ++i)
    {
        const int cls = static_cast<int>(info[i][5]);
        if(std::find(class_display.begin(), class_display.end(), cls) == class_display.end())
            continue;
        if (cls < 0 || cls >= static_cast<int>(class_name.size()))
            continue;

        float x1 = info[i][0] * static_cast<float>(warped_size.width)  / w640;
        float y1 = info[i][1] * static_cast<float>(warped_size.height) / h640;
        float x2 = info[i][2] * static_cast<float>(warped_size.width)  / w640;
        float y2 = info[i][3] * static_cast<float>(warped_size.height) / h640;

        std::vector<cv::Point2f> warped_pts = {
            {x1, y1}, {x2, y1}, {x2, y2}, {x1, y2}
        };
        std::vector<cv::Point2f> orig_pts;
        cv::perspectiveTransform(warped_pts, orig_pts, Hinv);

        float minx = std::min(std::min(orig_pts[0].x, orig_pts[1].x), std::min(orig_pts[2].x, orig_pts[3].x));
        float maxx = std::max(std::max(orig_pts[0].x, orig_pts[1].x), std::max(orig_pts[2].x, orig_pts[3].x));
        float miny = std::min(std::min(orig_pts[0].y, orig_pts[1].y), std::min(orig_pts[2].y, orig_pts[3].y));
        float maxy = std::max(std::max(orig_pts[0].y, orig_pts[1].y), std::max(orig_pts[2].y, orig_pts[3].y));

        const double xmin = std::clamp(static_cast<double>(minx), 0.0, std::max(0.0, static_cast<double>(original_size.width - 1)));
        const double ymin = std::clamp(static_cast<double>(miny), 0.0, std::max(0.0, static_cast<double>(original_size.height - 1)));
        const double xmax = std::clamp(static_cast<double>(maxx), 0.0, std::max(0.0, static_cast<double>(original_size.width - 1)));
        const double ymax = std::clamp(static_cast<double>(maxy), 0.0, std::max(0.0, static_cast<double>(original_size.height - 1)));

        const double bw = xmax - xmin;
        const double bh = ymax - ymin;
        if (bw <= 1.0 || bh <= 1.0) {
            continue;
        }

        CompLabels.push_back(CompLabel(i,
                                      xmin,
                                      ymin,
                                      bw,
                                      bh,
                                      cls,
                                      info[i][4],
                                      QString::fromStdString(class_name[cls]),
                                      "",
                                      QByteArray()));
    }
}



std::vector<float> YOLOModel::img2vector(const cv::Mat& img)
{
    std::vector<float> B;

    vector<float> G;
    vector<float> R;
    B.reserve(640 * 640 * 3);
    G.reserve(640 * 640);
    R.reserve(640 * 640);
    const uchar* pdata = (uchar*)img.datastart;
    for (int i = 0; i < img.dataend - img.datastart; i += 3)
    {
        B.push_back((float)*(pdata + i) / 255.0);
        G.push_back((float)*(pdata + i + 1) / 255.0);
        R.push_back((float)*(pdata + i + 2) / 255.0);
    }
    B.insert(B.cend(), G.cbegin(), G.cend());
    B.insert(B.cend(), R.cbegin(), R.cend());
    return B;
}

void YOLOModel::print_float_data(const float* const pdata, int data_num_per_line, int data_num)
{
    for (int i = 0; i < data_num; i++)


    {
        for (int j = 0; j < data_num_per_line; j++)
        {
            cout << *(pdata + i * data_num_per_line + j) << " ";
        }
        cout << endl;
    }
}

std::vector<std::vector<float>> YOLOModel::float2vector(const float* const pdata, int data_num_per_line, int data_num, float conf)
{
    std::vector<std::vector<float>> info;


    vector<float> info_line;
    for (int i = 0; i < data_num; i++)
    {
        if (*(pdata + i * data_num_per_line + 4) < conf)
        {
            continue;
        }
        for (int j = 0; j < data_num_per_line; j++)
        {
            //cout << *(pdata + i * data_num_per_line + j) << " ";
            info_line.push_back(*(pdata + i * data_num_per_line + j));
        }
        info.push_back(info_line);
        info_line.clear();
        //cout << endl;
    }
    return info;
}

void YOLOModel::set_class_display(const std::vector<int>& class_display)
{
    this->class_display = class_display;
}

std::vector<CompLabel> YOLOModel::get_labels(){
    return CompLabels;
}
