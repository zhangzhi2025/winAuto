#include "UiaSession.h"

#include <windows.h>
#include <UIAutomation.h>
#include <wrl/client.h>

#include <cstdio>
#include <vector>

#include "Win32Util.h"
#include "core/ControlType.h"

namespace wa::platform {

using Microsoft::WRL::ComPtr;

namespace {

std::string hrText(HRESULT hr) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "HRESULT 0x%08lX", static_cast<unsigned long>(hr));
    return buf;
}

std::string bstrToUtf8(BSTR b) {
    if (!b) return {};
    std::string s = toUtf8(std::wstring(b, SysStringLen(b)));
    SysFreeString(b);
    return s;
}

bool patternAvailable(IUIAutomationElement* e, PROPERTYID prop) {
    VARIANT v;
    VariantInit(&v);
    bool ok = false;
    if (SUCCEEDED(e->GetCachedPropertyValue(prop, &v)) && v.vt == VT_BOOL) ok = v.boolVal != VARIANT_FALSE;
    VariantClear(&v);
    return ok;
}

}  // namespace

struct UiaSession::Impl {
    bool comInitialized = false;
    std::string error;
    ComPtr<IUIAutomation> automation;
    ComPtr<IUIAutomationCacheRequest> cache;
    ComPtr<IUIAutomationTreeWalker> walker;
    std::vector<ComPtr<IUIAutomationElement>> elements;
    std::uint32_t generation = 0;

    IUIAutomationElement* resolve(std::uint64_t handle) const {
        if (static_cast<std::uint32_t>(handle >> 32) != generation) return nullptr;
        const std::uint32_t i = static_cast<std::uint32_t>(handle & 0xFFFFFFFFu);
        if (i >= elements.size()) return nullptr;
        return elements[i].Get();
    }

    // 深度优先读取。显式栈会更省，但控件树深度有限（<= kMaxDepth），递归足够。
    bool walk(IUIAutomationElement* e, int parent, int depth, const CancelToken& token, int maxNodes,
              UiaSnapshot& out) {
        if (token.isCancelled()) return false;
        if (static_cast<int>(out.nodes.size()) >= maxNodes) {
            out.truncated = true;
            return true;
        }
        UiaNode n;
        n.parent = parent;
        n.depth = depth;
        BSTR b = nullptr;
        if (SUCCEEDED(e->get_CachedAutomationId(&b))) n.automationId = bstrToUtf8(b);
        b = nullptr;
        if (SUCCEEDED(e->get_CachedName(&b))) n.name = bstrToUtf8(b);
        b = nullptr;
        if (SUCCEEDED(e->get_CachedClassName(&b))) n.className = bstrToUtf8(b);
        CONTROLTYPEID ct = 0;
        if (SUCCEEDED(e->get_CachedControlType(&ct))) n.controlType = controlTypeName(static_cast<int>(ct));
        RECT r{};
        if (SUCCEEDED(e->get_CachedBoundingRectangle(&r))) {
            n.bounds = Rect{static_cast<int>(r.left), static_cast<int>(r.top),
                            static_cast<int>(r.right - r.left), static_cast<int>(r.bottom - r.top)};
        }
        BOOL flag = FALSE;
        if (SUCCEEDED(e->get_CachedIsEnabled(&flag))) n.enabled = flag != FALSE;
        flag = FALSE;
        if (SUCCEEDED(e->get_CachedIsOffscreen(&flag))) n.offscreen = flag != FALSE;
        n.supportsInvoke = patternAvailable(e, UIA_IsInvokePatternAvailablePropertyId);
        n.supportsValue = patternAvailable(e, UIA_IsValuePatternAvailablePropertyId);

        const int index = static_cast<int>(out.nodes.size());
        n.nativeHandle = (static_cast<std::uint64_t>(generation) << 32) | static_cast<std::uint32_t>(index);
        out.nodes.push_back(std::move(n));
        elements.push_back(ComPtr<IUIAutomationElement>(e));

        if (depth >= kMaxDepth) return true;
        ComPtr<IUIAutomationElement> child;
        if (FAILED(walker->GetFirstChildElementBuildCache(e, cache.Get(), &child)) || !child) return true;
        while (child) {
            if (!walk(child.Get(), index, depth + 1, token, maxNodes, out)) return false;
            if (out.truncated) return true;
            ComPtr<IUIAutomationElement> next;
            if (FAILED(walker->GetNextSiblingElementBuildCache(child.Get(), cache.Get(), &next))) break;
            child = next;
        }
        return true;
    }

    static constexpr int kMaxDepth = 40;
};

