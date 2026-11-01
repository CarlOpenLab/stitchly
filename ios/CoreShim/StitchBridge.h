// StitchBridge - Swift 可见的 Objective-C++ 封装（内部调用 C++ core）
#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

@interface LSOptionsObj : NSObject
@property (nonatomic, assign) int cropTop;
@property (nonatomic, assign) int cropBottom;
@property (nonatomic, assign) BOOL autoDetectChrome;
@end

@interface LSFrameInfoObj : NSObject
@property (nonatomic, assign) int32_t sourceIndex;
@property (nonatomic, assign) int32_t offsetY;   // 内容在输出中的起始 y；丢弃帧为 -1
@property (nonatomic, assign) float score;
@property (nonatomic, assign) BOOL dropped;
@property (nonatomic, assign) BOOL gap;
@end

@interface LSResultObj : NSObject
@property (nonatomic, strong) NSData *pixels;    // RGBA8888
@property (nonatomic, assign) int width;
@property (nonatomic, assign) int height;
@property (nonatomic, strong) NSArray<LSFrameInfoObj *> *frames;
@property (nonatomic, assign) BOOL reachedBottom;
@property (nonatomic, assign) int droppedCount;
@property (nonatomic, assign) int gapCount;
@end

// 合成阶段按需取帧：返回该帧的 RGBA 数据（一次性拼接用整帧数组）
typedef NSData * _Nullable (^LSFrameProviderBlock)(int32_t index);

@interface LSStitcher : NSObject

/// 一次性拼接（相册模式）：所有帧常驻内存，帧数据为 RGBA8888 且同尺寸
+ (nullable LSResultObj *)stitchImages:(NSArray<NSData *> *)frames
                                 width:(int)width
                                height:(int)height
                               options:(nullable LSOptionsObj *)options;

/// 流式拼接：创建计划（内部仅保留缩小灰度图），返回不透明句柄
+ (nullable void *)createPlanWithWidth:(int)width
                                height:(int)height
                               options:(nullable LSOptionsObj *)options;

+ (BOOL)addFrameRGBA:(NSData *)rgba toPlan:(void *)plan;

/// 最后一帧是否与前一帧几乎相同（滚动停止快速判定）
+ (BOOL)isLastFrameDuplicateInPlan:(void *)plan;

/// 对齐并合成；provider 按帧下标取回全分辨率 RGBA
+ (nullable LSResultObj *)composePlan:(void *)plan
                             provider:(LSFrameProviderBlock)provider;

+ (void)freePlan:(void *)plan;

@end

NS_ASSUME_NONNULL_END
