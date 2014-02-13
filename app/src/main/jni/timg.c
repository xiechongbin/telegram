/*
 * Copyright (C) 2009 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */
#include <jni.h>
#include <time.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <android/log.h>
#include <android/bitmap.h>
#include "log.h"
#include "libjpeg/jpeglib.h"
#include "libjpeg/jerror.h"
#include "aes/aes.h"

#define  MIN(a,b) (a < b ? a : b)
#define  MAX(a,b) (a > b ? a : b)
#define  ABS(a) (a > 0 ? a : -a)
#define  R(c) ((c & 0x000000ff) >> 0)
#define  G(c) ((c & 0x0000ff00) >> 8)
#define  B(c) ((c & 0x00ff0000) >> 16)
#define  A(c) ((c & 0xff000000) >> 24)
#define  ARGB(a,r,g,b) (a << 24 | (b << 16) | (g << 8) | r)

#define  SIZE 90
#define  SIZE_FULL 8100

/* Read JPEG image from a memory segment */
static void init_source (j_decompress_ptr cinfo) {}
static boolean fill_input_buffer (j_decompress_ptr cinfo)
{
    ERREXIT(cinfo, JERR_INPUT_EMPTY);
return TRUE;
}
static void skip_input_data (j_decompress_ptr cinfo, long num_bytes)
{
    struct jpeg_source_mgr* src = (struct jpeg_source_mgr*) cinfo->src;

    if (num_bytes > 0) {
        src->next_input_byte += (size_t) num_bytes;
        src->bytes_in_buffer -= (size_t) num_bytes;
    }
}
static void term_source (j_decompress_ptr cinfo) {}
static void jpeg_mem_src (j_decompress_ptr cinfo, void* buffer, long nbytes)
{
    struct jpeg_source_mgr* src;

    if (cinfo->src == NULL) {   /* first time for this JPEG object? */
        cinfo->src = (struct jpeg_source_mgr *)
            (*cinfo->mem->alloc_small) ((j_common_ptr) cinfo, JPOOL_PERMANENT,
            sizeof(struct jpeg_source_mgr));
    }

    src = (struct jpeg_source_mgr*) cinfo->src;
    src->init_source = init_source;
    src->fill_input_buffer = fill_input_buffer;
    src->skip_input_data = skip_input_data;
    src->resync_to_restart = jpeg_resync_to_restart; /* use default method */
    src->term_source = term_source;
    src->bytes_in_buffer = nbytes;
    src->next_input_byte = (JOCTET*)buffer;
}

static int rgb_clamp(int value) {
  if(value > 255) {
    return 255;
  }
  if(value < 0) {
    return 0;
  }
  return value;
}

