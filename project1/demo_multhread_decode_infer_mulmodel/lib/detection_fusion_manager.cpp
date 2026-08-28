/*
 * Copyright (c) 2025-04-01 HeXiaotian
 *
 * This source code is licensed for learning and research purposes only.
 * Commercial use, redistribution, resale, and creation of derivative works
 * are strictly prohibited without prior written permission from the author.
 */

#include "detection_fusion_manager.h"
#include <iostream>
#include <chrono>

DetectionFusionManager::DetectionFusionManager() {
    // 初始化类别名称映射
    class_names_[0] = "Person";
    class_names_[1] = "Helmet";
    class_names_[2] = "Tired";
    class_names_[3] = "Call/Play";
    
    // 初始化统计信息
    stats_ = FusionStats{};
}

DetectionFusionManager::~DetectionFusionManager() {
}

void DetectionFusionManager::setConfig(const FusionConfig& config) {
    config_ = config;
}

std::vector<FusedDetection> DetectionFusionManager::fuseDetections(
    const detect_result_group_t& person_results,
    const detect_result_group_t& helmet_results,
    const detect_result_group_t& tired_results,
    const detect_result_group_t& callplay_results) {
    
    auto start_time = std::chrono::high_resolution_clock::now();
    
    std::vector<FusedDetection> fused_detections;
    std::vector<std::vector<FusedDetection>> model_detections(4);
    
    // 1. 转换各模型的检测结果
    // 人员检测结果
    for (int i = 0; i < person_results.count; i++) {
        const auto& det = person_results.results[i];
        FusedDetection fused;
        fused.class_id = 0;
        fused.class_name = "Person";
        fused.bbox = cv::Rect2f(det.box.left, det.box.top, 
                               det.box.right - det.box.left, 
                               det.box.bottom - det.box.top);
        fused.confidence = det.prop;
        fused.source_models = {0};
        fused.model_confidences = {det.prop};
        fused.timestamp = std::chrono::system_clock::now();
        model_detections[0].push_back(fused);
    }
    
    // 安全帽检测结果
    for (int i = 0; i < helmet_results.count; i++) {
        const auto& det = helmet_results.results[i];
        FusedDetection fused;
        fused.class_id = 1;
        fused.class_name = "Helmet";
        fused.bbox = cv::Rect2f(det.box.left, det.box.top, 
                               det.box.right - det.box.left, 
                               det.box.bottom - det.box.top);
        fused.confidence = det.prop;
        fused.source_models = {1};
        fused.model_confidences = {det.prop};
        fused.timestamp = std::chrono::system_clock::now();
        model_detections[1].push_back(fused);
    }
    
    // 疲劳检测结果
    for (int i = 0; i < tired_results.count; i++) {
        const auto& det = tired_results.results[i];
        FusedDetection fused;
        fused.class_id = 2;
        fused.class_name = "Tired";
        fused.bbox = cv::Rect2f(det.box.left, det.box.top, 
                               det.box.right - det.box.left, 
                               det.box.bottom - det.box.top);
        fused.confidence = det.prop;
        fused.source_models = {2};
        fused.model_confidences = {det.prop};
        fused.timestamp = std::chrono::system_clock::now();
        model_detections[2].push_back(fused);
    }
    
    // 通话检测结果
    for (int i = 0; i < callplay_results.count; i++) {
        const auto& det = callplay_results.results[i];
        FusedDetection fused;
        fused.class_id = 3;
        fused.class_name = "Call/Play";
        fused.bbox = cv::Rect2f(det.box.left, det.box.top, 
                               det.box.right - det.box.left, 
                               det.box.bottom - det.box.top);
        fused.confidence = det.prop;
        fused.source_models = {3};
        fused.model_confidences = {det.prop};
        fused.timestamp = std::chrono::system_clock::now();
        model_detections[3].push_back(fused);
    }
    
    // 2. 跨模型融合 - 寻找重叠的检测框
    // used[model][det_idx] 标记该检测是否已参与融合（按检测框而非按模型）
    std::vector<std::vector<bool>> used(4);
    for (int i = 0; i < 4; i++) {
        used[i].resize(model_detections[i].size(), false);
    }

    for (int model1 = 0; model1 < 4; model1++) {
        for (int model2 = model1 + 1; model2 < 4; model2++) {
            for (size_t idx1 = 0; idx1 < model_detections[model1].size(); idx1++) {
                if (used[model1][idx1]) continue;

                const auto& det1 = model_detections[model1][idx1];
                for (size_t idx2 = 0; idx2 < model_detections[model2].size(); idx2++) {
                    if (used[model2][idx2]) continue;

                    const auto& det2 = model_detections[model2][idx2];
                    // Person(0)+Helmet(1) 嵌套：全身框+头帽框，用 IoM 判断
                    bool should_fuse = false;
                    if (model1 == 0 && model2 == 1) {
                        float iom = calculateIoM(det1.bbox, det2.bbox);
                        should_fuse = (iom >= config_.iom_threshold);
                    } else {
                        float iou = calculateIoU(det1.bbox, det2.bbox);
                        should_fuse = (iou > config_.iou_threshold);
                    }
                    if (should_fuse) {
                        // 融合两个检测结果
                        FusedDetection fused;
                        fused.class_id = det1.class_id;
                        fused.class_name = det1.class_name;

                        std::vector<cv::Rect2f> boxes = {det1.bbox, det2.bbox};
                        std::vector<float> weights = {
                            config_.model_weights[model1],
                            config_.model_weights[model2]
                        };
                        fused.bbox = weightedFusion(boxes, weights);

                        std::vector<float> confidences = {det1.confidence, det2.confidence};
                        fused.confidence = confidenceFusion(confidences, weights);

                        fused.source_models = {model1, model2};
                        fused.model_confidences = {det1.confidence, det2.confidence};
                        fused.timestamp = std::chrono::system_clock::now();

                        fused_detections.push_back(fused);
                        used[model1][idx1] = true;
                        used[model2][idx2] = true;
                        break;
                    }
                }
            }
        }
    }

    // 3. 添加未融合的检测结果
    for (int i = 0; i < 4; i++) {
        for (size_t j = 0; j < model_detections[i].size(); j++) {
            if (!used[i][j] && model_detections[i][j].confidence > config_.confidence_threshold) {
                fused_detections.push_back(model_detections[i][j]);
            }
        }
    }
    
    // 4. 应用非极大值抑制
    if (config_.enable_nms) {
        std::vector<int> keep_indices = applyNMS(fused_detections);
        std::vector<FusedDetection> nms_detections;
        for (int idx : keep_indices) {
            nms_detections.push_back(fused_detections[idx]);
        }
        fused_detections = nms_detections;
    }
    
    // 5. 更新统计信息
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
    
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.total_detections = person_results.count + helmet_results.count + 
                                 tired_results.count + callplay_results.count;
        stats_.fused_detections = fused_detections.size();
        stats_.fusion_time_ms = duration.count() / 1000.0;
    }
    
    return fused_detections;
}

