modspectre.lv2 - Crude FFT Spectrum Analyzer
============================================

Install
-------

Compiling modspectre.lv2 requires the LV2 SDK, fftw3f, gnu-make, and a c-compiler.

```bash
  git clone git://github.com/x42/modspectre.lv2.git
  cd modspectre.lv2
  make
  sudo make install PREFIX=/usr
```

To build the the MOD GUI use `make MOD=1`
