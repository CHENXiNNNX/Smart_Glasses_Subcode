/**
 * @file retinaface_anchors.hpp
 * @brief RetinaFace模型的Anchor定义
 * @details 包含640x640输入尺寸的16800个anchor定义
 */

#ifndef RETINAFACE_ANCHORS_HPP
#define RETINAFACE_ANCHORS_HPP

// RetinaFace 640x640 输入的anchor定义 (16800个anchor)
// 每个anchor包含4个值: [center_x, center_y, width, height] (归一化坐标)
extern const float BOX_PRIORS_640[16800][4];

#endif // RETINAFACE_ANCHORS_HPP