void DetectionFusionManager::drawFusedDetections(cv::Mat& frame, 
                                                const std::vector<FusedDetection>& detections) {
    for (const auto& detection : detections) {
        // 根据类别选择颜色
        cv::Scalar color;
        int thickness = 2;
        
        switch (detection.class_id) {
            case 0: // Person
                color = cv::Scalar(0, 255, 0); // 绿色
                thickness = 3;
                break;
            case 1: // Helmet
                color = cv::Scalar(0, 0, 255); // 红色
                thickness = 2;
                break;
            case 2: // Tired
                color = cv::Scalar(0, 255, 255); // 黄色
                thickness = 2;
                break;
            case 3: // Call/Play
                color = cv::Scalar(255, 0, 0); // 蓝色
                thickness = 2;
                break;
            default:
                color = cv::Scalar(128, 128, 128); // 灰色
                thickness = 1;
                break;
        }
        
        // 绘制边界框
        cv::Rect rect(detection.bbox.x, detection.bbox.y, 
                     detection.bbox.width, detection.bbox.height);
        cv::rectangle(frame, rect, color, thickness);
        
        // 绘制置信度条
        int bar_width = detection.bbox.width;
        int bar_height = 4;
        cv::Rect confidence_bar(detection.bbox.x, detection.bbox.y + detection.bbox.height,
                               bar_width * detection.confidence, bar_height);
        cv::rectangle(frame, confidence_bar, color, -1);
    }
}