UiaSession::UiaSession() : impl_(new Impl) {
    const HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (SUCCEEDED(co)) {
        impl_->comInitialized = true;
    } else if (co != RPC_E_CHANGED_MODE) {
        impl_->error = "CoInitializeEx failed: " + hrText(co);
        return;
    }  // RPC_E_CHANGED_MODE：线程已是 STA，COM 可用，但不由我们卸载

    HRESULT hr = CoCreateInstance(__uuidof(CUIAutomation), nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&impl_->automation));
    if (FAILED(hr)) {
        impl_->error = "cannot create UI Automation: " + hrText(hr);
        return;
    }
    hr = impl_->automation->CreateCacheRequest(&impl_->cache);
    if (FAILED(hr)) {
        impl_->error = "CreateCacheRequest failed: " + hrText(hr);
        return;
    }
    const PROPERTYID props[] = {UIA_AutomationIdPropertyId,   UIA_NamePropertyId,
                                UIA_ControlTypePropertyId,    UIA_ClassNamePropertyId,
                                UIA_BoundingRectanglePropertyId, UIA_IsEnabledPropertyId,
                                UIA_IsOffscreenPropertyId,    UIA_IsInvokePatternAvailablePropertyId,
                                UIA_IsValuePatternAvailablePropertyId};
    for (PROPERTYID p : props) impl_->cache->AddProperty(p);
    hr = impl_->automation->get_ControlViewWalker(&impl_->walker);
    if (FAILED(hr)) impl_->error = "get_ControlViewWalker failed: " + hrText(hr);
}

UiaSession::~UiaSession() {
    // 先释放所有 COM 指针，再卸载 COM
    impl_->elements.clear();
    impl_->walker.Reset();
    impl_->cache.Reset();
    impl_->automation.Reset();
    if (impl_->comInitialized) CoUninitialize();
}

bool UiaSession::available() const { return impl_->error.empty() && impl_->automation && impl_->walker; }
const std::string& UiaSession::initError() const { return impl_->error; }

Result<UiaSnapshot> UiaSession::snapshot(std::uint64_t windowHandle, const CancelToken& token, int maxNodes) {
    using R = Result<UiaSnapshot>;
    if (!available()) return R::failure(ErrCode::Unsupported, "UI Automation unavailable: " + impl_->error);
    HWND h = reinterpret_cast<HWND>(static_cast<UINT_PTR>(windowHandle));
    if (!IsWindow(h)) return R::failure(ErrCode::WindowNotFound, "target window is gone");

    ComPtr<IUIAutomationElement> root;
    HRESULT hr = impl_->automation->ElementFromHandleBuildCache(h, impl_->cache.Get(), &root);
    if (FAILED(hr) || !root) {
        return R::failure(ErrCode::ElementNotFound, "cannot read the window's UI tree: " + hrText(hr));
    }

    impl_->elements.clear();
    ++impl_->generation;
    UiaSnapshot snap;
    if (!impl_->walk(root.Get(), -1, 0, token, maxNodes, snap)) {
        impl_->elements.clear();
        return R::failure(ErrCode::Cancelled);
    }
    return R::success(std::move(snap));
}

Status UiaSession::invoke(std::uint64_t handle) {
    IUIAutomationElement* e = impl_->resolve(handle);
    if (!e) return Status::failure(ErrCode::ElementNotFound, "element reference is stale (UI tree was re-read)");
    ComPtr<IUIAutomationInvokePattern> p;
    HRESULT hr = e->GetCurrentPatternAs(UIA_InvokePatternId, IID_PPV_ARGS(&p));
    if (FAILED(hr) || !p) return Status::failure(ErrCode::Unsupported, "element has no InvokePattern");
    hr = p->Invoke();
    if (FAILED(hr)) return Status::failure(ErrCode::InputBlocked, "Invoke failed: " + hrText(hr));
    return Status::success();
}

Status UiaSession::setValue(std::uint64_t handle, const std::string& text) {
    IUIAutomationElement* e = impl_->resolve(handle);
    if (!e) return Status::failure(ErrCode::ElementNotFound, "element reference is stale (UI tree was re-read)");
    ComPtr<IUIAutomationValuePattern> p;
    HRESULT hr = e->GetCurrentPatternAs(UIA_ValuePatternId, IID_PPV_ARGS(&p));
    if (FAILED(hr) || !p) return Status::failure(ErrCode::Unsupported, "element has no ValuePattern");
    BOOL readOnly = FALSE;
    if (SUCCEEDED(p->get_CurrentIsReadOnly(&readOnly)) && readOnly) {
        return Status::failure(ErrCode::InputBlocked, "element is read-only");
    }
    const std::wstring w = toWide(text);
    BSTR b = SysAllocStringLen(w.data(), static_cast<UINT>(w.size()));
    if (!b && !w.empty()) return Status::failure(ErrCode::Internal, "out of memory");
    hr = p->SetValue(b);
    SysFreeString(b);
    if (FAILED(hr)) return Status::failure(ErrCode::InputBlocked, "SetValue failed: " + hrText(hr));
    return Status::success();
}

Result<size_t> UiaSession::indexAtPoint(Point pt) {
    using R = Result<size_t>;
    if (!available()) return R::failure(ErrCode::Unsupported, "UI Automation unavailable: " + impl_->error);
    POINT p{static_cast<LONG>(pt.x), static_cast<LONG>(pt.y)};
    ComPtr<IUIAutomationElement> hit;
    HRESULT hr = impl_->automation->ElementFromPoint(p, &hit);
    if (FAILED(hr) || !hit) return R::failure(ErrCode::ElementNotFound, "no UI element at that point");
    for (size_t i = 0; i < impl_->elements.size(); ++i) {
        BOOL same = FALSE;
        if (SUCCEEDED(impl_->automation->CompareElements(hit.Get(), impl_->elements[i].Get(), &same)) && same) {
            return R::success(i);
        }
    }
    return R::failure(ErrCode::ElementNotFound,
                      "the element under the cursor is not in the window's control tree (try inspect)");
}

}  // namespace wa::platform
