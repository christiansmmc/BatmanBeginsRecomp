#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <d3d12.h>

#include <string>
#include <vector>

#include "check.h"
#include "gpu/dred_report.h"

namespace {

bool Has(const std::vector<std::string>& lines, const std::string& text) {
    for (const auto& line : lines) {
        if (line.find(text) != std::string::npos) return true;
    }
    return false;
}

}  // namespace

int main() {
    // three command lists in the order the GPU met them: one done, one it stopped in, one it never began
    D3D12_AUTO_BREADCRUMB_OP done_ops[] = {D3D12_AUTO_BREADCRUMB_OP_DRAWINSTANCED, D3D12_AUTO_BREADCRUMB_OP_RESOURCEBARRIER};
    UINT32 done_value = 2;
    D3D12_AUTO_BREADCRUMB_OP stuck_ops[] = {D3D12_AUTO_BREADCRUMB_OP_SETMARKER,     D3D12_AUTO_BREADCRUMB_OP_RESOURCEBARRIER,
                                            D3D12_AUTO_BREADCRUMB_OP_COPYTEXTUREREGION, D3D12_AUTO_BREADCRUMB_OP_DRAWINSTANCED};
    UINT32 stuck_value = 2;
    D3D12_DRED_BREADCRUMB_CONTEXT stuck_context[] = {{0, L"copy 00123000 to 00456000"}};
    D3D12_AUTO_BREADCRUMB_OP unstarted_ops[] = {D3D12_AUTO_BREADCRUMB_OP_DRAWINSTANCED};
    UINT32 unstarted_value = 0;

    D3D12_AUTO_BREADCRUMB_NODE1 unstarted{};
    unstarted.BreadcrumbCount = 1;
    unstarted.pLastBreadcrumbValue = &unstarted_value;
    unstarted.pCommandHistory = unstarted_ops;
    D3D12_AUTO_BREADCRUMB_NODE1 stuck{};
    stuck.BreadcrumbCount = 4;
    stuck.pLastBreadcrumbValue = &stuck_value;
    stuck.pCommandHistory = stuck_ops;
    stuck.BreadcrumbContextsCount = 1;
    stuck.pBreadcrumbContexts = stuck_context;
    stuck.pNext = &unstarted;
    D3D12_AUTO_BREADCRUMB_NODE1 done{};
    done.BreadcrumbCount = 2;
    done.pLastBreadcrumbValue = &done_value;
    done.pCommandHistory = done_ops;
    done.pNext = &stuck;

    const std::vector<std::string> report = gpu::BreadcrumbReport(&done);
    CHECK(Has(report, "1 finished"));
    CHECK(Has(report, "1 never began"));
    // the first operation the GPU did not finish is marked, among those around it
    CHECK(Has(report, ">> 2 CopyTextureRegion"));
    CHECK(Has(report, "1 ResourceBarrier"));
    CHECK(Has(report, "3 DrawInstanced"));
    // what the port wrote beside an operation is kept, or the operation cannot be told from any other
    CHECK(Has(report, "copy 00123000 to 00456000"));

    // a list that never began says nothing about the fault, but is what is left to read when no list began
    const std::vector<std::string> none = gpu::BreadcrumbReport(&unstarted);
    CHECK(Has(none, "1 never began"));
    CHECK(!Has(none, ">>"));

    // nothing recorded is said, and is not an empty report that reads as nothing wrong
    CHECK(Has(gpu::BreadcrumbReport(nullptr), "no operations recorded"));

    // an operation newer than this table still gets a line
    CHECK(gpu::BreadcrumbOpName(D3D12_AUTO_BREADCRUMB_OP(100000)) == std::string("op 100000"));

    // a page fault gives the address and what the address belonged to, alive or freed
    D3D12_DRED_ALLOCATION_NODE1 freed{};
    freed.ObjectNameA = "target 00ABC000";
    freed.AllocationType = D3D12_DRED_ALLOCATION_TYPE_RESOURCE;
    D3D12_DRED_ALLOCATION_NODE1 alive{};
    alive.ObjectNameW = L"texture 00DEF000";
    alive.AllocationType = D3D12_DRED_ALLOCATION_TYPE_RESOURCE;
    D3D12_DRED_PAGE_FAULT_OUTPUT2 fault{};
    fault.PageFaultVA = 0x1234ABCD;
    fault.pHeadExistingAllocationNode = &alive;
    fault.pHeadRecentFreedAllocationNode = &freed;
    const std::vector<std::string> faulted = gpu::PageFaultReport(fault);
    CHECK(Has(faulted, "1234ABCD"));
    CHECK(Has(faulted, "alive: texture 00DEF000"));
    CHECK(Has(faulted, "freed: target 00ABC000"));

    // no address means no page fault: nothing to say
    CHECK(gpu::PageFaultReport(D3D12_DRED_PAGE_FAULT_OUTPUT2{}).empty());

    CHECK(gpu::DeviceStateName(D3D12_DRED_DEVICE_STATE_PAGEFAULT) == std::string("page fault"));
    CHECK(gpu::DeviceStateName(D3D12_DRED_DEVICE_STATE_HUNG) == std::string("hung"));
    CHECK(gpu::DeviceStateName(D3D12_DRED_DEVICE_STATE_FAULT) == std::string("fault"));

    // the last things the port did, oldest first: the GPU runs behind them, and a report of the last one alone would miss it
    gpu::RecentLines<3> recent;
    CHECK(recent.Lines().empty());
    recent.Add("one");
    recent.Add("two");
    CHECK(recent.Lines() == (std::vector<std::string>{"one", "two"}));
    recent.Add("three");
    recent.Add("four");
    CHECK(recent.Lines() == (std::vector<std::string>{"two", "three", "four"}));
    // a line too long for its slot is cut, and does not run into the next
    gpu::RecentLines<2> cut;
    cut.Add(std::string(500, 'x').c_str());
    cut.Add("next");
    CHECK(cut.Lines()[0] == std::string(gpu::kRecentLineSize - 1, 'x'));
    CHECK(cut.Lines()[1] == "next");
    return g_failures;
}