static void fastBlur(AndroidBitmapInfo* info, void* pixels) {
  LOGI("Fast bluring image w=%d, h=%d, s=%d", info->width, info->height, info->stride);

  uint32_t* pix = (uint32_t*)pixels;
  int w = info->width;
  int h = info->height;
  const int radius = 3;
  const int div = radius * 2 + 1;

  uint32_t* rgb = (uint32_t *)malloc(w * h * sizeof(uint32_t));

  uint32_t rgbsum;
  int x, y, i;
  int* vmin = (int *)malloc(MAX(w, h) * sizeof(int));

  uint32_t stack[div];
  int stackpointer, stackstart;
  uint32_t rgboutsum, rgbinsum;

  for (x = 0; x < w - radius - 1; x++) {
    vmin[x] = x + radius + 1;
  }
  for (x = w - radius - 1; x < w; x++) {
    vmin[x] = w - 1;
  }

  int yw = 0;
  for (y = 0; y < h; y++) {
    rgbinsum = rgboutsum = rgbsum = 0;
    for (i = -radius; i <= radius; i++) {
      uint32_t p = pix[yw + MIN(w - 1, MAX(i, 0))];
      stack[i + radius] = ((p & 0x0000FC) >> 2) +
                           (p & 0x00FC00) +
                          ((p & 0xFC0000) << 2);

      rgbsum += stack[i + radius] * (radius + 1 - ABS(i));

      if (i > 0) {
        rgbinsum += stack[i + radius];
      } else {
        rgboutsum += stack[i + radius];
      }
    }
    stackstart = 0;
    stackpointer = radius + 1;

    for (x = 0; x < w; x++) {
      // rgb[yw + x] = (rgbsum >> 4) & 0x03F0FC3F;
      rgb[yw + x] = ((rgbsum + 0x00B02C0B) >> 4) & 0x03F0FC3F;

      rgbsum -= rgboutsum;

      rgboutsum -= stack[stackstart];

      uint32_t p = pix[yw + vmin[x]];

      stack[stackstart] = ((p & 0x0000FC) >> 2) +
                           (p & 0x00FC00) +
                          ((p & 0xFC0000) << 2);

      rgbinsum += stack[stackstart];

      rgbsum += rgbinsum;

      rgboutsum += stack[stackpointer];

      rgbinsum -= stack[stackpointer];

      stackstart++;
      if (stackstart == div) {
        stackstart = 0;
      }

      stackpointer++;
      if (stackpointer == div) {
        stackpointer = 0;
      }
    }
    yw += w;
  }

  for (y = 0; y < h - radius - 1; y++) {
    vmin[y] = (y + radius + 1) * w;
  }
  for (y = h - radius - 1; y < h; y++) {
    vmin[y] = (h - 1) * w;
  }

  for (x = 0; x < w; x++) {
    rgbinsum = rgboutsum = rgbsum = 0;
    for (i = -radius; i <= radius; i++) {
      stack[i + radius] = rgb[MIN(h - 1, MAX(0, i)) * w + x];

      rgbsum += stack[i + radius] * (radius + 1 - ABS(i));

      if (i > 0) {
        rgbinsum += stack[i + radius];
      } else {
        rgboutsum += stack[i + radius];
      }
    }
    stackstart = 0;
    stackpointer = radius + 1;

    int yi = x;
    for (y = 0; y < h; y++) {
      pix[yi] = (pix[yi] & 0xFF000000) + ((rgbsum & 0x3FC00000) >> 6) + ((rgbsum & 0x000FF000) >> 4) + ((rgbsum & 0x000003FC) >> 2);

      rgbsum -= rgboutsum;
      rgboutsum -= stack[stackstart];

      stack[stackstart] = rgb[x + vmin[y]];

      rgbinsum += stack[stackstart];

      rgbsum += rgbinsum;

      rgboutsum += stack[stackpointer];

      rgbinsum -= stack[stackpointer];

      stackstart++;
      if (stackstart == div) {
        stackstart = 0;
      }

      stackpointer++;
      if (stackpointer == div) {
        stackpointer = 0;
      }

      yi += w;
    }
  }

  free (rgb);
  free (vmin);
}


static void brightness(AndroidBitmapInfo* info, void* pixels, float brightnessValue){
	int xx, yy, red, green, blue;
	uint32_t* line;

	for(yy = 0; yy < info->height; yy++){
		line = (uint32_t*)pixels;
		for(xx =0; xx < info->width; xx++){
			//extract the RGB values from the pixel
			red = R(line[xx]);
			green = G(line[xx]);
			blue = B(line[xx]);

            //manipulate each value
            red = rgb_clamp((int)(red * brightnessValue + (1-brightnessValue)*255));
            green = rgb_clamp((int)(green * brightnessValue+ (1-brightnessValue)*255));
            blue = rgb_clamp((int)(blue * brightnessValue+ (1-brightnessValue)*255));

            // set the new pixel back in
            line[xx] = ARGB(A(line[xx]), red, green, blue);
		}
		pixels = (char*)pixels + info->stride;
	}
}

