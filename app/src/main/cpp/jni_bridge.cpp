// SPDX-License-Identifier: GPL-2.0-or-later
// PocketCentury JNI bridge
#include <jni.h>
#include <android/bitmap.h>
#include <android/log.h>
#include <cstdarg>
#include <mutex>
#include <algorithm>
#include "sy909.h"

static Sy909 *g_sys = nullptr;
static std::mutex g_lock;

void unsp_log(const char *fmt, ...)
{
	va_list ap; va_start(ap, fmt);
	__android_log_vprint(ANDROID_LOG_DEBUG, "PocketCentury", fmt, ap);
	va_end(ap);
}
void unsp_fatal(const char *fmt, ...)
{
	va_list ap; va_start(ap, fmt);
	__android_log_vprint(ANDROID_LOG_ERROR, "PocketCentury", fmt, ap);
	va_end(ap);
}

static std::vector<uint8_t> to_vec(JNIEnv *env, jbyteArray a)
{
	jsize n = env->GetArrayLength(a);
	std::vector<uint8_t> v(n);
	env->GetByteArrayRegion(a, 0, n, (jbyte *)v.data());
	return v;
}

#define FN(name) Java_com_hcode91_pocketcentury_Native_##name

extern "C" {

JNIEXPORT void JNICALL FN(create)(JNIEnv *, jclass)
{
	std::lock_guard<std::mutex> l(g_lock);
	delete g_sys;
	g_sys = new Sy909();
	g_sys->trace_hle = false;
}

JNIEXPORT jstring JNICALL FN(load)(JNIEnv *env, jclass, jbyteArray spi1, jbyteArray spi2)
{
	std::lock_guard<std::mutex> l(g_lock);
	if (!g_sys) return env->NewStringUTF("emulator not created");
	std::string err;
	if (!g_sys->load(to_vec(env, spi1), to_vec(env, spi2), err)) return env->NewStringUTF(err.c_str());
	g_sys->power_on();
	return nullptr;
}

JNIEXPORT void JNICALL FN(reset)(JNIEnv *, jclass)
{
	std::lock_guard<std::mutex> l(g_lock);
	if (g_sys) g_sys->power_on();
}

JNIEXPORT void JNICALL FN(setButtons)(JNIEnv *, jclass, jint b)
{
	if (g_sys) g_sys->set_buttons((uint16_t)b);
}

JNIEXPORT jint JNICALL FN(audioRate)(JNIEnv *, jclass)
{
	return g_sys ? g_sys->audio_rate() : 9375;
}

// Runs one frame, copies video into the bitmap, writes audio into `audio`.
// Returns the number of samples written.
JNIEXPORT jint JNICALL FN(runFrame)(JNIEnv *env, jclass, jobject bitmap, jshortArray audio)
{
	std::lock_guard<std::mutex> l(g_lock);
	if (!g_sys) return 0;
	g_sys->run_frame();

	void *pixels = nullptr;
	AndroidBitmapInfo info;
	if (AndroidBitmap_getInfo(env, bitmap, &info) == 0 && AndroidBitmap_lockPixels(env, bitmap, &pixels) == 0)
	{
		const uint16_t *src = g_sys->framebuffer();
		for (uint32_t y = 0; y < info.height && y < 240; y++)
		{
			uint32_t *dst = (uint32_t *)((uint8_t *)pixels + y * info.stride);
			for (uint32_t x = 0; x < info.width && x < 320; x++)
			{
				uint16_t c = src[y * 320 + x];
				uint32_t r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
				r = (r << 3) | (r >> 2); g = (g << 2) | (g >> 4); b = (b << 3) | (b >> 2);
				dst[x] = 0xff000000u | (b << 16) | (g << 8) | r; // RGBA_8888 in memory
			}
		}
		AndroidBitmap_unlockPixels(env, bitmap);
	}

	const auto &a = g_sys->audio();
	jsize cap = env->GetArrayLength(audio);
	jsize n = std::min<jsize>(cap, (jsize)a.size());
	env->SetShortArrayRegion(audio, 0, n, a.data());
	return n;
}

JNIEXPORT jbyteArray JNICALL FN(saveState)(JNIEnv *env, jclass)
{
	std::lock_guard<std::mutex> l(g_lock);
	if (!g_sys) return nullptr;
	auto s = g_sys->save_state();
	jbyteArray a = env->NewByteArray((jsize)s.size());
	env->SetByteArrayRegion(a, 0, (jsize)s.size(), (const jbyte *)s.data());
	return a;
}

JNIEXPORT jboolean JNICALL FN(loadState)(JNIEnv *env, jclass, jbyteArray data)
{
	std::lock_guard<std::mutex> l(g_lock);
	return g_sys && g_sys->load_state(to_vec(env, data));
}

} // extern "C"
