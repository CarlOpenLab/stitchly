// StitchBridge 实现：Objective-C++ 封装 C++ core
#import "StitchBridge.h"

#include <vector>

#include "longshot/stitch.h"

static LsOptions LsOptionsFromObj(LSOptionsObj *o) {
    LsOptions opt;
    ls_options_init(&opt);
    if (o) {
        opt.crop_top = o.cropTop;
        opt.crop_bottom = o.cropBottom;
        opt.auto_detect_chrome = o.autoDetectChrome ? 1 : 0;
    }
    return opt;
}

static LSFrameInfoObj *FrameInfoFromCore(const LsFrameInfo &f) {
    LSFrameInfoObj *obj = [[LSFrameInfoObj alloc] init];
    obj.sourceIndex = f.source_index;
    obj.offsetY = f.offset_y;
    obj.score = f.score;
    obj.dropped = f.dropped != 0;
    obj.gap = f.gap != 0;
    return obj;
}

static LSResultObj *ResultFromCore(const LsResult *r) {
    LSResultObj *res = [[LSResultObj alloc] init];
    res.pixels = [NSData dataWithBytes:r->data
                                length:(NSUInteger)r->width * r->height * 4];
    res.width = r->width;
    res.height = r->height;
    res.reachedBottom = r->reached_bottom != 0;
    res.droppedCount = r->dropped_count;
    res.gapCount = r->gap_count;
    NSMutableArray<LSFrameInfoObj *> *frames =
        [NSMutableArray arrayWithCapacity:(NSUInteger)r->frame_count];
    for (int i = 0; i < r->frame_count; i++)
        [frames addObject:FrameInfoFromCore(r->frames[i])];
    res.frames = frames;
    return res;
}

// 合成回调的上下文：持有 provider block（ARC strong 保活）
@interface LSProviderBox : NSObject
@property (nonatomic, copy) LSFrameProviderBlock block;
@property (nonatomic, assign) int width;
@property (nonatomic, assign) int height;
@end
@implementation LSProviderBox
@end

static int32_t ProviderTrampoline(int32_t index, LsImage *out, void *user) {
    LSProviderBox *box = (__bridge LSProviderBox *)user;
    NSData *d = box.block(index);
    if (!d) return -1;
    const NSUInteger need = (NSUInteger)box.width * box.height * 4;
    if ((NSUInteger)d.length != need) return -1;
    // provider 数据仅回调期间有效，复制到稳定缓冲供 core 读取
    static thread_local std::vector<uint8_t> buf;
    buf.resize(need);
    memcpy(buf.data(), d.bytes, need);
    out->data = buf.data();
    out->width = box.width;
    out->height = box.height;
    out->stride = 0;
    return LS_OK;
}

@implementation LSStitcher

+ (nullable LSResultObj *)stitchImages:(NSArray<NSData *> *)frames
                                 width:(int)width
                                height:(int)height
                               options:(nullable LSOptionsObj *)options {
    if (frames.count < 1 || width < 16 || height < 16) return nil;
    const NSUInteger need = (NSUInteger)width * height * 4;
    std::vector<LsImage> imgs(frames.count);
    for (NSUInteger i = 0; i < frames.count; i++) {
        NSData *d = frames[i];
        if ((NSUInteger)d.length != need) return nil;
        imgs[i].data = (const uint8_t *)d.bytes;
        imgs[i].width = width;
        imgs[i].height = height;
        imgs[i].stride = 0;
    }
    LsOptions opt = LsOptionsFromObj(options);
    LsResult *r = nullptr;
    if (ls_stitch_images(imgs.data(), (int32_t)frames.count, &opt, &r) != LS_OK)
        return nil;
    LSResultObj *res = ResultFromCore(r);
    ls_result_free(r);
    return res;
}

+ (nullable void *)createPlanWithWidth:(int)width
                                height:(int)height
                               options:(nullable LSOptionsObj *)options {
    if (width < 16 || height < 16) return NULL;
    LsOptions opt = LsOptionsFromObj(options);
    return ls_plan_create(width, height, &opt);
}

+ (BOOL)addFrameRGBA:(NSData *)rgba toPlan:(void *)plan {
    auto *p = (LsPlan *)plan;
    if (!p || !rgba) return NO;
    const NSUInteger need = (NSUInteger)p->full_w * p->full_h * 4;
    if ((NSUInteger)rgba.length != need) return NO;
    LsImage img{(const uint8_t *)rgba.bytes, p->full_w, p->full_h, 0};
    return ls_plan_add_frame(p, &img) == LS_OK;
}

+ (BOOL)isLastFrameDuplicateInPlan:(void *)plan {
    auto *p = (LsPlan *)plan;
    return p && ls_plan_last_frame_duplicate(p) != 0;
}

+ (nullable LSResultObj *)composePlan:(void *)plan
                             provider:(LSFrameProviderBlock)provider {
    auto *p = (LsPlan *)plan;
    if (!p || !provider) return nil;
    LSProviderBox *box = [[LSProviderBox alloc] init];
    box.block = provider;
    box.width = p->full_w;
    box.height = p->full_h;
    LsResult *r = nullptr;
    const int32_t rc = ls_plan_compose(p, ProviderTrampoline, (__bridge void *)box, &r);
    if (rc != LS_OK || !r) return nil;
    LSResultObj *res = ResultFromCore(r);
    ls_result_free(r);
    return res;
}

+ (void)freePlan:(void *)plan {
    delete (LsPlan *)plan;
}

@end
