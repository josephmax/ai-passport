[English](0006-ble-companion-without-device-networking.md) · **简体中文**

# 0006 — 使用 BLE 电脑伴侣，不给挂坠配网

- 日期：2026-10-14
- 状态：2026-10-15 已实现固件与 macOS 伴侣；硬件验收待完成
- 取代：[ADR 0001](0001-usage-proxy-holds-credentials.zh_CN.md) 中的设备传输、每小时拉取，以及否决 BLE 伴侣的结论；[ADR 0004](0004-asset-pipeline-server-side-transcode.zh_CN.md) 中的 HTTPS 下发前提
- 实施契约：[BLE 伴侣 MVP](../specs/2026-10-14-ble-companion-mvp.zh_CN.md)

办公网络可能拒绝非信任设备，而用户的电脑可以运行伴侣程序并使用蓝牙和 USB。采用一条经过身份认证的 BLE 连接传输用量快照、设置、设备状态和麦克风音频。挂坠是 BLE 外设，电脑是中心设备，负责采集、联网和配置。目标流程不再要求输入 SSID／密码／服务 IP，不使用 SoftAP 接入，也不让设备运行 HTTP 客户端。USB 保留为开发／调试通道，不是用户使用的前置步骤。

此前因仪表盘依赖附近电脑而否决 BLE，现在接受这个边界：断开时保留最后快照和本地番茄／经验功能。原局域网方案同样依赖一个可达且正在运行的服务，并未保证离开服务后仍可自主取得新数据。BLE 直连解决了真实的办公网络准入限制，也能用同一连接承载麦克风传输。

厂商凭证仍留在主机；现有快照结构、本地采集器、素材转码、番茄／经验持久化均可复用。第一增量保留内置／当前素材；BLE 素材传输延后，实施时仍须保留 APB1、分块写入、校验和原子安装。现有配置中心及桥接程序可以继续通过本机回环 HTTP 访问服务，但 HTTP 不再是挂坠协议。

## 2026-10-15 身份与多电脑修订

根据用户要求，最多记住四个认证电脑关联，同时只选中一个数据源。挂坠使用出厂 eFuse MAC，服务数据目录使用独立 UUID；电脑 UUID 必须匹配蓝牙认证身份。新关联和数据源切换都需要挂坠确认，快照按来源替换而不跨电脑累加。推翻原单绑定对假设；具体协议与回退点见实施契约。

## 替代方案与影响

- SoftAP 或共同热点可保留更多 HTTP 代码，但引入切网或共同网络前提。Espressif 当前协议栈不支持标准 Wi-Fi Direct。这些不再是已接受的接入路径。
- 普通电脑使用 ESP-NOW 通常需增加 ESP 接收器／适配器；MVP 不增加硬件，因此延后。
- 打洞首先要求联网，还需公网协调／中继设施，无法帮助不能入网的挂坠；远程连接延后。
- 固定 USB 串口接口可配合桥接程序传音频，但需要线缆。ESP32-C3 不支持原生 USB Audio 枚举。
- 新数据和麦克风功能依赖附近正在运行的伴侣。BLE 吞吐、主机兼容性、RAM、无线干扰和续航仍是真机验收关卡，本决策不承诺吞吐或续航。
- 实施时只移除本应用的配网流程，保留上游 Wi-Fi／BLE 示例与硬件文档。传输迁移不得擦除 NVS 或重写番茄／经验／素材数据。

主要限制来源：[Wi-Fi Direct FAQ](https://docs.espressif.com/projects/esp-faq/en/latest/software-framework/wifi.html#do-espressif-chips-support-wi-fi-direct-wi-fi-p2p)、[USB Serial/JTAG](https://docs.espressif.com/projects/esp-idf/en/v5.5.3/esp32c3/api-guides/usb-serial-jtag-console.html)。
