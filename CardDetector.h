// card_detector.h — YOLOv8-seg card detection in C++ (ONNXRuntime).
//
// Runs the exported YOLOv8n-seg model on a full photo and returns one
// quadrilateral (4 points, IMAGE coordinates) per detected card. Those quads
// drop straight into ImageCanvas as 4-point polygon selections — the same
// format a hand-drawn selection uses — so no new crop code is needed.
//
// Matches the Python pipeline (prepare_dataset.py / detect_yolov8.py):
//   letterbox to 640, run model, threshold, NMS, mask -> minAreaRect -> quad.
#pragma once

#include <string>
#include <vector>
#include <memory>
#include <opencv2/core.hpp>
#include <onnxruntime_cxx_api.h>

struct CardDetection {
    std::vector<cv::Point2f> quad;     // 4 corners (minAreaRect) — kept for compatibility
    std::vector<cv::Point2f> polygon;  // TIGHT mask outline (simplified), ORIGINAL image coords
    cv::Point2f centroid;              // for "which card did I click?"
    float score = 0.f;
};

class CardDetector {
public:
    CardDetector(const std::string& onnx_path,
                 int   input_size = 640,
                 float conf_thresh = 0.25f,
                 float iou_thresh  = 0.45f,
                 float mask_thresh = 0.5f);

    // Detect all cards in a BGR image. Quads are in that image's pixel coords.
    std::vector<CardDetection> detect(const cv::Mat& image_bgr);

    void setConf(float c) { conf_ = c; }
    void setIoU(float i)  { iou_ = i; }

private:
    // letterbox to SxS, remember scale + pad so we can map boxes back
    cv::Mat letterbox(const cv::Mat& src, float& scale, int& padx, int& pady);

    Ort::Env            env_;
    Ort::SessionOptions so_;
    std::unique_ptr<Ort::Session> session_;
    std::vector<std::string> inNames_, outNames_;
    std::vector<const char*> inPtrs_, outPtrs_;

    int   S_;
    float conf_, iou_, maskThresh_;
};
