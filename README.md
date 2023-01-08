# JPEGView - Image Viewer and Editor

This is a mod of [sylikc's official re-release of JPEGView](https://github.com/sylikc/jpegview/) to focus on the slideshow aspect of the app. _**Beta**_: [AVIF image format](https://avif.io/blog/articles/avif-faq/#%E2%8F%A9generalinformation) support has been added for viewing only (include animated AVIF); save as AVIF not yet implemented (low priority). 

## Description

JPEGView is a lean, fast and highly configurable viewer/editor for AVIF (_beta_), JPEG, BMP, PNG, WEBP, TGA, GIF and TIFF images with a minimal GUI. Basic on-the-fly image processing is provided - allowing adjusting typical parameters as sharpness, color balance, rotation, perspective, contrast and local under-/overexposure.

Features
* Small and fast, uses SSE2 and up to 4 CPU cores
* High quality resampling filter, preserving sharpness of images
* Basic image processing tools can be applied realtime during viewing
* Movie mode to play folder of JPEGs as movie

(summary from the original SourceForge project page)

### Slideshow

JPEGView has a slideshow mode which can be activated in various ways:
* **hotkey**:
  * **ALT+R**: 'resume'/start slideshow @ default of 1fps.
  * **ALT+SHF+R** (added in mod): start slideshow @ custom interval configured via `SlideShowCustomInterval` setting in `JPEGView.ini`.
  * **one of number 1 to 9**: start slideshow with corresponding delay in seconds, i.e. 1s to 9s.
    * May get rid of these excessive selections and repurpose these hotkeys for something else more useful in future.
  * **CTRL+(one of number 1 to 9)**: start slideshow with corresponding delay in tenth of a second (number x 0.1), i.e. 0.1s to 0.9s.
  * **CTRL+SHF+(one of number 1 to 9)**: start slideshow with corresponding delay in hundredth of a second (number x 0.01), i.e. 0.01s to 0.09s.
  * **ESC**: exit slideshow.
  * **SHIFT+Space** (_**added in mod**_): start slideshow @ 1fps.
  This is added via `KeyMap.txt` file.
  * **Plus** (added in mod; when in slideshow mode): increase slideshow frame interval, i.e. slow it down, in steps. Steps are the available fps speeds listed below.
  * **Minus** (_**added in mod**_; when in slideshow mode): decrease slideshow frame interval, i.e. speed it down, in steps. Steps are the available fps speeds listed below.
  * **Space** (_**modified in mod**_; when in slideshow mode): pause/resume slideshow.
  When not in slideshow, it toggles fit to window.
* **context menu**: right click image, select "Play folder as slideshow/movie" any of the options available - which are different slideshow speeds.
  * available speeds in fps: 100, 50, 30, 25, 10, 5, 1, 0.5, 0.33, 0.25, 0.2, 0.143, 0.1, 0.05, and _custom_ (interval)
  * _custom_ (_**added in mod**_): as per `SlideShowCustomInterval` setting.
* **commandline**: `JPEGView.exe [optional image/path] /slideshow <interval in secs>`
  Interval must be integer >= 1, or defaults to 5s; no upper limit.
    * E.g.: `JPEGView.exe c:\image.png /slideshow 1`
    starts JPEGView in slideshow with image switching at 1s intervals, beginning with given image or folder.
    * E.g.: `JPEGView.exe /slideshow 2`
    (**modified in mod**) starts JPEGView in image selection mode, and then starts slideshow with image switching at 2s intervals. (Previously when an image/path is not specified, `/slideshow` is ignored)

This mod makes it easier to manipulate the slideshow, specifically to pause/resume it, and (shift) up/down its speed.

### Slideshow Custom Interval

Configure slideshow to run at custom interval via `SlideShowCustomInterval` setting in `JPEGView.ini`.
* Default: 5mins
* Specify in secs, mins or hrs; default unit: seconds.
  * E.g.: `0.1` = 0.1s = 10fps, `1s` = 1sec = 1fps, `5m` = 5 mins, `1.5h` = 1.5hrs

Can be activated by **ALT+SHF+R** hotkey.

Alternatively, you may use the `/slideshow` commandline option to specify arbitrary intervals too; so this feature may not really be needed?

### Customizations

JPEGView hotkeys can be customized via the `KeyMap.txt` file.
WARNING: errors will render all hotkeys disabled.
* Available #define's are are in `src/JPEGView/resource.h`. These are the available commands that can be mapped to hotkeys.
* The bottom section are the hotkey mappings to the above commands.

Other useful customizations, refer to this [guide](https://yunharla.wixsite.com/softwaremmm/post/alternate-photo-viewer-for-windows-10-xnview)

### Wishlist

* A little Android-like `toast` to inform of new slideshow fps or interval. Or other notifications.

# Developer Notes

These notes are here in case anyone wishes to further enhance JPEGView =D and meddle with the troublesome-to-build avif+aom stuff.

## AVIF Image Format
![](https://github.com/sdneon/jpegview/releases/download/v1.1.41.3-beta/gentle-fists.avif)
(this is an animated AVIF; it doesn't animate if your browser does not support)

Support for viewing of AVIF images is via [AOMediaCodec/libavif](https://github.com/AOMediaCodec/libavif/) + [Alliance for Open Media](https://aomedia.googlesource.com/aom).
* JPEGView uses `avif.dll` from libavif; JPEGView requires `avif.lib` from libavif.
  * `libavif\examples\avif_example_decode_file.c` example is adapted into JPEGView's `ImageLoadThread.cpp`, `CImageLoadThread::ProcessReadAVIFRequest()` method, with reference to `ProcessReadWEBPRequest()`.
* libavif issues:
  *  [Resolved] c-based, using malloc/free for image buffer allocation - this has been modded in JPEGView to follow the latter's convention in using new[]/delete[] so as to let it (CJPEGImage) manage freeing of the memory itself.
  *  [Resolved] uses char filenames, unlike JPEGView which uses wchar_t. Switched to use of `avifDecoderSetIOMemory` instead of `avifDecoderSetIOFile`, so images with unicode filenames can now be opened.
     *  Though the original way letting libavif handle file read seems a tad more stable. There's less chance of animated AVIF image frame load error when switching image rapidly.
* aom's `aom.lib` seems statically linked into libavif to produce `avif.lib`, so its aom.dll is not needed by JPEGView.
* Tested on AVIF sample images from [https://github.com/link-u/avif-sample-images/](link-u/avif-sample-images)
    * Known issue: unable to view 'crop'ped samples - something to do with libavif internal  error: `AVIF_RESULT_BMFF_PARSE_FAILED`. There are many checks which can throw this error!
    * `CJPEGImage` class only handles 8bpp images? So higher bpp images are 'downgraded' to 8bpp.
* Added code for EXIF data extraction using `Helpers::FindEXIFBlock(pBuffer, nFileSize)`, but Not yet tested.

### Building aom

Roughly follow the steps in [aom's guide](https://aomedia.googlesource.com/aom) and roughly as such (for building in Win 7):
* Clone [aom repo](https://aomedia.googlesource.com/aom)
* These tools are needed. Install them:
  * Microsoft Visual Studio 2019. Don't use VS2017, something will fail.
  * CMake. Must be sufficiently new version in order to have (VS project generator for >= VS2019).
  * [NASM](https://www.nasm.us/)
  * [Strawberry Perl](https://strawberryperl.com/). There's another Perl listed by [perl.org](https://www.perl.org/) but that's troublesome needing registration to download.
*  Open Developer Tools for VS2019 command prompt, launch CMake GUI from it.
    * For 'Where the source code is', select the cloned aom folder.
    * Create a new folder, say 'aom_build', elsewhere for CMake & build output.
    * Check the (key) Name-Value pairs to ensure these are properly (detected and) filled in:
        * AS_EXECUTABLE = <path to NASM's exe>
        * PERL_EXECUTABLE = <path to Perl's exe>
    * Click the 'Configure' button. Hopefully there're no errors, so as to be able to move to the next step. If not, good luck resolve any errors.
    * Click the 'Generate' button to generate VS solution and projects files. If all goes well, click the 'Open Project' button to open in VS2019 =)
    * Probably only need to initate build on the `aom` project to get `aom.lib`. Build will take quite some time as dependencies are built.
        * Desired output: `aom_build/Release/aom.lib` which is needed by libavif below.

### Building libavif

* Download and unzip one of [libavif's latest release](https://github.com/AOMediaCodec/libavif/releases).
* These tools are needed. Install them:
  * Microsoft Visual Studio 2019.
  * CMake. Must be sufficiently new version in order to have (VS project generator for >= VS2019).
* These libraries are needed. Find, download and unzip them. Not sure if they're really needed for `avif.lib`, but CMake will scream and break if their Name-Value info aren't filled in. The (key) Name's in CMake are:
    * JPEG_LIBRARY_DEBUG / JPEG_LIBRARY_RELEASE
    * PNG_LIBRARY_DEBUG / PNG_LIBRARY_RELEASE
    * ZLIB_LIBRARY_DEBUG / ZLIB_LIBRARY_RELEASE
*  Open Developer Tools for VS2019 command prompt, launch CMake GUI from it.
    * For 'Where the source code is', select the unzipped libavif folder.
    * Create a new folder, say 'libavif_build', elsewhere for CMake & build output.
    * Check these (key) Name-Value pairs to ensure these are properly (detected and) filled in: JPEG_LIBRARY_*, PNG_LIBRARY_* and ZLIB_LIBRARY_*
    * Tick these (key) Name's:
        * `AVIF_CODEC_AOM` and maybe `AVIF_CODEC_AOM_ENCODE` & `AVIF_CODEC_AOM_DECODE`
        * `AOM_INCLUDE_FOLDER`: fill in the 'aom' folder path from earlier, like `c:/aom`.
        * `AOM_LIBRARY`: fill in the earlier, like `c:/aom_build/Release/aom.lib`
    * Click the 'Configure' button. Hopefully there're no errors, so as to be able to move to the next step. If not, good luck resolve any errors.
    * Click the 'Generate' button to generate VS solution and projects files. If all goes well, click the 'Open Project' button to open in VS2019 =)
    * Probably only need to initate build on the `ext/avif/avif` project to get `avif.lib` & `avif.dll`.
        * Optional: build `ext/avid/examples/avif_example_decode_file` project to get a .EXE to test decoding AVIF images.
        * Many project may fail to build (like owing to bad jpeg libs, etc), but they probably don't matter / aren't needed.
        * Desired output: `libavif_build/avif.lib` and `avif.dll`, which are needed by JPEGView =)

### JPEGView
The above `avif.lib` then goes into `src\JPEGView\libavif\lib64`.
`avif.dll` goes to `src\JPEGView\bin\x64\Release` and/or Debug folder.
These are added in `JPEGView.vcxproj`'s configuration.

# Installation

## Official Releases

Official releases will be made to [sylikc's GitHub Releases](https://github.com/sylikc/jpegview/releases) page.  Each release includes:
* **Archive Zip/7z** - Portable
* **Windows Installer MSI** - For Installs
* **Source code** - Build it yourself

### Mod Releases

2 zip files:
* `JPEGView-less-config.zip` - (one purely of the executable and DLLs) for updating your copy, without overriding your existing configuration files.
  * You will have to merge new config settings yourselves. E.g.: `SlideShowCustomInterval`.
* `JPEGView.zip` - full package for unzip and run. Includes above Plus all config/translation/etc files.

## Portable

JPEGView _does not require installation_ to run.  Just **unzip, and run** either the 64-bit version, or the 32-bit version depending on which platform you're on.  It can save the settings to the extracted folder and run entirely portable.

### Mod Update Portable Apps Version

Strangely, there's a [portable app version of JPEGView](https://portableapps.com/apps/graphics_pictures/jpegview_portable) on [portableapps.com](https://portableapps.com/).

To use this mod, simply replace the JPEGView.exe, *.DLL and KeyMap.txt in the `App/JPEGView` sub-folder.

## MSI Installer

For those who prefer to have JPEGView installed for All Users, a 32-bit/64-bit installer will be available to download starting with v1.0.40.  I don't own a code signing certificate yet, so the MSI release is not signed.

### WinGet

If you're on Windows 11, or Windows 10 1709 or later, you can also download it directly from the official [Microsoft WinGet tool](https://docs.microsoft.com/en-us/windows/package-manager/winget/) repository.  This downloads the latest MSI installer directly from GitHub for installation.

Example Usage:

C:\> `winget search jpegview`
```
Name     Id              Version  Source
-----------------------------------------
JPEGView sylikc.JPEGView 1.0.39.1 winget
```

C:\> `winget install jpegview`
```
Found JPEGView [sylikc.JPEGView] Version 1.0.39.1
This application is licensed to you by its owner.
Microsoft is not responsible for, nor does it grant any licenses to, third-party packages.
Downloading https://github.com/sylikc/jpegview/releases/download/v1.0.39.1-wix/JPEGView64_en-us_1.0.39.1.msi
  ==============================  2.13 MB / 2.13 MB
Successfully verified installer hash
Starting package install...
Successfully installed
```

## PortableApps

Another option is to use the official [JPEGView on PortableApps](https://portableapps.com/apps/graphics_pictures/jpegview_portable) package.  The PortableApps launcher preserves user settings in a separate directory from the extracted application directory.  This release is signed.

## System Requirements

Windows XP SP2 or later is needed to run the 32 bit version.

64 bit Windows 7 or later is needed to run the 64 bit version.
Mod only tested for >= Win 7 64 bit.

## What's New

* See what has changed in the [latest releases](https://github.com/sylikc/jpegview/releases)
* Or Check the [CHANGELOG.txt](https://github.com/sylikc/jpegview/blob/master/CHANGELOG.txt) to review new features in detail.


# Brief History

This GitHub repo continues the legacy (is a "fork") of the excellent project [JPEGView by David Kleiner](https://sourceforge.net/projects/jpegview/).  Unfortunately, starting in 2020, the SourceForge project has essentially been abandoned, with the last update being [2018-02-24 (1.0.37)](https://sourceforge.net/projects/jpegview/files/jpegview/).  It's an excellent lightweight image viewer that I use almost daily!

The starting point for this repo was a direct clone from SourceForge SVN to GitHub Git.  By continuing this way, it retains all previous commits and all original author comments.  

I'm hoping with this project, some devs might help me keep the project alive!  It's been awhile, and could use some new features or updates.  Looking forward to the community making suggestions, and devs will help with some do pull requests as some of the image code is quite a learning curve for me to pick it up. -sylikc

## Special Thanks

Thanks to [sylikc](https://github.com/sylikc) et al for maintaining JPEGView =D
Special thanks to [qbnu](https://github.com/qbnu) who added Animated WebP and Animated PNG support!
