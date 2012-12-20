//
// Copyright (c) 2012 The ANGLE Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//

// Renderer.cpp: Implements EGL dependencies for creating and destroying Renderer instances.

#include "libGLESv2/main.h"
#include "libGLESv2/Program.h"
#include "libGLESv2/renderer/Renderer.h"
#include "libGLESv2/renderer/Renderer9.h"
#include "libGLESv2/renderer/Renderer11.h"

#if !defined(ANGLE_ENABLE_D3D11)
// Enables use of the Direct3D 11 API, when available
#define ANGLE_ENABLE_D3D11 0
#endif

#if !defined(ANGLE_COMPILE_OPTIMIZATION_LEVEL)
#define ANGLE_COMPILE_OPTIMIZATION_LEVEL D3DCOMPILE_OPTIMIZATION_LEVEL3
#endif

namespace rx
{

Renderer::Renderer(egl::Display *display) : mDisplay(display)
{
    mD3dCompilerModule = NULL;
    mD3DCompileFunc = NULL;
}

Renderer::~Renderer()
{
    if (mD3dCompilerModule)
    {
        FreeLibrary(mD3dCompilerModule);
        mD3dCompilerModule = NULL;
    }
}

bool Renderer::initializeCompiler()
{
#if defined(ANGLE_PRELOADED_D3DCOMPILER_MODULE_NAMES)
    // Find a D3DCompiler module that had already been loaded based on a predefined list of versions.
    static TCHAR* d3dCompilerNames[] = ANGLE_PRELOADED_D3DCOMPILER_MODULE_NAMES;

    for (int i = 0; i < sizeof(d3dCompilerNames) / sizeof(*d3dCompilerNames); ++i)
    {
        if (GetModuleHandleEx(0, d3dCompilerNames[i], &mD3dCompilerModule))
        {
            break;
        }
    }
#else
    // Load the version of the D3DCompiler DLL associated with the Direct3D version ANGLE was built with.
    mD3dCompilerModule = LoadLibrary(D3DCOMPILER_DLL);
#endif  // ANGLE_PRELOADED_D3DCOMPILER_MODULE_NAMES

    if (!mD3dCompilerModule)
    {
        ERR("No D3D compiler module found - aborting!\n");
        return false;
    }

    mD3DCompileFunc = reinterpret_cast<pD3DCompile>(GetProcAddress(mD3dCompilerModule, "D3DCompile"));
    ASSERT(mD3DCompileFunc);

    return mD3DCompileFunc != NULL;
}

// Compiles HLSL code into executable binaries
ID3DBlob *Renderer::compileToBinary(gl::InfoLog &infoLog, const char *hlsl, const char *profile, bool alternateFlags)
{
    if (!hlsl)
    {
        return NULL;
    }

    HRESULT result = S_OK;
    UINT flags = D3DCOMPILE_ENABLE_BACKWARDS_COMPATIBILITY;   // Enables using legacy sampler syntax for Shader Model 4+
    std::string sourceText;
    if (gl::perfActive())
    {
        flags |= D3DCOMPILE_DEBUG;
#ifdef NDEBUG
        flags |= ANGLE_COMPILE_OPTIMIZATION_LEVEL;
#else
        flags |= D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

        std::string sourcePath = getTempPath();
        sourceText = std::string("#line 2 \"") + sourcePath + std::string("\"\n\n") + std::string(hlsl);
        writeFile(sourcePath.c_str(), sourceText.c_str(), sourceText.size());
    }
    else
    {
        flags |= ANGLE_COMPILE_OPTIMIZATION_LEVEL;
        sourceText = hlsl;
    }

    // Sometimes D3DCompile will fail with the default compilation flags for complicated shaders when it would otherwise pass with alternative options.
    // Try the default flags first and if compilation fails, try some alternatives.
    const static UINT extraFlags[] =
    {
        0,
        D3DCOMPILE_AVOID_FLOW_CONTROL,
        D3DCOMPILE_PREFER_FLOW_CONTROL
    };

    const static char * const extraFlagNames[] =
    {
        "default",
        "avoid flow control",
        "prefer flow control"
    };

    int attempts = (alternateFlags ? sizeof(extraFlags) / sizeof(UINT) : 1);

    for (int i = 0; i < attempts; ++i)
    {
        ID3DBlob *errorMessage = NULL;
        ID3DBlob *binary = NULL;
        result = mD3DCompileFunc(hlsl, strlen(hlsl), gl::g_fakepath, NULL, NULL,
                                 "main", profile, flags | extraFlags[i], 0, &binary, &errorMessage);
        if (errorMessage)
        {
            const char *message = (const char*)errorMessage->GetBufferPointer();

            infoLog.appendSanitized(message);
            TRACE("\n%s", hlsl);
            TRACE("\n%s", message);

            errorMessage->Release();
            errorMessage = NULL;
        }

        if (SUCCEEDED(result))
        {
            return binary;
        }
        else
        {
            if (result == D3DERR_OUTOFVIDEOMEMORY || result == E_OUTOFMEMORY)
            {
                return error(GL_OUT_OF_MEMORY, (ID3DBlob*) NULL);
            }

            infoLog.append("Warning: D3D shader compilation failed with ");
            infoLog.append(extraFlagNames[i]);
            infoLog.append(" flags.");
            if (i + 1 < attempts)
            {
                infoLog.append(" Retrying with ");
                infoLog.append(extraFlagNames[i + 1]);
                infoLog.append(".\n");
            }
        }
    }

    return NULL;
}

}

extern "C"
{

rx::Renderer *glCreateRenderer(egl::Display *display, HDC hDc, bool softwareDevice)
{
    rx::Renderer *renderer = NULL;
    EGLint status = EGL_BAD_ALLOC;
    
    if (ANGLE_ENABLE_D3D11)
    {
        renderer = new rx::Renderer11(display, hDc);
    
        if (renderer)
        {
            status = renderer->initialize();
        }

        if (status == EGL_SUCCESS)
        {
            return renderer;
        }

        // Failed to create a D3D11 renderer, try creating a D3D9 renderer
        delete renderer;
    }

    renderer = new rx::Renderer9(display, hDc, softwareDevice);
    
    if (renderer)
    {
        status = renderer->initialize();
    }

    if (status == EGL_SUCCESS)
    {
        return renderer;
    }

    return NULL;
}

void glDestroyRenderer(rx::Renderer *renderer)
{
    delete renderer;
}

}