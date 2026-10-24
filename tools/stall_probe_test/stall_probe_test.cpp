// Tests for patch 20 (stall probe): the WAIT_REG_MEM tracker and the main-thread sampler's
// platform-independent logic. Not part of the emulator build; see README.md.

#include "graphics/guest_gpu/command_processor/waitRegMemProbe.h"
#include "kernel/mainThreadSampler.h"
#include "kernel/stallProbe.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <mutex>
#include <thread>
#include <random>
#include <string>
#include <vector>

namespace {

int g_failures = 0;

#define CHECK(cond)                                                                                \
	do {                                                                                           \
		if (!(cond)) {                                                                             \
			std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                            \
			g_failures++;                                                                          \
		}                                                                                          \
	} while (false)

namespace Probe = Libs::Graphics::WaitRegMemProbe;
namespace Sampler = Libs::MainThreadSampler::Detail;
namespace FP      = Libs::Graphics::FrameProgress;
using Libs::StallProbe::RecentLongWaitAddresses;

void ResetAll() {
	g_log.clear();
	Probe::Detail::ResetForTest();
	Libs::StallProbe::ClearRecentForTest();
	Libs::StallProbe::SetEnabledForTest(true);
	FP::Detail::SetEnabledForTest(true);
	for (auto& q: FP::Detail::g_queues) {
		q.current_submit_id.store(0);
	}
}

size_t CountLines(const std::string& needle) {
	size_t n = 0;
	for (const auto& line: g_log) {
		n += line.find(needle) != std::string::npos ? 1 : 0;
	}
	return n;
}

const std::string* FindLine(const std::string& needle) {
	for (const auto& line: g_log) {
		if (line.find(needle) != std::string::npos) {
			return &line;
		}
	}
	return nullptr;
}

bool LineHas(const std::string* line, const std::string& text) {
	return line != nullptr && line->find(text) != std::string::npos;
}

Probe::Wait W32(uint64_t addr, uint32_t ref, uint32_t func = 3, uint64_t submit = 77) {
	Probe::Wait w;
	w.addr       = addr;
	w.ref        = ref;
	w.mask       = 0xffffffffu;
	w.func       = func;
	w.size_bytes = 4;
	w.poll       = 10;
	w.wait_op    = 0;
	w.submit_id  = submit;
	return w;
}

constexpr uint64_t Ms = 1000;

// --- WAIT_REG_MEM tracker ------------------------------------------------------------------

void TestDisabledLogsNothing() {
	ResetAll();
	Libs::StallProbe::SetEnabledForTest(false);
	Probe::Tracker t;
	for (int i = 0; i < 1000; i++) {
		t.Observe(W32(0x2011880360, 0x5f), 0, false);
	}
	CHECK(g_log.empty());
	CHECK(!t.Active());
	uint64_t addrs[8];
	CHECK(RecentLongWaitAddresses(addrs, 8) == 0);
}

void TestEnvironmentSwitch() {
	Libs::StallProbe::ResetEnabledForTest();
	setenv("KYTY_STALL_PROBE", "0", 1);
	CHECK(!Libs::StallProbe::Enabled());
	Libs::StallProbe::ResetEnabledForTest();
	setenv("KYTY_STALL_PROBE", "1", 1);
	CHECK(Libs::StallProbe::Enabled());
	Libs::StallProbe::ResetEnabledForTest();
	unsetenv("KYTY_STALL_PROBE");
	CHECK(Libs::StallProbe::Enabled());
}

void TestShortWaitIsSilent() {
	ResetAll();
	Probe::Tracker t;
	t.ObserveAt(W32(0x2011880360, 0x5f), 0, false, 0);
	t.ObserveAt(W32(0x2011880360, 0x5f), 0x10, false, 100 * Ms);
	t.ObserveAt(W32(0x2011880360, 0x5f), 0x5f, true, 150 * Ms);
	CHECK(g_log.empty());
	CHECK(!t.Active());
	uint64_t addrs[8];
	CHECK(RecentLongWaitAddresses(addrs, 8) == 0);
	// A satisfied wait that never waited does nothing at all.
	t.ObserveAt(W32(0x2011880360, 0x5f), 0x5f, true, 200 * Ms);
	CHECK(g_log.empty());
	CHECK(!t.Active());
}

void TestLongWaitFullDetail() {
	ResetAll();
	FP::NoteSubmissionStart(2, 77);
	Probe::Tracker t;
	const auto     w = W32(0x0000002011880360ull, 0x5f);
	t.ObserveAt(w, 0, false, 0);
	t.ObserveAt(w, 0, false, 100 * Ms);
	CHECK(g_log.empty());
	t.ObserveAt(w, 0, false, 250 * Ms);
	const auto* begin = FindLine("wait=1 begin");
	CHECK(begin != nullptr);
	CHECK(LineHas(begin, "addr=0x0000002011880360"));
	CHECK(LineHas(begin, "bits=32"));
	CHECK(LineHas(begin, "condition=(value&mask)==ref"));
	CHECK(LineHas(begin, "func=3"));
	CHECK(LineHas(begin, "ref=0x0000005f"));
	CHECK(LineHas(begin, "mask=0xffffffff"));
	CHECK(LineHas(begin, "value=0x00000000"));
	CHECK(LineHas(begin, "first_value=0x00000000"));
	CHECK(LineHas(begin, "queue=2"));
	CHECK(LineHas(begin, "submit=77"));
	CHECK(LineHas(begin, "poll=10"));
	CHECK(LineHas(begin, "waited_ms=250"));
	CHECK(LineHas(begin, "evaluations=3"));
	uint64_t addrs[8];
	CHECK(RecentLongWaitAddresses(addrs, 8) == 1 && addrs[0] == 0x0000002011880360ull);

	t.ObserveAt(w, 0, false, 1200 * Ms);
	CHECK(CountLines("wait=1 still_waiting") == 1);
	t.ObserveAt(w, 0x10, false, 2000 * Ms);
	const auto* change = FindLine("wait=1 value_change");
	CHECK(LineHas(change, "value=0x00000010"));
	CHECK(LineHas(change, "previous=0x00000000"));
	t.ObserveAt(w, 0x10, false, 5500 * Ms);
	CHECK(CountLines("wait=1 still_waiting") == 2);
	CHECK(LineHas(&g_log.back(), "value=0x00000010"));
	t.ObserveAt(w, 0x5f, true, 12300 * Ms);
	const auto* end = FindLine("wait=1 end");
	CHECK(LineHas(end, "result=satisfied"));
	CHECK(LineHas(end, "waited_ms=12300"));
	CHECK(LineHas(end, "evaluations=7"));
	CHECK(LineHas(end, "changes=2"));
	CHECK(LineHas(end, "first_value=0x00000000"));
	CHECK(LineHas(end, "final_value=0x0000005f"));
	CHECK(!t.Active());
	CHECK(g_log.size() == 6); // begin, still, change, still, change, end
}

void Test64BitValues() {
	ResetAll();
	Probe::Tracker t;
	Probe::Wait    w;
	w.addr       = 0x00007ff012345678ull;
	w.ref        = 0x0000000100000000ull;
	w.mask       = 0xffffffffffffffffull;
	w.func       = 5;
	w.size_bytes = 8;
	w.submit_id  = 9;
	t.ObserveAt(w, 0xffffffffull, false, 0);
	t.ObserveAt(w, 0xffffffffull, false, 300 * Ms);
	const auto* begin = FindLine("begin");
	CHECK(LineHas(begin, "addr=0x00007ff012345678"));
	CHECK(LineHas(begin, "bits=64"));
	CHECK(LineHas(begin, "condition=(value&mask)>=ref"));
	CHECK(LineHas(begin, "ref=0x0000000100000000"));
	CHECK(LineHas(begin, "value=0x00000000ffffffff"));
	CHECK(LineHas(begin, "queue=-1")); // submission not started on any queue here
}

void TestCompareOperators() {
	CHECK(std::string(Probe::CompareOperator(0)) == "always");
	CHECK(std::string(Probe::CompareOperator(1)) == "<");
	CHECK(std::string(Probe::CompareOperator(2)) == "<=");
	CHECK(std::string(Probe::CompareOperator(3)) == "==");
	CHECK(std::string(Probe::CompareOperator(4)) == "!=");
	CHECK(std::string(Probe::CompareOperator(5)) == ">=");
	CHECK(std::string(Probe::CompareOperator(6)) == ">");
	CHECK(std::string(Probe::CompareOperator(7)) == "?");
}

void TestSupersededAndNewSubmission() {
	ResetAll();
	Probe::Tracker t;
	t.ObserveAt(W32(0x1000, 1), 0, false, 0);
	t.ObserveAt(W32(0x1000, 1), 0, false, 300 * Ms);
	CHECK(CountLines("wait=1 begin") == 1);
	// Same packet in a later submission: a new wait.
	t.ObserveAt(W32(0x1000, 1, 3, 78), 0, false, 400 * Ms);
	CHECK(LineHas(FindLine("wait=1 end"), "result=superseded"));
	CHECK(t.Active());
	t.ObserveAt(W32(0x1000, 1, 3, 78), 0, false, 700 * Ms);
	CHECK(LineHas(FindLine("wait=2 begin"), "submit=78"));
	// A different packet that passes at once ends the old one and is not tracked.
	t.ObserveAt(W32(0x2000, 5), 5, true, 800 * Ms);
	CHECK(LineHas(FindLine("wait=2 end"), "result=superseded"));
	CHECK(!t.Active());
}

void TestLongWaitBudget() {
	ResetAll();
	Probe::Tracker t;
	uint64_t       now = 0;
	for (uint32_t i = 0; i < Probe::MaxLongWaits + 6; i++) {
		const auto w = W32(0x1000 + i * 4, 1);
		t.ObserveAt(w, 0, false, now);
		t.ObserveAt(w, 0, false, now + 300 * Ms);
		t.ObserveAt(w, 1, true, now + 400 * Ms);
		now += Ms * 1000;
	}
	CHECK(CountLines(" begin ") == Probe::MaxLongWaits);
	CHECK(CountLines(" end ") == Probe::MaxLongWaits);
	CHECK(CountLines("long waits logged; later ones are not") == 1);
	// Every long wait is still published, logged or not.
	uint64_t addrs[8];
	CHECK(RecentLongWaitAddresses(addrs, 8) == 8);
}

void TestValueChangeCaps() {
	ResetAll();
	Probe::Tracker t;
	const auto     w = W32(0x3000, 1000);
	t.ObserveAt(w, 0, false, 0);
	t.ObserveAt(w, 0, false, 250 * Ms);
	for (uint32_t i = 1; i <= 40; i++) {
		t.ObserveAt(w, i, false, 250 * Ms + i);
	}
	CHECK(CountLines("value_change") == Probe::MaxChangesPerWait);
	t.ObserveAt(w, 1000, true, 400 * Ms);
	CHECK(LineHas(FindLine(" end "), "changes=41"));
	// The run-wide cap.
	ResetAll();
	for (uint32_t k = 0; k < 20; k++) {
		Probe::Tracker t2;
		const auto     w2 = W32(0x4000 + k * 8, 1000);
		t2.ObserveAt(w2, 0, false, 0);
		t2.ObserveAt(w2, 0, false, 250 * Ms);
		for (uint32_t i = 1; i <= 20; i++) {
			t2.ObserveAt(w2, i, false, 250 * Ms + i);
		}
	}
	CHECK(CountLines("value_change") == Probe::MaxChangeLines);
}

void TestMilestonesSkippedTogether() {
	ResetAll();
	Probe::Tracker t;
	const auto     w = W32(0x5000, 1);
	t.ObserveAt(w, 0, false, 0);
	t.ObserveAt(w, 0, false, 300 * Ms);
	t.ObserveAt(w, 0, false, 40000 * Ms); // passes 1 s, 5 s and 30 s in one poll
	CHECK(CountLines("still_waiting") == 1);
	t.ObserveAt(w, 0, false, 60000 * Ms);
	CHECK(CountLines("still_waiting") == 1);
	t.ObserveAt(w, 0, false, 121000 * Ms);
	CHECK(CountLines("still_waiting") == 2);
	t.ObserveAt(w, 0, false, 700000 * Ms);
	t.ObserveAt(w, 0, false, 900000 * Ms);
	CHECK(CountLines("still_waiting") == 3); // 600 s is the last milestone
}

void TestQueueUnknownWhenFrameTraceOff() {
	ResetAll();
	FP::NoteSubmissionStart(1, 77);
	FP::Detail::SetEnabledForTest(false);
	Probe::Tracker t;
	t.ObserveAt(W32(0x6000, 1), 0, false, 0);
	t.ObserveAt(W32(0x6000, 1), 0, false, 300 * Ms);
	CHECK(LineHas(FindLine("begin"), "queue=-1"));
	FP::Detail::SetEnabledForTest(true);
}

// --- Sampler logic -------------------------------------------------------------------------

void TestClassifyHost() {
	using Sampler::ClassifyHost;
	using Sampler::Kind;
	CHECK(ClassifyHost(true, "kyty_emulator.exe", nullptr) == Kind::Emulator);
	CHECK(ClassifyHost(false, "ntdll.dll", "NtWaitForSingleObject") == Kind::OsWait);
	CHECK(ClassifyHost(false, "NTDLL.DLL", "ZwWaitForMultipleObjects") == Kind::OsWait);
	CHECK(ClassifyHost(false, "ntdll.dll", "NtWaitForAlertByThreadId") == Kind::OsWait);
	CHECK(ClassifyHost(false, "ntdll.dll", "NtDelayExecution") == Kind::OsWait);
	CHECK(ClassifyHost(false, "ntdll.dll", "NtRemoveIoCompletionEx") == Kind::OsWait);
	CHECK(ClassifyHost(false, "win32u.dll", "NtUserMsgWaitForMultipleObjectsEx") == Kind::OsWait);
	CHECK(ClassifyHost(false, "ntdll.dll", "NtYieldExecution") == Kind::OsYield);
	CHECK(ClassifyHost(false, "ntdll.dll", "RtlAcquireSRWLockExclusive") == Kind::HostOther);
	CHECK(ClassifyHost(false, "ntdll.dll", "NtQueryInformationThread") == Kind::HostOther);
	CHECK(ClassifyHost(false, "kernelbase.dll", "NtWaitForSingleObject") == Kind::HostOther);
	CHECK(ClassifyHost(false, "vulkan-1.dll", "vkQueueSubmit") == Kind::HostOther);
	CHECK(ClassifyHost(false, "ntdll.dll", nullptr) == Kind::HostOther);
}

void Bytes7(uint8_t out[7], std::initializer_list<int> tail) {
	// Fills the last bytes with `tail`, the rest with int3 padding.
	for (int i = 0; i < 7; i++) {
		out[i] = 0xCC;
	}
	int i = 7 - static_cast<int>(tail.size());
	for (int b: tail) {
		out[i++] = static_cast<uint8_t>(b);
	}
}

void TestDecodeCallBefore() {
	using Sampler::CallForm;
	using Sampler::DecodeCallBefore;
	uint8_t       b[7];
	const uint64_t ret = 0x900001000ull;
	Bytes7(b, {0xE8, 0x10, 0x00, 0x00, 0x00}); // call +0x10
	auto site = DecodeCallBefore(b, ret);
	CHECK(site.form == CallForm::Rel32 && site.value == ret + 0x10);
	Bytes7(b, {0xE8, 0xF0, 0xFF, 0xFF, 0xFF}); // call -0x10
	site = DecodeCallBefore(b, ret);
	CHECK(site.form == CallForm::Rel32 && site.value == ret - 0x10);
	Bytes7(b, {0xFF, 0x15, 0x00, 0x01, 0x00, 0x00}); // call [rip+0x100]
	site = DecodeCallBefore(b, ret);
	CHECK(site.form == CallForm::RipIndirect && site.value == ret + 0x100);
	Bytes7(b, {0xFF, 0xD0}); // call rax
	CHECK(DecodeCallBefore(b, ret).form == CallForm::Indirect);
	Bytes7(b, {0x41, 0xFF, 0xD3}); // call r11
	CHECK(DecodeCallBefore(b, ret).form == CallForm::Indirect);
	Bytes7(b, {0xFF, 0x10}); // call [rax]
	CHECK(DecodeCallBefore(b, ret).form == CallForm::Indirect);
	Bytes7(b, {0xFF, 0x50, 0x08}); // call [rax+8]
	CHECK(DecodeCallBefore(b, ret).form == CallForm::Indirect);
	Bytes7(b, {0xFF, 0x54, 0x24, 0x08}); // call [rsp+8]
	CHECK(DecodeCallBefore(b, ret).form == CallForm::Indirect);
	Bytes7(b, {0xFF, 0x90, 0x00, 0x01, 0x00, 0x00}); // call [rax+0x100]
	CHECK(DecodeCallBefore(b, ret).form == CallForm::Indirect);
	Bytes7(b, {0xFF, 0x94, 0x24, 0x00, 0x01, 0x00, 0x00}); // call [rsp+0x100]
	CHECK(DecodeCallBefore(b, ret).form == CallForm::Indirect);
	Bytes7(b, {}); // padding
	CHECK(DecodeCallBefore(b, ret).form == CallForm::None);
	Bytes7(b, {0x48, 0x8B, 0x45, 0xF8}); // mov rax,[rbp-8]
	CHECK(DecodeCallBefore(b, ret).form == CallForm::None);
	Bytes7(b, {0xFF, 0xE0}); // jmp rax: reg field 4, not a call
	CHECK(DecodeCallBefore(b, ret).form == CallForm::None);
	Bytes7(b, {0xFF, 0x25, 0, 0, 0, 0}); // jmp [rip]: not a call
	CHECK(DecodeCallBefore(b, ret).form == CallForm::None);
}

// A fake address space for the stack-scan tests.
struct FakeMemory {
	std::map<uint64_t, uint8_t> bytes;
	void Put(uint64_t addr, std::initializer_list<int> data) {
		for (int b: data) {
			bytes[addr++] = static_cast<uint8_t>(b);
		}
	}
	void Put64(uint64_t addr, uint64_t value) {
		for (int i = 0; i < 8; i++) {
			bytes[addr + i] = static_cast<uint8_t>(value >> (8 * i));
		}
	}
	bool Read(uint64_t addr, void* out, size_t size) const {
		auto* p = static_cast<uint8_t*>(out);
		for (size_t i = 0; i < size; i++) {
			auto it = bytes.find(addr + i);
			if (it == bytes.end()) {
				return false;
			}
			p[i] = it->second;
		}
		return true;
	}
};

void TestStackScanAndCallee() {
	using namespace Sampler;
	const std::vector<GuestRange> guests = {
	    {"eboot.bin", 0x900000000ull, 0x900800000ull, 0x900900000ull},
	    {"libc.prx", 0x910000000ull, 0x910100000ull, 0x910100000ull},
	};
	bool stub = false;
	CHECK(FindGuest(guests, 0x900001234ull, &stub) == &guests[0] && !stub);
	CHECK(FindGuest(guests, 0x900850000ull, &stub) == &guests[0] && stub);
	CHECK(FindGuest(guests, 0x910000010ull, &stub) == &guests[1] && !stub);
	CHECK(FindGuest(guests, 0x7ff600001000ull, &stub) == nullptr);

	FakeMemory mem;
	// Return address 1: after `call plt_stub`, the PLT stub jumps through a slot to an HLE
	// function at a host address.
	const uint64_t ret1 = 0x900002005ull;
	const uint64_t plt  = 0x900700000ull;
	const uint64_t slot = 0x900780000ull;
	const uint64_t hle  = 0x7ff612345670ull;
	const auto     rel1 = static_cast<uint32_t>(plt - ret1);
	mem.Put(ret1 - 7, {0x90, 0x90, 0xE8, int(rel1 & 0xff), int((rel1 >> 8) & 0xff),
	                   int((rel1 >> 16) & 0xff), int((rel1 >> 24) & 0xff)});
	const auto disp = static_cast<uint32_t>(slot - (plt + 6));
	mem.Put(plt, {0xFF, 0x25, int(disp & 0xff), int((disp >> 8) & 0xff), int((disp >> 16) & 0xff),
	              int((disp >> 24) & 0xff)});
	mem.Put64(slot, hle);
	// A guest data pointer that is not preceded by a call.
	const uint64_t data = 0x900600010ull;
	mem.Put(data - 7, {0, 0, 0, 0, 0, 0, 0});
	// Return address 2: after `call [rip+disp]` into libc.
	const uint64_t ret2  = 0x910000106ull;
	const uint64_t slot2 = 0x910000200ull;
	const auto     d2    = static_cast<uint32_t>(slot2 - ret2);
	mem.Put(ret2 - 7, {0x90, 0xFF, 0x15, int(d2 & 0xff), int((d2 >> 8) & 0xff),
	                   int((d2 >> 16) & 0xff), int((d2 >> 24) & 0xff)});
	mem.Put64(slot2, 0x900003000ull);
	// An address in the emulator-generated part of the mapping, even one that follows bytes
	// shaped like a call, is not taken for a guest return address.
	mem.Put(0x900850000ull - 7, {0x90, 0x90, 0xE8, 0, 0, 0, 0});
	// Return address 3: after `call rax`.
	const uint64_t ret3 = 0x900004002ull;
	mem.Put(ret3 - 7, {0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xFF, 0xD0});

	std::vector<uint64_t> words = {0x7ff600001000ull, 0x2a, data,  ret1, 0x900850000ull /*stub*/,
	                               ret2,              0,    ret3, ret1};
	std::vector<uint8_t> stack(words.size() * 8);
	std::memcpy(stack.data(), words.data(), stack.size());
	const auto read = [&](uint64_t a, void* o, size_t n) { return mem.Read(a, o, n); };

	uint64_t rets[3] = {};
	CallSite sites[3];
	const uint32_t n = ScanGuestReturnAddresses(stack.data(), stack.size(), guests, read, rets, sites, 3);
	CHECK(n == 3);
	CHECK(rets[0] == ret1 && sites[0].form == CallForm::Rel32 && sites[0].value == plt);
	CHECK(rets[1] == ret2 && sites[1].form == CallForm::RipIndirect && sites[1].value == slot2);
	CHECK(rets[2] == ret3 && sites[2].form == CallForm::Indirect);
	CHECK(ResolveCallee(sites[0], guests, read) == hle);            // through the PLT
	CHECK(ResolveCallee(sites[1], guests, read) == 0x900003000ull); // through the pointer
	CHECK(ResolveCallee(sites[2], guests, read) == 0);
	// A direct call to a guest function that is not a PLT entry resolves to that function.
	CallSite direct {CallForm::Rel32, 0x900005000ull};
	mem.Put(0x900005000ull, {0x55, 0x48, 0x89, 0xE5, 0x90, 0x90});
	CHECK(ResolveCallee(direct, guests, read) == 0x900005000ull);
	// Capacity respected, and a truncated stack is fine.
	CHECK(ScanGuestReturnAddresses(stack.data(), stack.size(), guests, read, rets, sites, 1) == 1);
	CHECK(ScanGuestReturnAddresses(stack.data(), 13, guests, read, rets, sites, 3) == 0);
}

// A minimal PE32+ image as loaded: exports NtWaitForSingleObject at 0x1000, NtYieldExecution
// at 0x1100, and a forwarder whose RVA points inside the export directory.
std::vector<uint8_t> MakePe() {
	std::vector<uint8_t> img(0x2000, 0);
	auto w16 = [&](uint32_t at, uint16_t v) { std::memcpy(&img[at], &v, 2); };
	auto w32 = [&](uint32_t at, uint32_t v) { std::memcpy(&img[at], &v, 4); };
	img[0] = 'M';
	img[1] = 'Z';
	w32(0x3C, 0x80);
	std::memcpy(&img[0x80], "PE\0\0", 4);
	const uint32_t opt = 0x80 + 24;
	w16(opt, 0x20B);
	w32(opt + 56, 0x2000); // SizeOfImage
	w32(opt + 108, 16);    // NumberOfRvaAndSizes
	w32(opt + 112, 0x200); // export directory
	w32(opt + 116, 0x100);
	const uint32_t dir = 0x200;
	w32(dir + 20, 3);     // NumberOfFunctions
	w32(dir + 24, 3);     // NumberOfNames
	w32(dir + 28, 0x240); // AddressOfFunctions
	w32(dir + 32, 0x260); // AddressOfNames
	w32(dir + 36, 0x280); // AddressOfNameOrdinals
	w32(0x240, 0x1000);
	w32(0x244, 0x1100);
	w32(0x248, 0x2F0 - 0xF0); // 0x200: inside the export directory = forwarder
	w32(0x260, 0x300);
	w32(0x264, 0x320);
	w32(0x268, 0x340);
	w16(0x280, 0);
	w16(0x282, 1);
	w16(0x284, 2);
	std::memcpy(&img[0x300], "NtWaitForSingleObject", 22);
	std::memcpy(&img[0x320], "NtYieldExecution", 17);
	std::memcpy(&img[0x340], "Forwarded", 10);
	return img;
}

void TestNearestExport() {
	using Sampler::NearestExport;
	auto     img = MakePe();
	uint32_t rva = 0;
	CHECK(Sampler::PeImageSize(img.data(), 4096) == 0x2000);
	const char* name = NearestExport(img.data(), 0x2000, 0x1014, &rva);
	CHECK(name != nullptr && std::string(name) == "NtWaitForSingleObject" && rva == 0x1000);
	name = NearestExport(img.data(), 0x2000, 0x1150, &rva);
	CHECK(name != nullptr && std::string(name) == "NtYieldExecution" && rva == 0x1100);
	CHECK(NearestExport(img.data(), 0x2000, 0x0FFF, &rva) == nullptr); // below every export
	name = NearestExport(img.data(), 0x2000, 0x0250, &rva); // the forwarder is never chosen
	CHECK(name == nullptr);
	// Truncated or corrupt images fail safely (run under AddressSanitizer).
	CHECK(NearestExport(img.data(), 0x100, 0x1014, &rva) == nullptr);
	CHECK(Sampler::PeImageSize(img.data(), 0x20) == 0);
	std::mt19937 rng(1234);
	for (int i = 0; i < 20000; i++) {
		auto copy = img;
		for (int k = 0; k < 8; k++) {
			copy[rng() % 0x400] = static_cast<uint8_t>(rng());
		}
		const uint32_t limit = 0x100 + rng() % 0x1F00;
		(void)NearestExport(copy.data(), limit, rng() % 0x3000, &rva);
		(void)Sampler::PeImageSize(copy.data(), limit);
	}
}

void TestNearestFunction() {
	Sampler::FunctionList fns = {{0x1000, "LibKernel::A"}, {0x2000, "LibKernel::B"}, {0x3000, "Posix::C"}};
	CHECK(Sampler::NearestFunction(fns, 0x0FFF) == nullptr);
	CHECK(Sampler::NearestFunction(fns, 0x1000)->second == "LibKernel::A");
	CHECK(Sampler::NearestFunction(fns, 0x2FFF)->second == "LibKernel::B");
	CHECK(Sampler::NearestFunction(fns, 0x9000)->second == "Posix::C");
	CHECK(Sampler::NearestFunction({}, 0x1000) == nullptr);
}

Sampler::BurstStats Stats(std::initializer_list<std::pair<Sampler::Kind, uint32_t>> kinds,
                          double cpu, uint64_t span, uint32_t top_entry = 0) {
	Sampler::BurstStats s;
	for (const auto& [kind, n]: kinds) {
		s.by_kind[static_cast<uint32_t>(kind)] += n;
		s.samples += n;
		if (kind == Sampler::Kind::Emulator || kind == Sampler::Kind::OsWait ||
		    kind == Sampler::Kind::OsYield || kind == Sampler::Kind::HostOther) {
			s.host_samples += n;
		}
	}
	s.cpu_pct            = cpu;
	s.guest_span         = span;
	s.top_emulator_entry = top_entry;
	return s;
}

void TestVerdicts() {
	using K = Sampler::Kind;
	auto v  = [](const Sampler::BurstStats& s) { return std::string(Sampler::Verdict(s)); };
	CHECK(v(Stats({{K::Failed, 32}}, -1, 0)) == "no_samples");
	// Blocked in the OS with no CPU: a wait.
	CHECK(v(Stats({{K::OsWait, 30}, {K::Emulator, 2}}, 0.5, UINT64_MAX, 32)) == "waiting");
	// Same samples but the thread burns CPU: not a plain wait.
	CHECK(v(Stats({{K::OsWait, 30}, {K::Emulator, 2}}, 95, UINT64_MAX, 32)) == "busy_in_emulator");
	// CPU unknown: said so rather than guessed.
	CHECK(v(Stats({{K::OsWait, 32}}, -1, UINT64_MAX, 32)) == "waiting_cpu_unknown");
	// A tight guest loop.
	CHECK(v(Stats({{K::Guest, 31}, {K::Emulator, 1}}, 99, 0x1c, 1)) == "guest_spin");
	CHECK(v(Stats({{K::Guest, 32}}, -1, 0x40)) == "guest_spin");
	// Guest code all over the place: progress-like.
	CHECK(v(Stats({{K::Guest, 32}}, 99, 0x200000)) == "varied");
	// A guest loop that keeps calling one emulator function.
	CHECK(v(Stats({{K::Guest, 20}, {K::Emulator, 8}, {K::OsYield, 4}}, 100, 0x30, 10)) ==
	      "polling_emulator");
	// ... but calls spread over many emulator functions is not "polling one".
	CHECK(v(Stats({{K::Guest, 20}, {K::Emulator, 12}}, 100, 0x30, 3)) == "varied");
	// Stuck inside the emulator, spinning.
	CHECK(v(Stats({{K::Emulator, 28}, {K::HostOther, 4}}, 100, UINT64_MAX, 30)) == "busy_in_emulator");
	// Low CPU without being in a wait: varied.
	CHECK(v(Stats({{K::Guest, 32}}, 5, 0x10)) == "varied");
}

void TestTopKeys() {
	std::vector<std::string> keys = {"b", "a", "", "b", "c", "a", "b"};
	std::string              top[2];
	uint32_t                 counts[2] = {};
	CHECK(Sampler::TopKeys(keys, top, counts, 2) == 2);
	CHECK(top[0] == "b" && counts[0] == 3);
	CHECK(top[1] == "a" && counts[1] == 2); // ties keep first-seen order: a before c
	CHECK(Sampler::TopKeys({}, top, counts, 2) == 0);
}

void TestResumeWithRetry() {
	using Sampler::ResumeWithRetry;
	// Succeeds at once: one call, previous count reported.
	int  calls = 0;
	auto ok    = ResumeWithRetry(
        [&](uint32_t*) -> uint32_t {
            calls++;
            return 1;
        },
        Sampler::ResumeAttempts);
	CHECK(ok.resumed && ok.attempts == 1 && calls == 1 && ok.previous_count == 1 && ok.error == 0);
	// Fails twice, then succeeds: three calls, the thread resumed exactly once.
	calls          = 0;
	int  decrements = 0;
	auto later      = ResumeWithRetry(
        [&](uint32_t* error) -> uint32_t {
            if (++calls < 3) {
                *error = 5;
                return UINT32_MAX;
            }
            decrements++;
            return 2;
        },
        Sampler::ResumeAttempts);
	CHECK(later.resumed && later.attempts == 3 && decrements == 1 && later.previous_count == 2);
	CHECK(later.error == 0);
	// Always fails: stops after the allowed attempts with the last error.
	calls     = 0;
	auto fail = ResumeWithRetry(
	    [&](uint32_t* error) -> uint32_t {
		    *error = static_cast<uint32_t>(100 + ++calls);
		    return UINT32_MAX;
	    },
	    Sampler::ResumeAttempts);
	CHECK(!fail.resumed && fail.attempts == Sampler::ResumeAttempts);
	CHECK(calls == static_cast<int>(Sampler::ResumeAttempts));
	CHECK(fail.error == 100 + Sampler::ResumeAttempts);
	CHECK(Sampler::ResumeAttempts >= 2);
}

void TestSummarizePauses() {
	const uint64_t pauses[] = {40000, 12000, 25000, 900000, 18000};
	const auto     s        = Sampler::SummarizePauses(pauses, 5);
	CHECK(s.count == 5 && s.min_ns == 12000 && s.median_ns == 25000 && s.max_ns == 900000);
	CHECK(s.total_ns == 995000);
	const uint64_t even[] = {4, 1, 3, 2};
	const auto     e      = Sampler::SummarizePauses(even, 4);
	CHECK(e.median_ns == 3 && e.min_ns == 1 && e.max_ns == 4 && e.total_ns == 10);
	const auto none = Sampler::SummarizePauses(nullptr, 0);
	CHECK(none.count == 0 && none.total_ns == 0 && none.max_ns == 0);
}

// The review case: a GameState join is queued while a campaign's burst is failing to resume the
// main thread. The queued campaign must be dropped, the worker must stop by itself, nothing may
// sample the thread afterwards, and the failure is logged only after sampling is disabled, with
// the queue's lock free.
void TestQueuedCampaignDroppedOnResumeFailure() {
	using namespace Sampler;
	CampaignQueue           queue;
	std::mutex              m;
	std::condition_variable cv;
	bool                    in_failing_burst = false;
	bool                    second_queued    = false;
	std::atomic<int>        starts {0};
	std::atomic<int>        suspends {0};
	std::atomic<int>        resume_calls {0};
	std::atomic<bool>       worker_done {false};
	std::vector<uint32_t>   campaigns_run; // written by the worker only; read after join
	bool                    log_called         = false;
	bool                    log_lock_free      = false;
	bool                    log_saw_disabled   = false;
	bool                    log_saw_no_pending = false;
	bool                    log_dropped        = false;
	std::thread             worker;

	const auto run_campaign = [&](const Request& r) {
		campaigns_run.push_back(r.campaign);
		suspends++; // the sample whose resume is about to fail
		{
			std::unique_lock lock(m);
			in_failing_burst = true;
			cv.notify_all();
			cv.wait(lock, [&] { return second_queued; });
		}
		const auto outcome = ResumeWithRetry(
		    [&](uint32_t* error) -> uint32_t {
			    resume_calls++;
			    *error = 6; // ERROR_INVALID_HANDLE
			    return UINT32_MAX;
		    },
		    ResumeAttempts);
		CHECK(!outcome.resumed && outcome.attempts == ResumeAttempts);
		HandleResumeExhausted(queue, [&](bool dropped) {
			log_called    = true;
			log_lock_free = queue.LockFreeForTest(); // first: the others take the lock
			log_saw_disabled   = queue.DisabledForTest();
			log_saw_no_pending = !queue.PendingForTest();
			log_dropped        = dropped;
		});
	};
	const auto start = [&] {
		starts++;
		worker = std::thread([&] {
			WorkerLoop(queue, run_campaign);
			worker_done = true;
		});
	};

	Request join;
	std::snprintf(join.joined, sizeof(join.joined), "GameState");
	const auto first = queue.Submit(join, start);
	CHECK(first.outcome == CampaignQueue::Outcome::Queued && first.campaign == 1);
	{
		std::unique_lock lock(m);
		cv.wait(lock, [&] { return in_failing_burst; });
	}
	// A join arrives during the failing burst and is queued.
	const auto second = queue.Submit(join, start);
	CHECK(second.outcome == CampaignQueue::Outcome::Queued && second.campaign == 2);
	CHECK(queue.PendingForTest());
	{
		std::lock_guard lock(m);
		second_queued = true;
	}
	cv.notify_all();

	// The worker must stop on its own; the timeout only keeps a regression from hanging the test.
	for (int i = 0; i < 400 && !worker_done; i++) {
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
	}
	const bool stopped_by_itself = worker_done.load();
	if (!stopped_by_itself) {
		queue.Stop();
	}
	worker.join();
	CHECK(stopped_by_itself);
	CHECK(campaigns_run.size() == 1 && campaigns_run[0] == 1); // campaign 2 never ran
	CHECK(suspends == 1);                                      // nothing sampled after the failure
	CHECK(resume_calls == static_cast<int>(ResumeAttempts));   // retry count unchanged
	CHECK(log_called && log_lock_free && log_saw_disabled && log_saw_no_pending && log_dropped);
	CHECK(!queue.PendingForTest());

	// Joins after the failure are refused for the rest of the run, the first one reported once,
	// and no worker is started again.
	const auto third = queue.Submit(join, start);
	CHECK(third.outcome == CampaignQueue::Outcome::Disabled && third.first_refusal);
	const auto fourth = queue.Submit(join, start);
	CHECK(fourth.outcome == CampaignQueue::Outcome::Disabled && !fourth.first_refusal);
	CHECK(starts == 1);
	CHECK(!queue.PendingForTest());
	Request out;
	CHECK(!queue.Next(&out)); // returns at once: disabled
}

// The worker refuses on the disabled flag alone, even with a request pending.
void TestNextRefusesWhenDisabled() {
	using namespace Sampler;
	CampaignQueue queue;
	Request       join;
	CHECK(queue.Submit(join, [] {}).outcome == CampaignQueue::Outcome::Queued);
	queue.ForceDisabledKeepingRequestForTest();
	CHECK(queue.PendingForTest());
	Request out;
	CHECK(!queue.Next(&out));
	CHECK(!queue.PendingForTest());
	const auto before = std::chrono::steady_clock::now();
	CHECK(!queue.WaitUntil(before + std::chrono::seconds(2))); // returns at once, not at 2 s
	CHECK(std::chrono::steady_clock::now() - before < std::chrono::seconds(1));
	CHECK(std::string(queue.Interruption()) == "sampling_disabled");
}

// What the queue still does in normal operation.
void TestCampaignQueueBasics() {
	using namespace Sampler;
	CampaignQueue queue;
	int           starts = 0;
	const auto    start  = [&] { starts++; };
	Request       join;
	for (uint32_t i = 1; i <= MaxCampaigns; i++) {
		const auto result = queue.Submit(join, start);
		CHECK(result.outcome == CampaignQueue::Outcome::Queued && result.campaign == i);
	}
	CHECK(starts == 1);
	Request out;
	CHECK(queue.Next(&out) && out.campaign == MaxCampaigns); // a newer join replaces the pending one
	const auto capped = queue.Submit(join, start);
	CHECK(capped.outcome == CampaignQueue::Outcome::CapReached && capped.first_refusal);
	CHECK(!queue.Submit(join, start).first_refusal);
	// An uninterrupted wait runs to its deadline.
	CHECK(queue.WaitUntil(std::chrono::steady_clock::now() + std::chrono::milliseconds(10)));
	// A newer join interrupts a campaign's wait.
	CampaignQueue other;
	CHECK(other.Submit(join, [] {}).outcome == CampaignQueue::Outcome::Queued);
	CHECK(other.Next(&out));
	std::thread joiner([&] {
		std::this_thread::sleep_for(std::chrono::milliseconds(20));
		other.Submit(join, [] {});
	});
	CHECK(!other.WaitUntil(std::chrono::steady_clock::now() + std::chrono::seconds(30)));
	joiner.join();
	CHECK(std::string(other.Interruption()) == "superseded_by_next_join");
	// Shutdown.
	queue.Stop();
	CHECK(!queue.Next(&out));
	CHECK(queue.Submit(join, start).outcome == CampaignQueue::Outcome::Stopping);
	CHECK(std::string(queue.Interruption()) == "stopping");
}

void TestBounds() {
	using namespace Sampler;
	uint32_t previous = 0;
	for (uint32_t i = 0; i < BurstCount; i++) {
		CHECK(i == 0 || BurstOffsetsS[i] > previous);
		previous = BurstOffsetsS[i];
	}
	CHECK(BurstOffsetsS[BurstCount - 1] <= 1800);
	CHECK(BurstCount * SamplesPerBurst <= 416);
	// Lines per campaign: start, end, and per burst a summary, the printed samples and at most
	// regs + reg_targets + wait match + code.
	const uint32_t lines = 2 + BurstCount * (1 + PrintedSamplesPerBurst + 4);
	CHECK(lines <= 200);
	CHECK(MaxCampaigns * lines <= 1200);
	// One burst suspends the thread SamplesPerBurst times over about this long.
	CHECK(SamplesPerBurst * SampleIntervalMs <= 200);
}

} // namespace

int main() {
	TestDisabledLogsNothing();
	TestEnvironmentSwitch();
	TestShortWaitIsSilent();
	TestLongWaitFullDetail();
	Test64BitValues();
	TestCompareOperators();
	TestSupersededAndNewSubmission();
	TestLongWaitBudget();
	TestValueChangeCaps();
	TestMilestonesSkippedTogether();
	TestQueueUnknownWhenFrameTraceOff();
	TestClassifyHost();
	TestDecodeCallBefore();
	TestStackScanAndCallee();
	TestNearestExport();
	TestNearestFunction();
	TestVerdicts();
	TestTopKeys();
	TestResumeWithRetry();
	TestSummarizePauses();
	TestQueuedCampaignDroppedOnResumeFailure();
	TestNextRefusesWhenDisabled();
	TestCampaignQueueBasics();
	TestBounds();
	if (g_failures != 0) {
		std::printf("stall probe tests: %d failed\n", g_failures);
		return 1;
	}
	std::printf("stall probe tests: all passed\n");
	return 0;
}
