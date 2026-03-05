/* retinaface_anchors.hpp - RetinaFace Anchor定义 */

#pragma once

// RetinaFace 640x640 输入的anchor定义 (16800个anchor)
// 每个anchor包含4个值: [center_x, center_y, width, height] (归一化坐标)
extern const float BOX_PRIORS_640[16800][4];
