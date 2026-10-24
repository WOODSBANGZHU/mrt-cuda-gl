// Windows CUDA/OpenGL interop bridge for the Java video pipeline.
// Build this file as mrt_cuda_gl.dll and place it next to the application.
// It deliberately exposes only AVFrame retain/release and NV12 upload; no
// CUDA pointer is ever mapped into a Java ByteBuffer.

#include <jni.h>

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/hwcontext.h>
}

#include <cuda_runtime_api.h>
#include <cuda_gl_interop.h>
#include <GL/gl.h>

#include <cstdint>
#include <mutex>

namespace {

struct FrameRef {
    AVFrame* frame;
};

const AVHWFramesContext* hwFrames(const AVFrame* frame) {
    return frame != nullptr && frame->hw_frames_ctx != nullptr
           ? reinterpret_cast<const AVHWFramesContext*>(frame->hw_frames_ctx->data)
           : nullptr;
}

bool cudaFrame(const AVFrame* frame) {
    if (frame == nullptr || frame->format != AV_PIX_FMT_CUDA ||
        frame->hw_frames_ctx == nullptr || frame->data[0] == nullptr) {
        return false;
    }
    const auto* context = hwFrames(frame);
    return context != nullptr && context->device_ref != nullptr;
}

bool copyToTexture(const void* source, size_t pitch, int width, int height, GLuint texture) {
    cudaGraphicsResource* resource = nullptr;
    cudaError_t status = cudaGraphicsGLRegisterImage(
            &resource, texture, GL_TEXTURE_2D, cudaGraphicsRegisterFlagsWriteDiscard);
    if (status != cudaSuccess) {
        return false;
    }

    status = cudaGraphicsMapResources(1, &resource, 0);
    if (status != cudaSuccess) {
        cudaGraphicsUnregisterResource(resource);
        return false;
    }

    cudaArray_t array = nullptr;
    status = cudaGraphicsSubResourceGetMappedArray(&array, resource, 0, 0);
    if (status == cudaSuccess) {
        status = cudaMemcpy2DToArray(array, 0, 0, source, pitch,
                                     static_cast<size_t>(width),
                                     static_cast<size_t>(height),
                                     cudaMemcpyDeviceToDevice);
    }

    cudaGraphicsUnmapResources(1, &resource, 0);
    cudaGraphicsUnregisterResource(resource);
    return status == cudaSuccess;
}

bool ensureInteropDevice() {
    static std::once_flag once;
    static bool ready = false;
    std::call_once(once, [] {
        cudaError_t status = cudaGLSetGLDevice(0);
        if (status == cudaSuccess) {
            ready = true;
            return;
        }
        // cudaGetDeviceCount() is used by the Java-side self-test before a
        // GL context exists. If that initialized the runtime already,
        // cudaGLSetGLDevice reports cudaErrorSetOnActiveProcess; accept it
        // only when the active device is the requested one.
        if (status == cudaErrorSetOnActiveProcess) {
            int device = -1;
            ready = cudaGetDevice(&device) == cudaSuccess && device == 0;
        }
    });
    return ready;
}

} // namespace

extern "C" JNIEXPORT jboolean JNICALL
Java_org_client_video_grabber_CudaGlInterop_nativeSelfTest(JNIEnv*, jclass) {
    int count = 0;
    return cudaGetDeviceCount(&count) == cudaSuccess && count > 0 ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jlong JNICALL
Java_org_client_video_grabber_CudaGlInterop_retainFrame__J(JNIEnv*, jclass, jlong address) {
    const auto* source = reinterpret_cast<const AVFrame*>(static_cast<uintptr_t>(address));
    if (!cudaFrame(source)) {
        return 0;
    }

    AVFrame* retained = av_frame_alloc();
    if (retained == nullptr || av_frame_ref(retained, source) < 0) {
        if (retained != nullptr) {
            av_frame_free(&retained);
        }
        return 0;
    }

    auto* ref = new FrameRef{retained};
    return static_cast<jlong>(reinterpret_cast<uintptr_t>(ref));
}

extern "C" JNIEXPORT void JNICALL
Java_org_client_video_grabber_CudaGlInterop_releaseFrameNative(JNIEnv*, jclass, jlong address) {
    auto* ref = reinterpret_cast<FrameRef*>(static_cast<uintptr_t>(address));
    if (ref == nullptr) {
        return;
    }
    av_frame_free(&ref->frame);
    delete ref;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_org_client_video_grabber_CudaGlInterop_uploadNv12Native(
        JNIEnv*, jclass, jlong address, jint yTexture, jint uvTexture) {
    auto* ref = reinterpret_cast<FrameRef*>(static_cast<uintptr_t>(address));
    if (ref == nullptr || !cudaFrame(ref->frame) || ref->frame->width <= 0 ||
        ref->frame->height <= 0 || ref->frame->linesize[0] <= 0) {
        return JNI_FALSE;
    }
    if (!ensureInteropDevice()) {
        return JNI_FALSE;
    }

    const int width = ref->frame->width;
    const int height = ref->frame->height;
    const size_t yPitch = static_cast<size_t>(ref->frame->linesize[0]);
    const size_t uvPitch = ref->frame->linesize[1] > 0
                           ? static_cast<size_t>(ref->frame->linesize[1])
                           : yPitch;
    const auto* context = hwFrames(ref->frame);
    if (context == nullptr ||
        (context->sw_format != AV_PIX_FMT_NV12 && context->sw_format != AV_PIX_FMT_P010LE)) {
        return JNI_FALSE;
    }
    const bool p010 = context->sw_format == AV_PIX_FMT_P010LE;
    const int bytesPerSample = p010 ? 2 : 1;
    const size_t rowBytes = static_cast<size_t>(width) * bytesPerSample;
    if (yPitch < rowBytes || uvPitch < rowBytes) {
        return JNI_FALSE;
    }
    const auto* y = reinterpret_cast<const void*>(ref->frame->data[0]);
    const auto* uv = ref->frame->data[1] != nullptr
                     ? reinterpret_cast<const void*>(ref->frame->data[1])
                     : reinterpret_cast<const void*>(
                             reinterpret_cast<const std::uint8_t*>(y) + yPitch * height);

    if (!copyToTexture(y, yPitch, static_cast<int>(rowBytes), height,
                       static_cast<GLuint>(yTexture))) {
        return JNI_FALSE;
    }
    return copyToTexture(uv, uvPitch, static_cast<int>(rowBytes), height / 2,
                         static_cast<GLuint>(uvTexture))
           ? JNI_TRUE : JNI_FALSE;
}
