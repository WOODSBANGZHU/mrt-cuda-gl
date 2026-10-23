# CUDA/OpenGL zero-copy bridge

The Java path enables this backend only when `mrt_cuda_gl.dll` loads and the
bridge self-test finds a CUDA device. Build the DLL with the FFmpeg development
package used by the application and the CUDA Toolkit:

Windows:

```powershell
cmake -S native/cuda_gl -B build/cuda-gl -DFFMPEG_ROOT=C:/ffmpeg
cmake --build build/cuda-gl --config Release
```

Ubuntu:

```bash
sudo apt install build-essential cmake libavutil-dev libgl-dev openjdk-8-jdk
cmake -S native/cuda_gl -B build/cuda-gl -DFFMPEG_ROOT=/opt/ffmpeg
cmake --build build/cuda-gl --config Release -j
```

The repository also contains [Linux CI](../../.github/workflows/linux-ci.yml),
which builds `libmrt_cuda_gl.so` in a CUDA development container and publishes
it as a downloadable workflow artifact. The hosted runner has no NVIDIA GPU,
so the workflow verifies compilation only; runtime CUDA/OpenGL validation must
be performed on the deployment machine or a self-hosted GPU runner.

Place `mrt_cuda_gl.dll` beside the Windows application executable, or
`libmrt_cuda_gl.so` in the Ubuntu application's library directory. The native
FFmpeg `avutil` library and CUDA runtime must be discoverable by the platform's
loader. The Java side can use another library name with
`-Dmrt.cuda.gl.library=...`.

The bridge retains each CUDA `AVFrame` with `av_frame_ref`. On the OpenGL
thread it maps two `GL_R8`/`GL_RG8` textures through CUDA graphics interop and
copies the NV12 Y and interleaved UV planes on the GPU. No CUDA pointer is
wrapped as a Java `ByteBuffer`, and no device-to-host readback occurs.

If the DLL is absent, incompatible, or reports no CUDA device, the grabber
automatically selects the existing CPU YUV420P path. This fallback is required
because the Java-only application cannot safely map an FFmpeg CUDA surface into
OpenGL.
