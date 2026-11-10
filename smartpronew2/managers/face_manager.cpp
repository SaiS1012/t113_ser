#include "face_manager.h"
#include <QDebug>
#include <QImage>

FaceManager::FaceManager(QObject* parent)
    : QObject(parent)
{
    faceRecognizer = cv::face::LBPHFaceRecognizer::create();
}

FaceManager::~FaceManager() {}

// 功能：保存一张人脸样本并报告当前采集进度。
void FaceManager::addFaceSample(const QImage& faceImg)
{
    if (registeredFaces.size() >= collectTarget) return;
    QImage gray = faceImg.convertToFormat(QImage::Format_Grayscale8);
    cv::Mat mat(gray.height(), gray.width(), CV_8UC1, (void*)gray.bits(), gray.bytesPerLine());
    registeredFaces.append(mat.clone());
    emit faceSampled(registeredFaces.size(), collectTarget);
}

// 功能：使用已采集的人脸样本训练识别器。
void FaceManager::train()
{
    if (registeredFaces.size() < collectTarget) {
        emit trainFinished(false);
        return;
    }
    std::vector<int> labels(collectTarget, 1);
    faceRecognizer->train(QVector<cv::Mat>(registeredFaces).toStdVector(), labels);
    emit trainFinished(true);
}

// 功能：清除当前会话中已采集的人脸样本。
void FaceManager::clear()
{
    registeredFaces.clear();
}

// 功能：使用训练好的识别器判断输入人脸是否匹配。
void FaceManager::recognize(const QImage& faceImg)
{
    if (registeredFaces.size() < collectTarget) {
        emit recognizeResult(false, 0.0);
        return;
    }
    QImage gray = faceImg.convertToFormat(QImage::Format_Grayscale8);
    cv::Mat testFace(gray.height(), gray.width(), CV_8UC1, (void*)gray.bits(), gray.bytesPerLine());
    int predictedLabel = -1;
    double confidence = 0.0;
    faceRecognizer->predict(testFace, predictedLabel, confidence);
    bool success = (predictedLabel == 1 && confidence < 90);
    emit recognizeResult(success, confidence);
}

// 功能：返回当前已采集的人脸样本数量。
int FaceManager::sampleCount() const
{
    return registeredFaces.size();
}
