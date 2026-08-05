#include "CardDetector.h"

#include <algorithm>
#include <cmath>
#include <opencv2/imgproc.hpp>

CardDetector::CardDetector(const std::string& onnx_path, int input_size,
                           float conf_thresh, float iou_thresh, float mask_thresh)
    : env_(ORT_LOGGING_LEVEL_WARNING, "yolo"),
      S_(input_size), conf_(conf_thresh), iou_(iou_thresh), maskThresh_(mask_thresh) {
    so_.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

#ifdef _WIN32
    std::wstring wpath(onnx_path.begin(), onnx_path.end());
    session_ = std::make_unique<Ort::Session>(env_, wpath.c_str(), so_);
#else
    session_ = std::make_unique<Ort::Session>(env_, onnx_path.c_str(), so_);
#endif

    Ort::AllocatorWithDefaultOptions alloc;
    for (size_t i = 0; i < session_->GetInputCount(); ++i)
        inNames_.emplace_back(session_->GetInputNameAllocated(i, alloc).get());
    for (size_t i = 0; i < session_->GetOutputCount(); ++i)
        outNames_.emplace_back(session_->GetOutputNameAllocated(i, alloc).get());
    for (auto& s : inNames_)  inPtrs_.push_back(s.c_str());
    for (auto& s : outNames_) outPtrs_.push_back(s.c_str());
}

// Resize preserving aspect into SxS, padding with 114 grey (YOLO convention).
cv::Mat CardDetector::letterbox(const cv::Mat& src, float& scale, int& padx, int& pady) {
    int w = src.cols, h = src.rows;
    scale = std::min(S_ / (float)w, S_ / (float)h);
    int nw = (int)std::round(w * scale), nh = (int)std::round(h * scale);
    cv::Mat resized;
    cv::resize(src, resized, cv::Size(nw, nh), 0, 0, cv::INTER_LINEAR);
    padx = (S_ - nw) / 2;
    pady = (S_ - nh) / 2;
    cv::Mat out(S_, S_, CV_8UC3, cv::Scalar(114, 114, 114));
    resized.copyTo(out(cv::Rect(padx, pady, nw, nh)));
    return out;
}

static float iou_of(const cv::Rect2f& a, const cv::Rect2f& b) {
    float x1 = std::max(a.x, b.x), y1 = std::max(a.y, b.y);
    float x2 = std::min(a.x + a.width,  b.x + b.width);
    float y2 = std::min(a.y + a.height, b.y + b.height);
    float inter = std::max(0.f, x2 - x1) * std::max(0.f, y2 - y1);
    float uni = a.area() + b.area() - inter;
    return uni > 0 ? inter / uni : 0.f;
}

