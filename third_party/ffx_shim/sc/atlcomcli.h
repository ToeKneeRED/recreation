// CComPtr stub: the GLSL-only Linux build never instantiates the DXC/FXC
// paths, the member declarations in hlsl_compiler.h just need to parse.
#ifndef RECREATION_FFX_SHIM_ATLCOMCLI_H_
#define RECREATION_FFX_SHIM_ATLCOMCLI_H_
#ifndef _WIN32

template <typename T>
class CComPtr {
 public:
  T* p = nullptr;
};

#endif  // !_WIN32
#endif  // RECREATION_FFX_SHIM_ATLCOMCLI_H_
