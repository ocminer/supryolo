// SPDX-License-Identifier: LicenseRef-Supryolo-NC-1.0
#pragma once
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
namespace yolo {
inline void *load_library(const char *,const wchar_t *name) {
  return reinterpret_cast<void *>(LoadLibraryExW(name,nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32));
}
inline void *library_symbol(void *lib,const char *name) { return reinterpret_cast<void *>(GetProcAddress(static_cast<HMODULE>(lib),name)); }
inline void unload_library(void *lib) { FreeLibrary(static_cast<HMODULE>(lib)); }
}
#else
#include <dlfcn.h>
namespace yolo {
inline void *load_library(const char *name,const wchar_t *) { return dlopen(name,RTLD_NOW|RTLD_LOCAL); }
inline void *library_symbol(void *lib,const char *name) { return dlsym(lib,name); }
inline void unload_library(void *lib) { dlclose(lib); }
}
#endif