JNIEXPORT void Java_org_telegram_android_util_ImageNativeUtils_nativeMergeBitmapAlpha(
        JNIEnv* env,
        jclass clazz,
        jobject source,
        jobject alpha)
{
    AndroidBitmapInfo sinfo, ainfo;
    int ret;
    void* sourceP;
    void* alphaP;
    int xx, yy;

    if ((ret = AndroidBitmap_getInfo(env, source, &sinfo)) < 0) {
        LOGE("AndroidBitmap_getInfo() failed ! error=%d", ret);
        return;
    }

    if (sinfo.format != ANDROID_BITMAP_FORMAT_RGBA_8888) {
        LOGE("Bitmap format is not RGBA_8888 !");
        return;
    }

    if ((ret = AndroidBitmap_lockPixels(env, source, &sourceP)) < 0) {
        LOGE("AndroidBitmap_lockPixels() failed ! error=%d", ret);
    }

    if ((ret = AndroidBitmap_getInfo(env, alpha, &ainfo)) < 0) {
        LOGE("AndroidBitmap_getInfo() failed ! error=%d", ret);
        return;
    }

    if (ainfo.format != ANDROID_BITMAP_FORMAT_RGBA_8888) {
        LOGE("Bitmap format is not RGBA_8888 !");
        return;
    }

    if ((ret = AndroidBitmap_lockPixels(env, alpha, &alphaP)) < 0) {
        LOGE("AndroidBitmap_lockPixels() failed ! error=%d", ret);
    }

    uint32_t* sline = sourceP;
    uint32_t* aline = alphaP;

    for(yy = 0; yy < sinfo.height; yy++){
        sline = (uint32_t*) sourceP;
        aline = (uint32_t*) alphaP;

        for(xx =0; xx < sinfo.width; xx++) {
            sline[xx] = ARGB(R(aline[xx]), R(sline[xx]), G(sline[xx]), B(sline[xx]));
        }
        sourceP = (char*)sourceP + sinfo.stride;
        alphaP = (char*)alphaP + ainfo.stride;
    }


    AndroidBitmap_unlockPixels(env, source);
    AndroidBitmap_unlockPixels(env, alpha);
}

JNIEXPORT void Java_org_telegram_android_media_OptimizedBlur_nativeFastBlur(
        JNIEnv* env,
        jobject thiz,
        jobject bitmap)
{
    AndroidBitmapInfo  info;
        int ret;
        void* pixels;

        if ((ret = AndroidBitmap_getInfo(env, bitmap, &info)) < 0) {
                LOGE("AndroidBitmap_getInfo() failed ! error=%d", ret);
                return;
        }

        if (info.format != ANDROID_BITMAP_FORMAT_RGBA_8888) {
            LOGE("Bitmap format is not RGBA_8888 !");
            return;
        }

        if ((ret = AndroidBitmap_lockPixels(env, bitmap, &pixels)) < 0) {
            LOGE("AndroidBitmap_lockPixels() failed ! error=%d", ret);
        }

        fastBlur(&info,pixels);
        // brightness(&info,pixels, 0.8f);

        AndroidBitmap_unlockPixels(env, bitmap);
}

