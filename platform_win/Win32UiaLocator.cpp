#include "Win32UiaLocator.h"

#include "core/UiaMatch.h"

namespace wa::platform {

Result<ElementRef> Win32UiaLocator::locate(std::uint64_t windowHandle, const LeafLocator& locator,
                                           const CancelToken& token) {
    using R = Result<ElementRef>;
    const auto* u = std::get_if<UiaLocator>(&locator);
    if (!u) return points_.locate(windowHandle, locator, token);

    Result<UiaSnapshot> snap = session_.snapshot(windowHandle, token);
    if (!snap.ok()) return failAs<ElementRef>(snap);

    Result<size_t> idx = pickNode(snap.value, *u);
    if (!idx.ok()) return failAs<ElementRef>(idx);

    const UiaNode& n = snap.value.nodes[idx.value];
    ElementRef e;
    e.bounds = n.bounds;
    e.name = n.name;
    e.controlType = n.controlType;
    e.nativeHandle = n.nativeHandle;
    e.supportsInvoke = n.supportsInvoke;
    e.supportsValue = n.supportsValue;
    e.enabled = n.enabled;
    return R::success(std::move(e));
}

}  // namespace wa::platform
