TintIcon
========

A lightweight Windows command line tool to retint icons and PNG images by replacing orange hues with DeepSkyBlue. It preserves original shading and transparency in every pixel. It supports both standard ICO and standalone PNG files. It relies solely on the Windows GDI+ library.

Features
--------

*   Supports .ico and .png input and output
    
*   Detects PNG‐encoded and classic DIB ICO images
    
*   Converts only the orange hue range (≈15°–45°) to DeepSkyBlue (≈195°)
    
*   Preserves per-pixel lightness, shadows, highlights, and transparency
    

Prerequisites
-------------

*   Windows 7 or later
    
*   Visual Studio 2015 or newer with C++ workload
    
*   Windows 10 SDK or later
    
*   GDI+ library (gdiplus.lib)
    

Building from Source
--------------------

1.  git clone https://github.com/yourusername/tinticon.gitcd tinticon
    
2.  Open tinticon.sln in Visual Studio.
    
3.  Select Release or Debug configuration for x86 or x64.
    
4.  Build the solution.
    
5.  Locate tinticon.exe in bin/Release or bin/Debug under the platform folder.
    

Usage
-----

`tinticon.exe` Path to the source icon or PNG image as the first argument. Path for the retinted output as the second argument.

Examples:

`   tinticon.exe original.ico tinted.ico
tinticon.exe logo.png logo_tinted.png   `

How It Works
------------

### TintPixels Function

`   inline void TintPixels(      BYTE* pixels,      UINT width,      UINT height,      INT stride);   `

This function scans each pixel in BGRA order. It skips fully transparent pixels. It converts RGB to HSL, replaces hue in the orange range with DeepSkyBlue, and converts back to RGB while preserving alpha.

### PNG Handling

ProcessPNG uses GDI+ Bitmap to lock bits, apply TintPixels directly on the buffer, and save as PNG.

### ICO Handling

ProcessICO reads raw file bytes and parses ICONDIR and ICONDIRENTRY. Each sub-image is loaded via a PNG stream or by creating an HICON and converting it to a GDI+ Bitmap. Every entry is retinted and re-encoded as PNG. The ICO directory is rebuilt and written out.

Roadmap
-------

*   Expose custom hue and threshold parameters via command line flags
    
*   Add preset tints such as teal, turquoise, and coral
    
*   Introduce GPU acceleration for bulk processing
    
*   Package as a NuGet or Chocolatey module for easy installation
    

Contributing
------------

1.  Fork the repository.
    
2.  Create a feature branch.
    
3.  Implement and test your changes.
    
4.  Submit a pull request for review.
    

License
-------

MIT License © 2025 – Your Name

See the LICENSE file for details.