JNIEXPORT void Java_org_telegram_android_media_BitmapDecoderEx_nativeDecodeBitmapBlend(
                                                             JNIEnv* env,
                                                             jobject thiz,
                                                             jstring fileName,
                                                             jobject bitmap)
{
    char * path =  (*env)->GetStringUTFChars( env, fileName , NULL );
    AndroidBitmapInfo  info;
    struct jpeg_error_mgr jerr;
    struct jpeg_decompress_struct cinfo;
    FILE * infile;		/* source file */
    JSAMPARRAY buffer;		/* Output row buffer */
    int row_stride;		/* physical row width in output buffer */
    int ret;
    int rowIndex;
    int i;
    void* pixels;

    if ((ret = AndroidBitmap_getInfo(env, bitmap, &info)) < 0) {
        LOGE("AndroidBitmap_getInfo() failed ! error=%d", ret);
        return;
    }

    if ((ret = AndroidBitmap_lockPixels(env, bitmap, &pixels)) < 0) {
        LOGE("AndroidBitmap_lockPixels() failed ! error=%d", ret);
    }

    LOGI("Loading image from path %s", path);

    if ((infile = fopen(path, "rb")) == NULL) {
        LOGE("Unable to open file");
        return;
    }

    cinfo.err = jpeg_std_error(&jerr);

    jpeg_create_decompress(&cinfo);

    jpeg_stdio_src(&cinfo, infile);
    (void) jpeg_read_header(&cinfo, TRUE);

    (void) jpeg_start_decompress(&cinfo);

    row_stride = cinfo.output_width * cinfo.output_components;

    buffer = (*cinfo.mem->alloc_sarray)
    ((j_common_ptr) &cinfo, JPOOL_IMAGE, row_stride, 1);

    rowIndex = 0;
    uint32_t* line = (uint32_t*)pixels;
    while (cinfo.output_scanline < cinfo.output_height) {
        (void) jpeg_read_scanlines(&cinfo, buffer, 1);

        if (rowIndex++ < info.height) {
            for( i = 0; i < MIN(info.width, cinfo.output_width); i++) {
                line[i] = ARGB(buffer[0][i], R(line[i]), G(line[i]), B(line[i]));
            }
            line = (char*)line + (info.stride);
        }
    }

    (void) jpeg_finish_decompress(&cinfo);

    AndroidBitmap_unlockPixels(env, bitmap);
}

JNIEXPORT void Java_org_telegram_android_media_BitmapDecoderEx_nativeDecodeBitmap(
                                                             JNIEnv* env,
                                                             jobject thiz,
                                                             jstring fileName,
                                                             jobject bitmap)
{
    char * path =  (*env)->GetStringUTFChars( env, fileName , NULL );
    AndroidBitmapInfo  info;
    struct jpeg_error_mgr jerr;
    struct jpeg_decompress_struct cinfo;
    FILE * infile;		/* source file */
    JSAMPARRAY buffer;		/* Output row buffer */
    int row_stride;		/* physical row width in output buffer */
    int ret;
    int rowIndex;
    int i;
    void* pixels;
    
    if ((ret = AndroidBitmap_getInfo(env, bitmap, &info)) < 0) {
        LOGE("AndroidBitmap_getInfo() failed ! error=%d", ret);
        return;
    }
    
    if ((ret = AndroidBitmap_lockPixels(env, bitmap, &pixels)) < 0) {
        LOGE("AndroidBitmap_lockPixels() failed ! error=%d", ret);
    }
    
    LOGI("Loading image from path %s", path);
    
    if ((infile = fopen(path, "rb")) == NULL) {
        LOGE("Unable to open file");
        return;
    }
    
    cinfo.err = jpeg_std_error(&jerr);
    
    jpeg_create_decompress(&cinfo);
    
    jpeg_stdio_src(&cinfo, infile);
    (void) jpeg_read_header(&cinfo, TRUE);
    
    (void) jpeg_start_decompress(&cinfo);
    
    row_stride = cinfo.output_width * cinfo.output_components;
    
    buffer = (*cinfo.mem->alloc_sarray)
    ((j_common_ptr) &cinfo, JPOOL_IMAGE, row_stride, 1);
    
    rowIndex = 0;
    uint32_t* line = (uint32_t*)pixels;
    while (cinfo.output_scanline < cinfo.output_height) {
        (void) jpeg_read_scanlines(&cinfo, buffer, 1);
        
        if (rowIndex++ < info.height) {
            for( i = 0; i < MIN(info.width, cinfo.output_width); i++) {
                line[i] = ARGB(255, buffer[0][i*3], buffer[0][i*3+1], buffer[0][i*3 + 2]);
            }
            line = (char*)line + (info.stride);
        }
    }
    
    (void) jpeg_finish_decompress(&cinfo);
    
    AndroidBitmap_unlockPixels(env, bitmap);
}

