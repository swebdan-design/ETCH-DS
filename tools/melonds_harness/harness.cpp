/*
 * ETCH DS -- by Synth Centric
 * Headless melonDS test harness (links melonDS's core library).
 *
 *   harness ROM FRAMES OUT_PREFIX [script] [symbols]
 *
 * Runs the ROM for FRAMES frames (59.83 Hz) with melonDS's interpreter (no
 * JIT, for the most faithful timing), recording:
 *   OUT_PREFIX.wav     the console's audio output (stereo, 32 728 Hz)
 *   OUT_PREFIX_NNNN.ppm screenshots (top over bottom) at the frames asked for
 *   stdout             lines "MEM frame name value" for each symbol watch
 *
 * script: ';'-separated items
 *   F:KEYS:N    hold KEYS (e.g. START, A+RIGHT, DOWN) for N frames from frame F
 *   F:TOUCH:X,Y,N   touch the bottom screen at X,Y for N frames
 *   F:SHOT      screenshot after frame F
 *   F:DUMP      print every watched symbol
 * env ETCH_SD=path gives the console an SD card (FAT image, DLDI)
 * symbols: ';'-separated name=hexaddr:size (main RAM addresses from nm)
 */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdarg>
#include <string>
#include <vector>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <fstream>
#include <sstream>

#include "NDS.h"
#include "NDSCart.h"
#include "GPU.h"
#include "GPU_Soft.h"
#include "SPU.h"
#include "Platform.h"
#include "Args.h"
#include "FATStorage.h"

using namespace melonDS;