std::vector<CardDetection> CardDetector::detect(const cv::Mat& image_bgr) {
    float scale; int padx, pady;
    cv::Mat lb = letterbox(image_bgr, scale, padx, pady);

    // to CHW float, 0..1, RGB
    cv::Mat rgb; cv::cvtColor(lb, rgb, cv::COLOR_BGR2RGB);
    std::vector<float> input((size_t)3 * S_ * S_);
    for (int y = 0; y < S_; ++y) {
        const cv::Vec3b* row = rgb.ptr<cv::Vec3b>(y);
        for (int x = 0; x < S_; ++x)
            for (int c = 0; c < 3; ++c)
                input[(size_t)c*S_*S_ + (size_t)y*S_ + x] = row[x][c] / 255.0f;
    }

    Ort::MemoryInfo mem = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    std::array<int64_t,4> ishape{1,3,S_,S_};
    std::vector<Ort::Value> ins;
    ins.push_back(Ort::Value::CreateTensor<float>(mem, input.data(), input.size(),
                                                  ishape.data(), ishape.size()));
    auto outs = session_->Run(Ort::RunOptions{nullptr},
                              inPtrs_.data(), ins.data(), ins.size(),
                              outPtrs_.data(), outPtrs_.size());

    // output0: (1, C, A) with C = 4 + num_classes(1) + 32, A anchors
    auto s0 = outs[0].GetTensorTypeAndShapeInfo().GetShape();     // [1, C, A]
    const float* d0 = outs[0].GetTensorData<float>();
    int C = (int)s0[1], A = (int)s0[2];
    int nmask = C - 5;                         // 4 box + 1 class score + mask coeffs
    // d0 is laid out [c][a]; index (c,a) = c*A + a
    auto at = [&](int c, int a){ return d0[(size_t)c*A + a]; };

    // output1: (1, 32, mh, mw) prototypes
    auto s1 = outs[1].GetTensorTypeAndShapeInfo().GetShape();     // [1, 32, mh, mw]
    const float* d1 = outs[1].GetTensorData<float>();
    int mc = (int)s1[1], mh = (int)s1[2], mw = (int)s1[3];

    // ── collect candidates over conf threshold ──────────────────────────────
    struct Cand { cv::Rect2f box; float score; std::vector<float> coeffs; };
    std::vector<Cand> cands;
    for (int a = 0; a < A; ++a) {
        float score = at(4, a);                // single class -> col 4 is the score
        if (score < conf_) continue;
        float cx = at(0,a), cy = at(1,a), w = at(2,a), h = at(3,a);
        Cand c;
        c.box = cv::Rect2f(cx - w/2, cy - h/2, w, h);  // in letterboxed 640 space
        c.score = score;
        c.coeffs.resize(nmask);
        for (int k = 0; k < nmask; ++k) c.coeffs[k] = at(5 + k, a);
        cands.push_back(std::move(c));
    }
    if (cands.empty()) return {};

    // ── NMS ─────────────────────────────────────────────────────────────────
    std::vector<int> idx(cands.size());
    for (int i = 0; i < (int)idx.size(); ++i) idx[i] = i;
    std::sort(idx.begin(), idx.end(), [&](int a, int b){ return cands[a].score > cands[b].score; });
    std::vector<int> keep;
    std::vector<char> removed(cands.size(), 0);
    for (int ii = 0; ii < (int)idx.size(); ++ii) {
        int i = idx[ii];
        if (removed[i]) continue;
        keep.push_back(i);
        for (int jj = ii + 1; jj < (int)idx.size(); ++jj) {
            int j = idx[jj];
            if (!removed[j] && iou_of(cands[i].box, cands[j].box) > iou_) removed[j] = 1;
        }
    }

    // ── build each kept mask, extract rotated quad, map back to image ───────
    std::vector<CardDetection> out;
    const float mx = (float)mw / S_, my = (float)mh / S_;   // 640 -> proto grid
    for (int i : keep) {
        const Cand& c = cands[i];

        // mask (mh x mw) = sigmoid( sum_k coeff_k * proto_k ), restricted to box
        int bx0 = std::max(0, (int)std::floor(c.box.x * mx));
        int by0 = std::max(0, (int)std::floor(c.box.y * my));
        int bx1 = std::min(mw, (int)std::ceil((c.box.x + c.box.width)  * mx));
        int by1 = std::min(mh, (int)std::ceil((c.box.y + c.box.height) * my));
        if (bx1 <= bx0 || by1 <= by0) continue;

        cv::Mat m(mh, mw, CV_8UC1, cv::Scalar(0));
        for (int y = by0; y < by1; ++y) {
            for (int x = bx0; x < bx1; ++x) {
                float acc = 0.f;
                for (int k = 0; k < mc; ++k)
                    acc += c.coeffs[k] * d1[((size_t)k*mh + y)*mw + x];
                float p = 1.f / (1.f + std::exp(-acc));       // sigmoid
                if (p > maskThresh_) m.at<uchar>(y, x) = 255;
            }
        }

        // upscale mask from proto grid (160) to letterbox size (640)
        cv::Mat mBig;
        cv::resize(m, mBig, cv::Size(S_, S_), 0, 0, cv::INTER_LINEAR);

        std::vector<std::vector<cv::Point>> contours;
        cv::findContours(mBig, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
        if (contours.empty()) continue;
        auto& cnt = *std::max_element(contours.begin(), contours.end(),
                    [](auto& a, auto& b){ return cv::contourArea(a) < cv::contourArea(b); });
        if (cv::contourArea(cnt) < 20) continue;

        CardDetection det; det.score = c.score;
        auto toImg = [&](const cv::Point2f& p){
            return cv::Point2f((p.x - padx) / scale, (p.y - pady) / scale);
        };

        // (a) quad from minAreaRect (kept for the crop/deskew path)
        cv::RotatedRect rr = cv::minAreaRect(cnt);
        cv::Point2f pts[4]; rr.points(pts);
        for (int p = 0; p < 4; ++p) det.quad.push_back(toImg(pts[p]));

        // (b) TIGHT polygon: simplify the mask contour so it hugs the card
        //     outline but has few enough points to edit like a hand-drawn poly.
        double peri = cv::arcLength(cnt, true);
        std::vector<cv::Point> approx;
        cv::approxPolyDP(cnt, approx, 0.01 * peri, true);   // ~1% tolerance
        // guard: if simplification collapsed it, fall back to the quad
        const std::vector<cv::Point>* src = (approx.size() >= 4) ? &approx : nullptr;
        if (src) {
            for (const auto& p : *src)
                det.polygon.push_back(toImg(cv::Point2f((float)p.x, (float)p.y)));
        } else {
            det.polygon = det.quad;
        }

        // (c) centroid (image coords) for click hit-testing
        cv::Moments mm = cv::moments(cnt);
        if (mm.m00 > 0)
            det.centroid = toImg(cv::Point2f((float)(mm.m10/mm.m00), (float)(mm.m01/mm.m00)));
        else
            det.centroid = toImg(rr.center);

        out.push_back(std::move(det));
    }
    return out;
}
