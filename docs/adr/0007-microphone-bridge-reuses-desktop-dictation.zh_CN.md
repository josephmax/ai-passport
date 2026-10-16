[English](0007-microphone-bridge-reuses-desktop-dictation.md) · **简体中文**

# 0007 — 提供麦克风流，复用桌面听写，不自行开发输入法

- 日期：2026-10-14
- 状态：2026-10-15 已实现 BLE／macOS 音频桥接；Typeless 验收待完成
- 取代：[2026-09-26 语音报告](../specs/2026-09-26-voice-input-feasibility.zh_CN.md) 中 Wi-Fi WebSocket、内置 whisper.cpp／FunASR 推理、文字／按键注入的方案

挂坠的角色是麦克风，供用户已有的语音输入软件使用。通过 [ADR 0006](0006-ble-companion-without-device-networking.zh_CN.md) 选定的 BLE 连接传输独立分帧的 ADPCM。macOS 伴侣接收并缓冲音频，经系统虚拟麦克风提供给 Typeless，由它负责识别、语义整理和文字输入。原型采用单独安装的 BlackHole 2ch 回环设备，不先自研音频驱动。第一版使用电脑端听写快捷键，设备触发快捷键延后。

这推翻了包含模型管理、语音识别、剪贴板处理、焦点跟踪与导航按键注入的过大 MVP。固件不保存录音；有界 RAM 队列只是传输缓冲，不是整句录音档案。桥接程序无需 ASR／LLM 模型或推理 API Key。音频端点不绑定 Typeless，其他使用麦克风的软件也能接入。

Typeless 是云端服务，不是本项目提供的本地模型。它会将音频和有限上下文送到云端处理；零保留／本地历史不代表离线运行。订阅和电脑联网是独立前提，费用记录在[更新后的语音报告](../specs/2026-09-26-voice-input-feasibility.zh_CN.md)。若以后要求离线听写，可另立范围，通过同一音频源接入本地 ASR 及可选文字整理适配器。

这个边界的代价是安装和维护桌面桥接程序及虚拟音频设备，并接受第三方听写依赖。须验证虚拟设备兼容性、静音行为、首字采集、音质、延迟及云端可达性。将 BlackHole 嵌入或随产品分发前须解决其 GPLv3／商业许可；原型单独安装不代表已决定打包其代码。

来源核对于 2026-10-14：[Typeless 麦克风选择](https://www.typeless.com/help/quickstart/settings)、[Typeless 云端处理](https://www.typeless.com/data-controls)、[BlackHole 路由及许可](https://github.com/ExistentialAudio/BlackHole)。

## 2026-10-15 传输修订

优先实现独立 IMA ADPCM 块，每块 320 个 16 kHz 采样、172 字节、20 ms，Mac 解码回 PCM。推翻原始 PCM 优先方案是为了降低带宽并实现单通知一帧，并非已实测 PCM 失败。音效和采集统一由一个音频任务拥有；Mac 有界抖动队列送至单独安装的 BlackHole，仍由现有 Typeless 完成识别／AI 整理／输入。没有复制遥控器项目的 GPL 源码，没有内置 Whisper。