/* ---------------- Platform layer (minimal, headless) --------------------- */
namespace melonDS::Platform {
void SignalStop(StopReason, void*) {}
std::string GetLocalFilePath(const std::string& f) { return f; }
FileHandle* OpenFile(const std::string& path, FileMode mode)
{
    if (!(mode & FileMode::Write)) return (FileHandle*)fopen(path.c_str(), "rb");
    if (mode & FileMode::Preserve) {
        FILE* f = fopen(path.c_str(), "r+b");
        if (f || (mode & FileMode::NoCreate)) return (FileHandle*)f;
    }
    return (FileHandle*)fopen(path.c_str(), "w+b");
}
FileHandle* OpenLocalFile(const std::string& path, FileMode mode) { return OpenFile(path, mode); }
bool FileExists(const std::string& name) { FILE* f = fopen(name.c_str(), "rb"); if (f) fclose(f); return f != nullptr; }
bool LocalFileExists(const std::string& name) { return FileExists(name); }
bool CheckFileWritable(const std::string&) { return true; }
bool CheckLocalFileWritable(const std::string&) { return true; }
bool CloseFile(FileHandle* f) { return fclose((FILE*)f) == 0; }
bool IsEndOfFile(FileHandle* f) { return feof((FILE*)f); }
bool FileReadLine(char* s, int n, FileHandle* f) { return fgets(s, n, (FILE*)f) != nullptr; }
u64 FilePosition(FileHandle* f) { return ftell((FILE*)f); }
bool FileSeek(FileHandle* f, s64 o, FileSeekOrigin w)
{
    int ww = w == FileSeekOrigin::Start ? SEEK_SET : w == FileSeekOrigin::Current ? SEEK_CUR : SEEK_END;
    return fseek((FILE*)f, o, ww) == 0;
}
void FileRewind(FileHandle* f) { rewind((FILE*)f); }
u64 FileRead(void* d, u64 s, u64 c, FileHandle* f) { return fread(d, s, c, (FILE*)f); }
bool FileFlush(FileHandle* f) { return fflush((FILE*)f) == 0; }
u64 FileWrite(const void* d, u64 s, u64 c, FileHandle* f) { return fwrite(d, s, c, (FILE*)f); }
u64 FileWriteFormatted(FileHandle* f, const char* fmt, ...)
{
    va_list a; va_start(a, fmt); int r = vfprintf((FILE*)f, fmt, a); va_end(a); return r;
}
u64 FileLength(FileHandle* f)
{
    long p = ftell((FILE*)f); fseek((FILE*)f, 0, SEEK_END); long l = ftell((FILE*)f); fseek((FILE*)f, p, SEEK_SET); return l;
}
static bool g_log = false;
void Log(LogLevel lv, const char* fmt, ...)
{
    if (!g_log && lv < LogLevel::Warn) return;
    va_list a; va_start(a, fmt); vfprintf(stderr, fmt, a); va_end(a);
}
struct Thread { std::thread t; };
Thread* Thread_Create(std::function<void()> f) { auto* t = new Thread; t->t = std::thread(f); return t; }
void Thread_Free(Thread* t) { if (t->t.joinable()) t->t.detach(); delete t; }
void Thread_Wait(Thread* t) { if (t->t.joinable()) t->t.join(); }
struct Semaphore { std::mutex m; std::condition_variable c; int n = 0; };
Semaphore* Semaphore_Create() { return new Semaphore; }
void Semaphore_Free(Semaphore* s) { delete s; }
void Semaphore_Reset(Semaphore* s) { std::lock_guard<std::mutex> l(s->m); s->n = 0; }
void Semaphore_Wait(Semaphore* s) { std::unique_lock<std::mutex> l(s->m); s->c.wait(l, [&] { return s->n > 0; }); s->n--; }
bool Semaphore_TryWait(Semaphore* s, int ms)
{
    std::unique_lock<std::mutex> l(s->m);
    if (!s->c.wait_for(l, std::chrono::milliseconds(ms), [&] { return s->n > 0; })) return false;
    s->n--; return true;
}
void Semaphore_Post(Semaphore* s, int c) { { std::lock_guard<std::mutex> l(s->m); s->n += c; } s->c.notify_all(); }
struct Mutex { std::mutex m; };
Mutex* Mutex_Create() { return new Mutex; }
void Mutex_Free(Mutex* m) { delete m; }
void Mutex_Lock(Mutex* m) { m->m.lock(); }
void Mutex_Unlock(Mutex* m) { m->m.unlock(); }
bool Mutex_TryLock(Mutex* m) { return m->m.try_lock(); }
void Sleep(u64 us) { std::this_thread::sleep_for(std::chrono::microseconds(us)); }
u64 GetMSCount() { return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
u64 GetUSCount() { return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
void WriteNDSSave(const u8*, u32, u32, u32, void*) {}
void WriteGBASave(const u8*, u32, u32, u32, void*) {}
void WriteFirmware(const Firmware&, u32, u32, void*) {}
void WriteDateTime(int, int, int, int, int, int, void*) {}
void MP_Begin(void*) {}
void MP_End(void*) {}
int MP_SendPacket(u8*, int, u64, void*) { return 0; }
int MP_RecvPacket(u8*, u64*, void*) { return 0; }
int MP_SendCmd(u8*, int, u64, void*) { return 0; }
int MP_SendReply(u8*, int, u64, u16, void*) { return 0; }
int MP_SendAck(u8*, int, u64, void*) { return 0; }
int MP_RecvHostPacket(u8*, u64*, void*) { return 0; }
u16 MP_RecvReplies(u8*, u64, u16, void*) { return 0; }
int Net_SendPacket(u8*, int, void*) { return 0; }
int Net_RecvPacket(u8*, void*) { return 0; }
void Camera_Start(int, void*) {}
void Camera_Stop(int, void*) {}
void Camera_CaptureFrame(int, u32*, int, int, bool, void*) {}
void Mic_Start(void*) {}
void Mic_Stop(void*) {}
int Mic_ReadInput(s16*, int, void*) { return 0; }
AACDecoder* AAC_Init() { return nullptr; }
void AAC_DeInit(AACDecoder*) {}
bool AAC_Configure(AACDecoder*, int, int) { return false; }
bool AAC_DecodeFrame(AACDecoder*, const void*, int, void*, int) { return false; }
bool Addon_KeyDown(KeyType, void*) { return false; }
void Addon_RumbleStart(u32, void*) {}
void Addon_RumbleStop(void*) {}
float Addon_MotionQuery(MotionQueryType, void*) { return 0; }
DynamicLibrary* DynamicLibrary_Load(const char*) { return nullptr; }
void DynamicLibrary_Unload(DynamicLibrary*) {}
void* DynamicLibrary_LoadFunction(DynamicLibrary*, const char*) { return nullptr; }
}

/* ---------------- script ---------------------------------------------------- */
struct Item { int frame; std::string kind; std::string arg; };
struct Sym { std::string name; u32 addr; u32 size; };

static u32 key_bits(const std::string& s)
{
    /* NDS key mask bits (melonDS order): A B SELECT START RIGHT LEFT UP DOWN R L X Y */
    static const char* names[] = { "A", "B", "SELECT", "START", "RIGHT", "LEFT", "UP", "DOWN", "R", "L", "X", "Y" };
    u32 m = 0;
    std::stringstream ss(s);
    std::string k;
    while (std::getline(ss, k, '+'))
        for (int i = 0; i < 12; i++) if (k == names[i]) m |= 1u << i;
    return m;
}

static void write_wav(const char* path, const std::vector<s16>& pcm, int rate)
{
    FILE* f = fopen(path, "wb");
    u32 data = (u32)pcm.size() * 2, riff = 36 + data, fmtlen = 16, sr = rate, br = rate * 4;
    u16 fmt = 1, ch = 2, ba = 4, bits = 16;
    fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f);
    fwrite(&fmtlen, 4, 1, f); fwrite(&fmt, 2, 1, f); fwrite(&ch, 2, 1, f); fwrite(&sr, 4, 1, f);
    fwrite(&br, 4, 1, f); fwrite(&ba, 2, 1, f); fwrite(&bits, 2, 1, f);
    fwrite("data", 1, 4, f); fwrite(&data, 4, 1, f); fwrite(pcm.data(), 2, pcm.size(), f);
    fclose(f);
}

static void shot(NDS& nds, const std::string& path)
{
    void *top, *bot;
    if (!nds.GPU.GetFramebuffers(&top, &bot)) return;
    FILE* f = fopen(path.c_str(), "wb");
    fprintf(f, "P6\n256 384\n255\n");
    for (int s = 0; s < 2; s++) {
        u32* p = (u32*)(s ? bot : top);
        for (int i = 0; i < 256 * 192; i++) {
            u32 c = p[i];
            u8 rgb[3] = { (u8)((c >> 16) & 0xFF), (u8)((c >> 8) & 0xFF), (u8)(c & 0xFF) };
            fwrite(rgb, 1, 3, f);
        }
    }
    fclose(f);
}

static u32 read_mem(NDS& nds, u32 addr, u32 size)
{
    u32 off = addr & (nds.MainRAMMask), v = 0;
    for (u32 i = 0; i < size && i < 4; i++) v |= (u32)nds.MainRAM[(off + i) & nds.MainRAMMask] << (8 * i);
    return v;
}

int main(int argc, char** argv)
{
    if (argc < 4) { fprintf(stderr, "usage: harness ROM FRAMES OUT [script] [symbols]\n"); return 1; }
    int frames = atoi(argv[2]);
    std::string out = argv[3];
    std::vector<Item> items;
    std::vector<Sym> syms;
    if (argc > 4) {
        std::stringstream ss(argv[4]);
        std::string it;
        while (std::getline(ss, it, ';')) {
            if (it.empty()) continue;
            size_t a = it.find(':'), b = it.find(':', a + 1);
            Item x;
            x.frame = atoi(it.substr(0, a).c_str());
            x.kind = it.substr(a + 1, b == std::string::npos ? std::string::npos : b - a - 1);
            x.arg = b == std::string::npos ? "" : it.substr(b + 1);
            items.push_back(x);
        }
    }
    if (argc > 5) {
        std::stringstream ss(argv[5]);
        std::string it;
        while (std::getline(ss, it, ';')) {
            if (it.empty()) continue;
            size_t e = it.find('='), c = it.find(':');
            syms.push_back({ it.substr(0, e), (u32)strtoul(it.substr(e + 1, c - e - 1).c_str(), nullptr, 16),
                             (u32)atoi(it.substr(c + 1).c_str()) });
        }
    }

    std::ifstream rf(argv[1], std::ios::binary);
    std::vector<char> rom((std::istreambuf_iterator<char>(rf)), std::istreambuf_iterator<char>());
    auto romdata = std::make_unique<u8[]>(rom.size());
    memcpy(romdata.get(), rom.data(), rom.size());

    NDSArgs args;
    args.JIT = std::nullopt;
    args.OutputSampleRate = 16756991.0 / 512.0;
    auto nds = std::make_unique<NDS>(std::move(args), nullptr);
    nds->SetRenderer(std::make_unique<SoftRenderer>(*nds));
    /* ETCH_SD=path: a FAT SD card image behind melonDS's DLDI (created if missing) */
    std::optional<NDSCart::NDSCartArgs> cargs = std::nullopt;
    if (const char* sd = getenv("ETCH_SD")) {
        NDSCart::NDSCartArgs a;
        a.SDCard = FATStorageArgs{ std::string(sd), 64ull << 20, false, std::nullopt };
        cargs = std::move(a);
    }
    auto cart = NDSCart::ParseROM(std::move(romdata), (u32)rom.size(), nullptr, std::move(cargs));
    if (!cart) { fprintf(stderr, "bad ROM\n"); return 1; }
    nds->SetNDSCart(std::move(cart));
    nds->Reset();                       /* after the cart: applies the DLDI patch */
    nds->SetupDirectBoot(std::string("etch_ds.nds"));
    nds->Start();

    std::vector<s16> pcm;
    std::vector<s16> buf(8192);
    u32 keys_hold_until[12] = { 0 };
    int touch_until = -1, tx = 0, ty = 0;
    for (int fr = 0; fr < frames; fr++) {
        for (auto& it : items) {
            if (it.frame != fr) continue;
            if (it.kind == "TOUCH") {
                int n = 1;
                sscanf(it.arg.c_str(), "%d,%d,%d", &tx, &ty, &n);
                touch_until = fr + n;
            } else if (it.kind != "SHOT" && it.kind != "DUMP") {
                u32 m = key_bits(it.kind);
                int n = atoi(it.arg.c_str());
                for (int k = 0; k < 12; k++) if (m & (1u << k)) keys_hold_until[k] = fr + (n ? n : 1);
            }
        }
        u32 pressed = 0;
        for (int k = 0; k < 12; k++) if ((int)keys_hold_until[k] > fr) pressed |= 1u << k;
        nds->SetKeyMask(~pressed & 0xFFF);
        if (touch_until > fr) nds->TouchScreen(tx, ty); else nds->ReleaseScreen();
        nds->RunFrame();
        int n;
        while ((n = nds->SPU.ReadOutput(buf.data(), 4096)) > 0) pcm.insert(pcm.end(), buf.begin(), buf.begin() + n * 2);
        for (auto& it : items) {
            if (it.frame != fr) continue;
            if (it.kind == "SHOT") {
                char p[512];
                snprintf(p, sizeof p, "%s_%05d.ppm", out.c_str(), fr);
                shot(*nds, p);
            } else if (it.kind == "DUMP") {
                for (auto& s : syms) printf("MEM %d %s %u\n", fr, s.name.c_str(), read_mem(*nds, s.addr, s.size));
            }
        }
    }
    for (auto& s : syms) printf("MEM %d %s %u\n", frames, s.name.c_str(), read_mem(*nds, s.addr, s.size));
    write_wav((out + ".wav").c_str(), pcm, 32728);
    fprintf(stderr, "frames %d, audio %zu samples (%.2f s)\n", frames, pcm.size() / 2, pcm.size() / 2 / 32728.5);
    return 0;
}
