# Stitchly 拾卷 — 跨平台长截图工具

> 任意应用，一卷到底。系统级长截图工具（Android + iOS），可截取**任意应用**的长页面。拼接算法用纯 C++ 实现一份共享核心，两端原生壳。

[![Platform](https://img.shields.io/badge/platform-Android%208%2B%20%7C%20iOS%2015%2B-4c4c4c)](#功能)
[![Core](https://img.shields.io/badge/core-C%2B%2B17%20·%20零依赖-8A2BE2)](core)
[![Tests](https://img.shields.io/badge/core%20tests-11%20passed-2ea44f)](core/tests)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![Pages](https://github.com/CarlOpenLab/stitchly/actions/workflows/pages.yml/badge.svg)](https://carlopenlab.github.io/stitchly/)
[![Landing](https://img.shields.io/badge/landing-page-b8431c)](https://carlopenlab.github.io/stitchly/)
[![GitHub](https://img.shields.io/badge/GitHub-CarlOpenLab%2Fstitchly-181717?logo=github)](https://github.com/CarlOpenLab/stitchly)

```bash
git clone https://github.com/CarlOpenLab/stitchly.git
```

## 功能

| 模式 | 说明 | 平台 |
|---|---|---|
| 实时抓帧拼接 | MediaProjection 录屏授权后，切到目标应用缓慢滚动，停止约 1 秒自动结束并拼接；可选"无障碍自动滚动"代为滚屏 | Android |
| 录屏抽帧拼接 | ReplayKit Broadcast 扩展实时录制，停止后自动抽帧去重拼接 | iOS |
| 相册截图拼接 | 连续截多张图 → 按顺序选入 → 自动检测重叠拼成长图 | 双端 |

## 目录结构

```
├── core/                # C++17 拼接核心（C API，无第三方依赖）
│   ├── include/longshot/stitch.h
│   ├── src/             # 对齐 / 固定区检测 / 去重 / 合成 / API
│   ├── tests/           # 合成滚动页面夹具 + 单元测试
│   └── CMakeLists.txt
├── android/             # Kotlin + Jetpack Compose
│   └── app/src/main/
│       ├── cpp/         # JNI 桥接 + 链接 core 源码
│       └── java/.../    # 采集服务 / 无障碍自动滚动 / 拼接 / UI
├── ios/                 # SwiftUI 主 App + Broadcast Upload 扩展（XcodeGen）
│   ├── project.yml
│   ├── App/             # 主 App（相册拼接 / 录屏抽帧 / 结果预览）
│   ├── BroadcastExtension/   # ReplayKit 扩展（H.264 流式写入 App Group）
│   └── CoreShim/        # Objective-C++ 桥接
└── docs/ARCHITECTURE.md # 架构与算法细节
```

## 构建

### 1. 核心（本仓库已在 Windows/msys2 验证）

```bash
# 直接编译运行测试（任一支持 C++17 的编译器）
g++ -std=c++17 -O2 -Icore/include core/src/*.cpp core/tests/test_stitch.cpp -o ls_test && ./ls_test

# 或用 CMake
cmake -S core -B build -DLS_BUILD_TESTS=ON && cmake --build build && ctest --test-dir build
```

### 2. Android（需 Android Studio + NDK）

1. 用 Android Studio 打开 `android/`（首次打开按提示同步 Gradle 8.7 / AGP 8.5.2 / JDK 17）
2. 等待 NDK/CMake 组件安装完成（SDK Manager）
3. 真机运行 `app`（minSdk 26，需真机验证录屏链路）

### 3. iOS（需 Mac + Xcode + XcodeGen）

```bash
brew install xcodegen
cd ios && xcodegen generate
open Stitchly.xcodeproj
```

1. Signing & Capabilities 中选择你的开发者团队（App 与扩展两个 target）
2. App Group（`group.com.stitchly.app`）会随 entitlements 自动配置
3. 真机运行 `Longshot`（iOS 15+，Broadcast 扩展必须真机验证）

## 真机验证清单

**Android**
- [ ] 授权录屏 → 悬浮"停止"按钮出现 → 切微信/微博缓慢滚动 → 停止后自动拼接
- [ ] 通知栏"停止"可用；未授予悬浮窗权限时不崩溃
- [ ] 滚动停止 ~1.2s 自动结束（连续 10 帧重复判定）
- [ ] 开启无障碍 + 自动滚动开关后，采集期间页面自动匀速上滑
- [ ] 相册拼接：选 3~10 张连续截图，出图顺序正确
- [ ] 保存到 `Pictures/Longshot`，分享可用

**iOS**
- [ ] 录屏选择器能列出「长截图录屏」（扩展注册成功）
- [ ] 录制中红点提示；停止后主 App 自动检测到视频并抽帧拼接
- [ ] 拼接结果无重影/缺行；"已到页底"角标在真正到底时出现
- [ ] 相册拼接、保存相册（仅添加权限）、分享

## 上架合规注意

- **Android 无障碍权限**：Play 审核对非无障碍用途严格。本项目仅用其发送滚动手势（服务声明不读取屏幕内容），上架前需在 Play Console 提交用途说明视频；不通过时可只保留"手动滚动抓帧"模式（默认即该模式）。
- **iOS Broadcast 扩展**：审核通常要求演示用途；扩展不得擅自读取屏幕外的数据（本实现仅写视频文件到 App Group）。
- 录屏授权说明文案（Android `createScreenCaptureIntent` 弹窗、iOS 系统直播确认）由系统提供，无需自备。

## 已知限制（v1）

- 周期性重复内容（长列表、商品卡片）可能对齐到错误周期，导致重复/跳行
- 滚动跨度过大（一次滑动超过一屏）时中间内容缺失，会在结果中标记"未对齐"次数
- 页底/暂停无法从画面上区分：滚动中途长时间停留也会被判定为"停止采集"
- iOS 扩展受约 50MB 内存限制，实现为 H.264 硬编码流式写文件；超长录屏（>5 分钟）未做上限保护
- 横屏 / 折叠屏展开中途 / 深色模式中途切换未做特殊处理

## 命名说明

品牌名 **Stitchly（拾卷）**，应用包名 / Bundle ID / App Group 已统一为：

- Android：`applicationId` / `namespace` = `com.stitchly.app`
- iOS：Bundle ID `com.stitchly.app`，Broadcast 扩展 `com.stitchly.app.broadcast`，App Group `group.com.stitchly.app`
- 拼接核心库的内部代号仍为 `longshot`（`liblongshot.so`、C API 前缀 `ls_`），仅工程内部可见，不影响品牌与上架，无需替换