float DetectionFusionManager::calculateIoU(const cv::Rect2f& box1, const cv::Rect2f& box2) {
    float x1 = std::max(box1.x, box2.x);
    float y1 = std::max(box1.y, box2.y);
    float x2 = std::min(box1.x + box1.width, box2.x + box2.width);
    float y2 = std::min(box1.y + box1.height, box2.y + box2.height);
    
    if (x2 <= x1 || y2 <= y1) {
        return 0.0f;
    }
    
    float intersection = (x2 - x1) * (y2 - y1);
    float area1 = box1.width * box1.height;
    float area2 = box2.width * box2.height;
    float union_area = area1 + area2 - intersection;
    
    return intersection / union_area;
}

// Person(全身大框) + Helmet(头部小框) 等嵌套场景：小框与大框 IoU 低，用 IoM 判断
// IoM = intersection / min(area1, area2)，小框大半在大框内则融合
float DetectionFusionManager::calculateIoM(const cv::Rect2f& box1, const cv::Rect2f& box2) {
    float x1 = std::max(box1.x, box2.x);
    float y1 = std::max(box1.y, box2.y);
    float x2 = std::min(box1.x + box1.width, box2.x + box2.width);
    float y2 = std::min(box1.y + box1.height, box2.y + box2.height);
    
    if (x2 <= x1 || y2 <= y1) {
        return 0.0f;
    }
    
    float intersection = (x2 - x1) * (y2 - y1);
    float area1 = box1.width * box1.height;
    float area2 = box2.width * box2.height;
    float min_area = std::min(area1, area2);
    
    return (min_area > 0) ? (intersection / min_area) : 0.0f;
}

cv::Rect2f DetectionFusionManager::weightedFusion(const std::vector<cv::Rect2f>& boxes,
                                                 const std::vector<float>& weights) {
    if (boxes.empty()) return cv::Rect2f();
    
    float total_weight = 0.0f;
    float weighted_x = 0.0f, weighted_y = 0.0f;
    float weighted_width = 0.0f, weighted_height = 0.0f;
    
    for (size_t i = 0; i < boxes.size(); i++) {
        float weight = weights[i];
        total_weight += weight;
        
        weighted_x += boxes[i].x * weight;
        weighted_y += boxes[i].y * weight;
        weighted_width += boxes[i].width * weight;
        weighted_height += boxes[i].height * weight;
    }
    
    return cv::Rect2f(weighted_x / total_weight,
                     weighted_y / total_weight,
                     weighted_width / total_weight,
                     weighted_height / total_weight);
}

float DetectionFusionManager::confidenceFusion(const std::vector<float>& confidences,
                                              const std::vector<float>& weights) {
    if (confidences.empty()) return 0.0f;
    
    float total_weight = 0.0f;
    float weighted_confidence = 0.0f;
    
    for (size_t i = 0; i < confidences.size(); i++) {
        float weight = weights[i];
        total_weight += weight;
        weighted_confidence += confidences[i] * weight;
    }
    
    return weighted_confidence / total_weight;
}

std::vector<int> DetectionFusionManager::applyNMS(const std::vector<FusedDetection>& detections) {
    if (detections.empty()) return {};
    
    // 按置信度排序
    std::vector<std::pair<float, int>> confidences_with_indices;
    for (size_t i = 0; i < detections.size(); i++) {
        confidences_with_indices.push_back({detections[i].confidence, i});
    }
    
    std::sort(confidences_with_indices.begin(), confidences_with_indices.end(),
              [](const std::pair<float, int>& a, const std::pair<float, int>& b) {
                  return a.first > b.first;
              });
    
    std::vector<int> keep_indices;
    std::vector<bool> suppressed(detections.size(), false);
    
    for (const auto& conf_idx : confidences_with_indices) {
        int idx = conf_idx.second;
        if (suppressed[idx]) continue;
        
        keep_indices.push_back(idx);
        
        // 抑制与当前检测框IoU过高的其他检测框
        for (size_t i = 0; i < detections.size(); i++) {
            if (i == idx || suppressed[i]) continue;
            
            float iou = calculateIoU(detections[idx].bbox, detections[i].bbox);
            if (iou > config_.iou_threshold) {
                suppressed[i] = true;
            }
        }
    }
    
    return keep_indices;
}

DetectionFusionManager::FusionStats DetectionFusionManager::getStats() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return stats_;
}