JNIEXPORT void Java_org_telegram_android_media_BitmapDecoderEx_nativeDecodeArray(
                                                             JNIEnv* env,
                                                             jobject thiz,
                                                             jbyteArray array,
                                                             jobject bitmap)
{
    AndroidBitmapInfo  info;
    struct jpeg_error_mgr jerr;
    struct jpeg_decompress_struct cinfo;
    JSAMPARRAY buffer;		/* Output row buffer */
    int row_stride;		/* physical row width in output buffer */
    int ret;
    int rowIndex;
    int i;
    void* pixels;

    if ((ret = AndroidBitmap_getInfo(env, bitmap, &info)) < 0) {
        LOGE("AndroidBitmap_getInfo() failed ! error=%d", ret);
        return;
    }

    if ((ret = AndroidBitmap_lockPixels(env, bitmap, &pixels)) < 0) {
        LOGE("AndroidBitmap_lockPixels() failed ! error=%d", ret);
    }

    jbyte *b = (jbyte *)(*env)->GetByteArrayElements(env, array, NULL);
    jsize len = (*env)->GetArrayLength(env, array);

    cinfo.err = jpeg_std_error(&jerr);

    jpeg_create_decompress(&cinfo);

    jpeg_mem_src(&cinfo, b, len);
    // jpeg_stdio_src(&cinfo, infile);

    (void) jpeg_read_header(&cinfo, TRUE);

    (void) jpeg_start_decompress(&cinfo);

    row_stride = cinfo.output_width * cinfo.output_components;

    buffer = (*cinfo.mem->alloc_sarray)
    ((j_common_ptr) &cinfo, JPOOL_IMAGE, row_stride, 1);

    rowIndex = 0;
    uint32_t* line = (uint32_t*)pixels;
    while (cinfo.output_scanline < cinfo.output_height) {
        (void) jpeg_read_scanlines(&cinfo, buffer, 1);

        if (rowIndex++ < info.height) {
            for( i = 0; i < MIN(info.width, cinfo.output_width); i++) {
                line[i] = ARGB(255, buffer[0][i*3], buffer[0][i*3+1], buffer[0][i*3 + 2]);
            }
            line = (char*)line + (info.stride);
        }
    }

    (void) jpeg_finish_decompress(&cinfo);

    AndroidBitmap_unlockPixels(env, bitmap);

    (*env)->ReleaseByteArrayElements(env, array, b, 0 );
}

