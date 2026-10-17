#import <Foundation/Foundation.h>
#import <ImageIO/ImageIO.h>
#import <CoreGraphics/CoreGraphics.h>
#include "png_io.h"
#include <cstring>
namespace ar {
bool png_write(const std::string &path, const uint8_t *rgba, uint32_t w, uint32_t h) {
    CGColorSpaceRef cs = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CGDataProviderRef dp = CGDataProviderCreateWithData(nullptr, rgba, (size_t)w * h * 4, nullptr);
    CGImageRef img = CGImageCreate(w, h, 8, 32, (size_t)w * 4, cs, (CGBitmapInfo)kCGImageAlphaLast, dp, nullptr, false, kCGRenderingIntentDefault);
    NSURL *url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:path.c_str()]];
    CGImageDestinationRef dst = CGImageDestinationCreateWithURL((__bridge CFURLRef)url, CFSTR("public.png"), 1, nullptr);
    bool ok = false;
    if (dst && img) { CGImageDestinationAddImage(dst, img, nullptr); ok = CGImageDestinationFinalize(dst); }
    if (dst) CFRelease(dst);
    if (img) CGImageRelease(img);
    CGDataProviderRelease(dp);
    CGColorSpaceRelease(cs);
    return ok;
}
bool png_read(const std::string &path, std::vector<uint8_t> &rgba, uint32_t &w, uint32_t &h) {
    NSURL *url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:path.c_str()]];
    CGImageSourceRef src = CGImageSourceCreateWithURL((__bridge CFURLRef)url, nullptr);
    if (!src) return false;
    CGImageRef img = CGImageSourceCreateImageAtIndex(src, 0, nullptr);
    CFRelease(src);
    if (!img) return false;
    w = (uint32_t)CGImageGetWidth(img); h = (uint32_t)CGImageGetHeight(img);
    size_t bpp = CGImageGetBitsPerPixel(img), bpr = CGImageGetBytesPerRow(img);
    CFDataRef data = CGDataProviderCopyData(CGImageGetDataProvider(img));
    bool ok = data != nullptr && CGImageGetBitsPerComponent(img) == 8 && (bpp == 32 || bpp == 24);
    if (ok) {
        rgba.assign((size_t)w * h * 4, 255);
        const uint8_t *p = CFDataGetBytePtr(data);
        for (uint32_t y = 0; y < h; y++)
            for (uint32_t x = 0; x < w; x++)
                std::memcpy(&rgba[((size_t)y * w + x) * 4], p + y * bpr + x * (bpp / 8), bpp / 8);
    }
    if (data) CFRelease(data);
    CGImageRelease(img);
    return ok;
}
}
