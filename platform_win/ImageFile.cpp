#include "ImageFile.h"

#include <windows.h>
// WIN32_LEAN_AND_MEAN 下 windows.h 不带 COM 头，WIC 依赖它们
#include <objbase.h>
#include <oleauto.h>
#include <unknwn.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <cstdio>

#include "Win32Util.h"

namespace wa::platform {

using Microsoft::WRL::ComPtr;

namespace {

// 当前线程的 COM 初始化（MTA）。线程已是 STA（RPC_E_CHANGED_MODE）时 COM 仍可用，只是不由我们卸载。
struct ComScope {
    bool owns = false;
    HRESULT hr = S_OK;
    ComScope() {
        hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        owns = SUCCEEDED(hr);
        if (hr == RPC_E_CHANGED_MODE) hr = S_OK;
    }
    ~ComScope() {
        if (owns) CoUninitialize();
    }
};

std::string hr(HRESULT h) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "HRESULT 0x%08lX", static_cast<unsigned long>(h));
    return buf;
}

}  // namespace

Result<Bitmap> loadImage(const std::string& path) {
    using R = Result<Bitmap>;
    ComScope com;
    if (FAILED(com.hr)) return R::failure(ErrCode::Internal, "CoInitializeEx failed: " + hr(com.hr));

    ComPtr<IWICImagingFactory> factory;
    HRESULT h = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    if (FAILED(h)) return R::failure(ErrCode::Internal, "cannot create the WIC factory: " + hr(h));

    ComPtr<IWICBitmapDecoder> decoder;
    const std::wstring wpath = toWide(path);
    h = factory->CreateDecoderFromFilename(wpath.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder);
    if (FAILED(h)) return R::failure(ErrCode::InvalidWorkflow, "cannot decode image \"" + path + "\": " + hr(h));

    ComPtr<IWICBitmapFrameDecode> frame;
    h = decoder->GetFrame(0, &frame);
    if (FAILED(h)) return R::failure(ErrCode::InvalidWorkflow, "image has no frame: " + hr(h));

    ComPtr<IWICFormatConverter> conv;
    h = factory->CreateFormatConverter(&conv);
    if (SUCCEEDED(h)) {
        h = conv->Initialize(frame.Get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0.0,
                             WICBitmapPaletteTypeCustom);
    }
    if (FAILED(h)) return R::failure(ErrCode::InvalidWorkflow, "cannot convert image to BGRA: " + hr(h));

    UINT w = 0, ht = 0;
    h = conv->GetSize(&w, &ht);
    if (FAILED(h) || w == 0 || ht == 0 || w > 16384 || ht > 16384) {
        return R::failure(ErrCode::InvalidWorkflow, "image size is invalid or too large");
    }
    Bitmap b;
    b.w = static_cast<int>(w);
    b.h = static_cast<int>(ht);
    b.bgra.resize(static_cast<size_t>(w) * static_cast<size_t>(ht) * 4);
    h = conv->CopyPixels(nullptr, w * 4, static_cast<UINT>(b.bgra.size()), b.bgra.data());
    if (FAILED(h)) return R::failure(ErrCode::InvalidWorkflow, "cannot read image pixels: " + hr(h));
    for (size_t i = 3; i < b.bgra.size(); i += 4) {
        if (b.bgra[i] != 255) { b.hasTransparency = true; break; }
    }
    return R::success(std::move(b));
}

Status savePng(const std::string& path, const Bitmap& b) {
    ComScope com;
    if (FAILED(com.hr)) return Status::failure(ErrCode::Internal, "CoInitializeEx failed: " + hr(com.hr));
    if (b.w <= 0 || b.h <= 0 || b.bgra.size() != static_cast<size_t>(b.w) * static_cast<size_t>(b.h) * 4) {
        return Status::failure(ErrCode::InvalidWorkflow, "invalid bitmap");
    }
    ComPtr<IWICImagingFactory> factory;
    HRESULT h = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    if (FAILED(h)) return Status::failure(ErrCode::Internal, "cannot create the WIC factory: " + hr(h));

    ComPtr<IWICStream> stream;
    h = factory->CreateStream(&stream);
    if (SUCCEEDED(h)) h = stream->InitializeFromFilename(toWide(path).c_str(), GENERIC_WRITE);
    if (FAILED(h)) return Status::failure(ErrCode::Internal, "cannot open \"" + path + "\" for writing: " + hr(h));

    ComPtr<IWICBitmapEncoder> enc;
    h = factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc);
    if (SUCCEEDED(h)) h = enc->Initialize(stream.Get(), WICBitmapEncoderNoCache);
    ComPtr<IWICBitmapFrameEncode> frame;
    if (SUCCEEDED(h)) h = enc->CreateNewFrame(&frame, nullptr);
    if (SUCCEEDED(h)) h = frame->Initialize(nullptr);
    if (SUCCEEDED(h)) h = frame->SetSize(static_cast<UINT>(b.w), static_cast<UINT>(b.h));
    WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppBGRA;
    if (SUCCEEDED(h)) h = frame->SetPixelFormat(&fmt);
    if (SUCCEEDED(h)) {
        h = frame->WritePixels(static_cast<UINT>(b.h), static_cast<UINT>(b.w) * 4,
                               static_cast<UINT>(b.bgra.size()), const_cast<BYTE*>(b.bgra.data()));
    }
    if (SUCCEEDED(h)) h = frame->Commit();
    if (SUCCEEDED(h)) h = enc->Commit();
    if (FAILED(h)) return Status::failure(ErrCode::Internal, "cannot write the PNG: " + hr(h));
    return okStatus();
}

}  // namespace wa::platform