JNIEXPORT void Java_org_telegram_android_media_BitmapDecoderEx_nativeDecodeBitmapScaled(
                                                             JNIEnv* env,
                                                             jobject thiz,
                                                             jstring fileName,
                                                             jobject bitmap)
{
    char * path =  (*env)->GetStringUTFChars( env, fileName , NULL );
    AndroidBitmapInfo  info;
    struct jpeg_error_mgr jerr;
    struct jpeg_decompress_struct cinfo;
    FILE * infile;		/* source file */
    JSAMPARRAY buffer;		/* Output row buffer */
    int row_stride;		/* physical row width in output buffer */
    int ret;
    int rowIndex;
    int sRowIndex;
    int i;
    int ind;
    void* pixels;

    if ((ret = AndroidBitmap_getInfo(env, bitmap, &info)) < 0) {
        LOGE("AndroidBitmap_getInfo() failed ! error=%d", ret);
        return;
    }

    if ((ret = AndroidBitmap_lockPixels(env, bitmap, &pixels)) < 0) {
        LOGE("AndroidBitmap_lockPixels() failed ! error=%d", ret);
    }

    LOGI("Loading image from path %s", path);

    if ((infile = fopen(path, "rb")) == NULL) {
        LOGE("Unable to open file");
        return;
    }

    cinfo.err = jpeg_std_error(&jerr);

    jpeg_create_decompress(&cinfo);

    jpeg_stdio_src(&cinfo, infile);
    (void) jpeg_read_header(&cinfo, TRUE);

    (void) jpeg_start_decompress(&cinfo);

    row_stride = cinfo.output_width * cinfo.output_components;

    buffer = (*cinfo.mem->alloc_sarray)
    ((j_common_ptr) &cinfo, JPOOL_IMAGE, row_stride, 1);

    rowIndex = 0;
    sRowIndex = 0;
    uint32_t* line = (uint32_t*)pixels;
    while (cinfo.output_scanline < cinfo.output_height) {
        (void) jpeg_read_scanlines(&cinfo, buffer, 1);

        if (sRowIndex++ % 2 == 0)
        {
            if (rowIndex++ < info.height) {
                for( i = 0; i < MIN(info.width, cinfo.output_width/2); i++) {
                    ind = i * 2;
                    line[i] = ARGB(255, buffer[0][ind*3], buffer[0][ind*3+1], buffer[0][ind*3 + 2]);
                }
                line = (char*)line + (info.stride);
            }
        }
    }

    (void) jpeg_finish_decompress(&cinfo);

    AndroidBitmap_unlockPixels(env, bitmap);
}

JNIEXPORT void Java_org_telegram_android_util_NativeAES_nativeAesDecrypt(
                                                             JNIEnv* env,
                                                             jobject thiz,
                                                             jbyteArray _source,
                                                             jbyteArray _dest,
                                                             jint len,
                                                             jbyteArray _iv,
                                                             jbyteArray _key) {
    unsigned char *source = (unsigned char *)(*env)->GetByteArrayElements(env, _source, NULL);
    unsigned char *dest = (unsigned char *)(*env)->GetByteArrayElements(env, _dest, NULL);
    unsigned char *key = (unsigned char *)(*env)->GetByteArrayElements(env, _key, NULL);
    unsigned char *iv = (unsigned char *)(*env)->GetByteArrayElements(env, _iv, NULL);

    AES_KEY akey;
    AES_set_decrypt_key(key, (*env)->GetArrayLength(env, _key) * 8, &akey);
    AES_ige_encrypt(source, dest, len, &akey, iv, AES_DECRYPT);

    (*env)->ReleaseByteArrayElements(env, _source, _source, JNI_ABORT);
    (*env)->ReleaseByteArrayElements(env, _dest, _dest, 0);
    (*env)->ReleaseByteArrayElements(env, _key, key, JNI_ABORT);
    (*env)->ReleaseByteArrayElements(env, _iv, iv, JNI_ABORT);
}

JNIEXPORT void Java_org_telegram_android_util_NativeAES_nativeAesDecryptStreaming(
                                                             JNIEnv* env,
                                                             jobject thiz,
                                                             jbyteArray _source,
                                                             jbyteArray _dest,
                                                             jint len,
                                                             jbyteArray _iv,
                                                             jbyteArray _key) {
    unsigned char *source = (unsigned char *)(*env)->GetByteArrayElements(env, _source, NULL);
    unsigned char *dest = (unsigned char *)(*env)->GetByteArrayElements(env, _dest, NULL);
    unsigned char *key = (unsigned char *)(*env)->GetByteArrayElements(env, _key, NULL);
    unsigned char *iv = (unsigned char *)(*env)->GetByteArrayElements(env, _iv, NULL);

    AES_KEY akey;
    AES_set_decrypt_key(key, (*env)->GetArrayLength(env, _key) * 8, &akey);
    AES_ige_encrypt(source, dest, len, &akey, iv, AES_DECRYPT);

    (*env)->ReleaseByteArrayElements(env, _source, _source, JNI_ABORT);
    (*env)->ReleaseByteArrayElements(env, _dest, _dest, 0);
    (*env)->ReleaseByteArrayElements(env, _key, key, JNI_ABORT);
    (*env)->ReleaseByteArrayElements(env, _iv, iv, 0);
}

