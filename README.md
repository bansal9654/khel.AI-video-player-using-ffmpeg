# C++ FFmpeg/OpenCV Video Player

This is a simple, frame-accurate video player created in C++. It uses FFmpeg for video decoding and OpenCV for displaying the video frames in a window.

## Dependencies

To build this project, you will need:
* **Visual Studio 2022** (with the "Desktop development with C++" workload)
* **CMake**
* **vcpkg** (C++ Package Manager)

The following libraries are required and can be installed via `vcpkg`:
* **FFmpeg**
* **OpenCV**

## How to Build (Windows)

1.  **Install Libraries:**
    Use `vcpkg` to install the necessary dependencies:
    ```bash
    vcpkg install ffmpeg:x64-windows opencv4:x64-windows
    ```

2.  **Configure with CMake:**
    Create a `build` directory and run CMake from within it. You must point CMake to the `vcpkg` toolchain file.

    ```bash
    # From the project's root directory
    mkdir build
    cd build
    
    # Replace C:\vcpkg with the actual path to your vcpkg installation
    cmake .. -DCMAKE_TOOLCHAIN_FILE=C:\vcpkg\scripts\buildsystems\vcpkg.cmake
    ```

3.  **Compile:**
    After CMake configures the project, run the build command:
    ```bash
    cmake --build . --config Release
    ```
    This will create `vmix_player.exe` inside the `build\Release` folder.

## How to Run

Run the application from your terminal, passing the path to your video file as the first argument.

```bash
# Navigate to your build directory (e.g., C:\path\to\your\project\build)
cd C:\path\to\your\project\build

# Run the executable, pointing it to a video file
.\Release\vmix_player.exe C:\path\to\your\video.mp4

```
## Player Controls

* **Spacebar**: Play/Pause
* 'n' : Step one frame forward
* 'b' : Step one frame backward
* 'q' / ESC: Quit the player
