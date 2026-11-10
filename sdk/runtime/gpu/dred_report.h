// What Direct3D 12's device removed data says about a lost device, as lines for the log. Without it a lost device leaves one
// line and no way to tell which of the frame's thousands of operations the GPU stopped in.
#pragma once
#include <d3d12.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace gpu {

inline std::string BreadcrumbOpName(D3D12_AUTO_BREADCRUMB_OP op) {
    switch (op) {
        case D3D12_AUTO_BREADCRUMB_OP_SETMARKER: return "SetMarker";
        case D3D12_AUTO_BREADCRUMB_OP_BEGINEVENT: return "BeginEvent";
        case D3D12_AUTO_BREADCRUMB_OP_ENDEVENT: return "EndEvent";
        case D3D12_AUTO_BREADCRUMB_OP_DRAWINSTANCED: return "DrawInstanced";
        case D3D12_AUTO_BREADCRUMB_OP_DRAWINDEXEDINSTANCED: return "DrawIndexedInstanced";
        case D3D12_AUTO_BREADCRUMB_OP_COPYBUFFERREGION: return "CopyBufferRegion";
        case D3D12_AUTO_BREADCRUMB_OP_COPYTEXTUREREGION: return "CopyTextureRegion";
        case D3D12_AUTO_BREADCRUMB_OP_COPYRESOURCE: return "CopyResource";
        case D3D12_AUTO_BREADCRUMB_OP_RESOLVESUBRESOURCE: return "ResolveSubresource";
        case D3D12_AUTO_BREADCRUMB_OP_CLEARRENDERTARGETVIEW: return "ClearRenderTargetView";
        case D3D12_AUTO_BREADCRUMB_OP_CLEARDEPTHSTENCILVIEW: return "ClearDepthStencilView";
        case D3D12_AUTO_BREADCRUMB_OP_RESOURCEBARRIER: return "ResourceBarrier";
        case D3D12_AUTO_BREADCRUMB_OP_PRESENT: return "Present";
        case D3D12_AUTO_BREADCRUMB_OP_RESOLVEQUERYDATA: return "ResolveQueryData";
        case D3D12_AUTO_BREADCRUMB_OP_BEGINSUBMISSION: return "BeginSubmission";
        case D3D12_AUTO_BREADCRUMB_OP_ENDSUBMISSION: return "EndSubmission";
        case D3D12_AUTO_BREADCRUMB_OP_BEGIN_COMMAND_LIST: return "BeginCommandList";
        default: return "op " + std::to_string(int(op));
    }
}

inline const char* DeviceStateName(D3D12_DRED_DEVICE_STATE state) {
    switch (state) {
        case D3D12_DRED_DEVICE_STATE_HUNG: return "hung";
        case D3D12_DRED_DEVICE_STATE_FAULT: return "fault";
        case D3D12_DRED_DEVICE_STATE_PAGEFAULT: return "page fault";
        default: return "unknown";
    }
}

namespace dred_detail {

inline std::string Narrow(const wchar_t* text) {
    std::string out;
    for (; text && *text; text++) out += *text < 0x80 ? char(*text) : '?';
    return out;
}

inline std::string ContextOf(const D3D12_AUTO_BREADCRUMB_NODE1& node, uint32_t index) {
    for (uint32_t i = 0; i < node.BreadcrumbContextsCount; i++) {
        if (node.pBreadcrumbContexts[i].BreadcrumbIndex == index) return Narrow(node.pBreadcrumbContexts[i].pContextString);
    }
    return {};
}

inline std::string NameOf(const D3D12_DRED_ALLOCATION_NODE1& node) {
    if (node.ObjectNameW) return Narrow(node.ObjectNameW);
    if (node.ObjectNameA) return node.ObjectNameA;
    return "(no name) type " + std::to_string(int(node.AllocationType));
}

}  // namespace dred_detail

// the operations around the first one the GPU did not finish, in each command list it stopped inside. The last marker before
// that point says which of the port's calls the operation came from, so the window reaches back further than it reaches ahead.
inline std::vector<std::string> BreadcrumbReport(const D3D12_AUTO_BREADCRUMB_NODE1* head, uint32_t before = 24,
                                                 uint32_t after = 8) {
    std::vector<std::string> lines;
    uint32_t finished = 0;
    uint32_t never_began = 0;
    std::vector<const D3D12_AUTO_BREADCRUMB_NODE1*> stopped;
    for (auto* node = head; node; node = node->pNext) {
        const uint32_t done = node->pLastBreadcrumbValue ? *node->pLastBreadcrumbValue : 0;
        if (done >= node->BreadcrumbCount) {
            finished++;
        } else if (done == 0) {
            never_began++;
        } else {
            stopped.push_back(node);
        }
    }
    if (!head) {
        lines.push_back("no operations recorded");
        return lines;
    }
    lines.push_back("command lists: " + std::to_string(finished) + " finished, " + std::to_string(stopped.size()) +
                    " stopped inside, " + std::to_string(never_began) + " never began");
    for (auto* node : stopped) {
        const uint32_t done = *node->pLastBreadcrumbValue;
        lines.push_back("stopped after " + std::to_string(done) + " of " + std::to_string(node->BreadcrumbCount) + " operations:");
        const uint32_t first = done > before ? done - before : 0;
        const uint32_t last = (std::min)(node->BreadcrumbCount, done + after + 1);
        for (uint32_t i = first; i < last; i++) {
            std::string line = (i == done ? ">> " : "   ") + std::to_string(i) + " " + BreadcrumbOpName(node->pCommandHistory[i]);
            const std::string context = dred_detail::ContextOf(*node, i);
            if (!context.empty()) line += ": " + context;
            lines.push_back(line);
        }
    }
    return lines;
}

inline constexpr size_t kRecentLineSize = 96;

// the last N lines added. No allocation: a line goes in for every target change of every frame
template <size_t N>
class RecentLines {
public:
    void Add(const char* text) {
        auto& line = lines_[size_t(next_ % N)];
        std::snprintf(line.data(), line.size(), "%s", text);
        next_++;
    }
    std::vector<std::string> Lines() const {
        std::vector<std::string> out;
        const uint64_t first = next_ > N ? next_ - N : 0;
        for (uint64_t i = first; i < next_; i++) out.emplace_back(lines_[size_t(i % N)].data());
        return out;
    }

private:
    std::array<std::array<char, kRecentLineSize>, N> lines_{};
    uint64_t next_ = 0;
};

// empty without a page fault
inline std::vector<std::string> PageFaultReport(const D3D12_DRED_PAGE_FAULT_OUTPUT2& fault) {
    std::vector<std::string> lines;
    if (!fault.PageFaultVA) return lines;
    char address[64];
    std::snprintf(address, sizeof(address), "page fault at %016llX", static_cast<unsigned long long>(fault.PageFaultVA));
    lines.emplace_back(address);
    for (auto* node = fault.pHeadExistingAllocationNode; node; node = node->pNext) {
        lines.push_back("alive: " + dred_detail::NameOf(*node));
    }
    for (auto* node = fault.pHeadRecentFreedAllocationNode; node; node = node->pNext) {
        lines.push_back("freed: " + dred_detail::NameOf(*node));
    }
    return lines;
}

}  // namespace gpu