JNIEXPORT void Java_org_telegram_android_util_NativeAES_nativeAesEncrypt(
                                                             JNIEnv* env,
                                                             jobject thiz,
                                                             jbyteArray _source,
                                                             jbyteArray _dest,
                                                             jint len,
                                                             jbyteArray _iv,
                                                             jbyteArray _key) {

    unsigned char *source = (unsigned char *)(*env)->GetByteArrayElements(env, _source, NULL);
    unsigned char *dest = (unsigned char *)(*env)->GetByteArrayElements(env, _dest, NULL);
    unsigned char *key = (unsigned char *)(*env)->GetByteArrayElements(env, _key, NULL);
    unsigned char *iv = (unsigned char *)(*env)->GetByteArrayElements(env, _iv, NULL);

    AES_KEY akey;
    AES_set_encrypt_key(key, (*env)->GetArrayLength(env, _key) * 8, &akey);
    AES_ige_encrypt(source, dest, len, &akey, iv, AES_ENCRYPT);

    (*env)->ReleaseByteArrayElements(env, _source, _source, JNI_ABORT);
    (*env)->ReleaseByteArrayElements(env, _dest, _dest, 0);
    (*env)->ReleaseByteArrayElements(env, _key, key, JNI_ABORT);
    (*env)->ReleaseByteArrayElements(env, _iv, iv, JNI_ABORT);
}

JNIEXPORT void Java_org_telegram_android_util_NativeAES_nativeAesEncryptStreaming(
                                                             JNIEnv* env,
                                                             jobject thiz,
                                                             jbyteArray _source,
                                                             jbyteArray _dest,
                                                             jint len,
                                                             jbyteArray _iv,
                                                             jbyteArray _key) {

    unsigned char *source = (unsigned char *)(*env)->GetByteArrayElements(env, _source, NULL);
    unsigned char *dest = (unsigned char *)(*env)->GetByteArrayElements(env, _dest, NULL);
    unsigned char *key = (unsigned char *)(*env)->GetByteArrayElements(env, _key, NULL);
    unsigned char *iv = (unsigned char *)(*env)->GetByteArrayElements(env, _iv, NULL);

    AES_KEY akey;
    AES_set_encrypt_key(key, (*env)->GetArrayLength(env, _key) * 8, &akey);
    AES_ige_encrypt(source, dest, len, &akey, iv, AES_ENCRYPT);

    (*env)->ReleaseByteArrayElements(env, _source, _source, JNI_ABORT);
    (*env)->ReleaseByteArrayElements(env, _dest, _dest, 0);
    (*env)->ReleaseByteArrayElements(env, _key, key, JNI_ABORT);
    (*env)->ReleaseByteArrayElements(env, _iv, iv, 0);
}

uint64_t gcd(uint64_t a, uint64_t b){
    while(a != 0 && b != 0) {
        while((b & 1) == 0) b >>= 1;
        while((a & 1) == 0) a >>= 1;
        if(a > b) a -= b; else b -= a;
    }
    return b == 0 ? a : b;
}

JNIEXPORT jlong Java_org_telegram_android_util_NativePQ_solvePq(JNIEnv* env, jobject thiz, jlong src)
{
    uint64_t what = src;
        int it = 0, i, j;
        uint64_t g = 0;
        for (i = 0; i < 3 || it < 1000; i++){
            int q = ((lrand48() & 15) + 17) % what;
            uint64_t x = (long long)lrand48() % (what - 1) + 1, y = x;
            int lim = 1 << (i + 18), j;
            for(j = 1; j < lim; j++){
                ++it;
                uint64_t a = x, b = x, c = q;
                while(b){
                    if(b & 1){
                        c += a;
                        if(c >= what) c -= what;
                    }
                    a += a;
                    if(a >= what) a -= what;
                    b >>= 1;
                }
                x = c;
                uint64_t z = x < y ? what + x - y : x - y;
                g = gcd(z, what);
                if(g != 1) break;
                if(!(j & (j - 1))) y = x;
            }
            if(g > 1 && g < what) break;
        }
        return g;

}


