# WebRTC Bug 总结

## 1. 背景
- 设备端（眼镜）作为 Offerer，通过信令服务器向前端测试页面发送 SDP 和 ICE 候选。
- 前端页面 `test_webrtc_receiver.html` 被用作 Answerer，用于验证音频接收路径是否正常。
- 按照“谁发送谁建轨道”的原则，眼镜端负责创建音频轨道并发送 Opus 数据，前端只收流。

## 2. 问题现象
- 前端成功收到 Offer 和音频轨道，但在处理远端 ICE 候选时抛出浏览器异常：
  - `Failed to execute 'addIceCandidate' on 'RTCPeerConnection': Error processing ICE candidate`
  - `Failed to construct 'RTCIceCandidate': sdpMid and sdpMLineIndex are both null`
- 导致 ICE 状态最终降级到 `failed/disconnected`，音频链路无法建立，尽管数据通道可以正常通信。

## 3. 根因分析
- 眼镜端发送的 ICE 候选遵循 WebRTC SDP 格式：`a=candidate:...`。
- 前端虽然做了 `a=` 前缀裁剪，但在调用 `new RTCIceCandidate()` 时未提供 `sdpMid` 或 `sdpMLineIndex`。
- 根据 W3C 规范：在将 ICE 候选传给浏览器时，`sdpMid` 与 `sdpMLineIndex` 至少要填写一个，以告知浏览器该候选属于哪个 `m=`(媒体) 行；否则 Chrome/Edge 会直接抛出上述错误。

## 4. 修复方案
1. **在设置远程描述后解析 SDP**
   - 提取音频流的 `mid`（`a=mid:`）和 `m-line` 索引。
   - 若未找到则回退到默认值 `mid='0'`、`mLineIndex=0`。
2. **在构造 `RTCIceCandidate` 时写入解析结果**
   - 首选 `{ candidate, sdpMid, sdpMLineIndex }`。
   - 如果仍失败，回退为 `{ candidate, sdpMLineIndex }` 防止个别浏览器对 `mid` 的兼容问题。

```641:807:Smart_Glasses/Demo/Smart_Glasses_demo/test_webrtc_receiver.html
            peerConnection.setRemoteDescription(offer)
                .then(() => {
                    // ... existing code ...
                    parseSdpForAudioInfo(message.data.sdp);
                    processPendingIceCandidates();
                    // ... existing code ...
                });

        function parseSdpForAudioInfo(sdp) {
            // 从 SDP 中解析音频 mid 与 m-line
            // ... existing code ...
                        audioMid = currentMid;
                        audioMLineIndex = mLineIndex;
                        // ... log ...
        }

        function addIceCandidate(candidateStr) {
            // ... existing code ...
                const candidate = new RTCIceCandidate({
                    candidate: processedCandidate,
                    sdpMid: audioMid,
                    sdpMLineIndex: audioMLineIndex
                });
                peerConnection.addIceCandidate(candidate)
                    .catch((error) => {
                        // 回退仅使用 mLineIndex
                        const candidate2 = new RTCIceCandidate({
                            candidate: processedCandidate,
                            sdpMLineIndex: audioMLineIndex
                        });
                        peerConnection.addIceCandidate(candidate2);
                    });
            // ... existing code ...
        }
```

## 5. 验证结果
- 刷新前端页面后重新连接：
  - 日志显示 `从SDP解析音频信息: mid=0, mLineIndex=0`。
  - 所有远端 ICE 候选均添加成功，ICE 状态进入 `connected/completed`。
  - 音频播放正常，音频链路验证通过。

## 6. 后续建议
- 若后续加入视频轨道，需要扩展 SDP 解析逻辑以支持多条 `m=` 行并按 `mid` 精确匹配。
- 在信令层约定 ICE 消息结构，明确是否传递 `sdpMid`/`sdpMLineIndex` 字段，避免客户端重复解析。
- 建议在前端增加告警，当连续多次添加 ICE 失败时上报详细日志，便于远程排查。

