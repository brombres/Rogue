uint8_t* RogueMacOS_decodePNGToARGB(NSData *data, size_t &out_width, size_t &out_height)
{
  // Create CGImageSource from NSData
  CGImageSourceRef source = CGImageSourceCreateWithData((CFDataRef)data, NULL);
  if (!source) return nullptr;

  CGImageRef image = CGImageSourceCreateImageAtIndex(source, 0, NULL);
  CFRelease(source);
  if (!image) return nullptr;

  out_width = CGImageGetWidth(image);
  out_height = CGImageGetHeight(image);

  size_t width = out_width;
  size_t height = out_height;

  // Allocate buffer for ARGB pixels (4 bytes per pixel)
  size_t bytesPerPixel = 4;
  size_t bytesPerRow = width * bytesPerPixel;
  uint8_t* pixelData = (uint8_t*)malloc(height * bytesPerRow);
  if (!pixelData) {
    CGImageRelease(image);
    return nullptr;
  }

  // Create a CGColorSpace
  CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();

  // Create a bitmap context with non-premultiplied alpha, ARGB format
  CGBitmapInfo bitmapInfo = kCGImageAlphaFirst | kCGBitmapByteOrder32Host; // ARGB straight alpha

  CGContextRef context = CGBitmapContextCreate(
      pixelData,
      width,
      height,
      8,              // bits per component
      bytesPerRow,
      colorSpace,
      bitmapInfo
      );

  CGColorSpaceRelease(colorSpace);

  if (!context) {
    free(pixelData);
    CGImageRelease(image);
    return nullptr;
  }

  // Draw the image into the context to get pixel data in our format
  CGRect rect = CGRectMake(0, 0, width, height);
  CGContextDrawImage(context, rect, image);

  CGContextRelease(context);
  CGImageRelease(image);

  // Now pixelData contains ARGB straight alpha pixel bytes
  return pixelData;
}