void Java_org_telegram_android_util_ImageNativeUtils_nativeLoadEmoji(
        JNIEnv* env,
        jclass clazz,
        jstring colorPath,
        jstring alphaPath) {
    char * cPath =  (*env)->GetStringUTFChars( env, colorPath , NULL );
    char * aPath =  (*env)->GetStringUTFChars( env, alphaPath , NULL );
    AndroidBitmapInfo  info;
    struct jpeg_error_mgr jerr;
    struct jpeg_decompress_struct cinfo;
    struct jpeg_decompress_struct ainfo;
    FILE *cFile, *aFile;
    JSAMPARRAY cBuffer, aBuffer;
    int strideC, strideA, ret, rowIndex, i;
    void *cPixels, *aPixels;
    uint32_t *cData, *aData;

    cinfo.err = jpeg_std_error(&jerr);
    ainfo.err = jpeg_std_error(&jerr);

    jpeg_create_decompress(&cinfo);
    jpeg_create_decompress(&ainfo);

    if ((cFile = fopen(cPath, "rb")) == NULL) {
        LOGE("Unable to open file");
        return;
    }

    if ((aFile = fopen(aPath, "rb")) == NULL) {
        LOGE("Unable to open file");
        return;
    }

    jpeg_stdio_src(&cinfo, cFile);
    jpeg_stdio_src(&ainfo, aFile);

    (void) jpeg_read_header(&cinfo, TRUE);
    (void) jpeg_read_header(&ainfo, TRUE);

    (void) jpeg_start_decompress(&cinfo);
    (void) jpeg_start_decompress(&ainfo);

    strideC = cinfo.output_width * cinfo.output_components;
    strideA = ainfo.output_width * ainfo.output_components;

    cBuffer = (*cinfo.mem->alloc_sarray)
        ((j_common_ptr) &cinfo, JPOOL_IMAGE, strideC, 1);
    aBuffer = (*ainfo.mem->alloc_sarray)
        ((j_common_ptr) &ainfo, JPOOL_IMAGE, strideA, 1);

    jclass java_bitmap_class = (jclass)(*env)->FindClass(env, "android/graphics/Bitmap");
    jmethodID mid = (*env)->GetStaticMethodID(env, java_bitmap_class, "createBitmap", "(IILandroid/graphics/Bitmap$Config;)Landroid/graphics/Bitmap;");

    jclass bcfg_class = (*env)->FindClass(env, "android/graphics/Bitmap$Config");
    jobject java_bitmap_config = (*env)->CallStaticObjectMethod(env, bcfg_class, (*env)->GetStaticMethodID(env, bcfg_class, "valueOf", "(Ljava/lang/String;)Landroid/graphics/Bitmap$Config;"), (*env)->NewStringUTF(env, "ARGB_8888"));

    jobject* res =(jobject*) malloc(16 * sizeof(jobject));
    for(i = 0; i < 16; i++) {
        res[i] = (*env)->CallStaticObjectMethod(env, java_bitmap_class, mid, 8 * 54, 8 * 54, java_bitmap_config);
    }

    cData = (uint32_t*)cPixels;
    aData = (unsigned char*)aPixels;
    while (cinfo.output_scanline < cinfo.output_height) {
        (void) jpeg_read_scanlines(&cinfo, cBuffer, 1);
        (void) jpeg_read_scanlines(&ainfo, aBuffer, 1);
    }

    (void) jpeg_finish_decompress(&cinfo);
    (void) jpeg_finish_decompress(&ainfo);
}