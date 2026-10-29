#include "tas.h"
#include "../game_addrs.h"
#include "../input/input.h"
#include "../ios/disc.h"
#include "../kernel/host.h"
#include "../kernel/memory.h"
#include "../log.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <tuple>
#include <unordered_map>
#include <vector>

#ifdef BKR_CALL_COUNTS
extern "C" uint32_t g_callCounts[0x200000];
#endif

namespace
{
constexpr double kFieldsPerSecond = 60.0;   // script time: video fields of game time

// ------------------------------------------------------------------ symbols
// Names in address expressions: tools/game_addrs.txt names, then every symbol of the game's
// linker ELF (files/tsc_wii_release_dvd.elf on the disc), which also names the state
// classes from their vtables (__vt__<len><class>).
std::once_flag s_symOnce;
std::unordered_map<std::string, uint32_t> s_symbols;
std::unordered_map<uint32_t, std::string> s_vtables;   // vtable address -> class name

uint32_t BE32(const uint8_t* p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
uint16_t BE16(const uint8_t* p) { return (uint16_t)(p[0] << 8 | p[1]); }

void LoadSymbols()
{
    std::filesystem::path elf = std::filesystem::path(Disc_DataDir()) / "files" / "tsc_wii_release_dvd.elf";
    std::ifstream f(elf, std::ios::binary);
    std::vector<uint8_t> d((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (d.size() < 52 || memcmp(d.data(), "\x7f" "ELF", 4) != 0)
    {
        LOG_WARN("TAS: no symbols ({} not readable); names from tools/game_addrs.txt only", elf.string());
        return;
    }
    uint32_t shoff = BE32(&d[32]);
    uint16_t shnum = BE16(&d[48]);
    for (uint32_t i = 0; i < shnum; i++)
    {
        const uint8_t* sh = &d[shoff + i * 40];
        if (BE32(sh + 4) != 2) continue;   // SHT_SYMTAB
        uint32_t off = BE32(sh + 16), size = BE32(sh + 20);
        const uint8_t* str = &d[BE32(&d[shoff + BE32(sh + 24) * 40 + 16])];
        for (uint32_t k = 0; k + 16 <= size; k += 16)
        {
            const uint8_t* s = &d[off + k];
            uint32_t value = BE32(s + 4);
            if (!value) continue;
            std::string name = (const char*)str + BE32(s);
            if (name.rfind("__vt__", 0) == 0)
            {
                // __vt__<len><class>
                size_t i2 = 6, len = 0;
                while (i2 < name.size() && isdigit((unsigned char)name[i2])) len = len * 10 + (name[i2++] - '0');
                if (len && i2 + len == name.size()) s_vtables[value] = name.substr(i2);
            }
            s_symbols.emplace(std::move(name), value);
        }
    }
    LOG_INFO("TAS: {} symbols, {} vtables from {}", s_symbols.size(), s_vtables.size(), elf.filename().string());
}

std::unordered_map<std::string, std::string> s_aliases;   // "alias NAME EXPR"

bool SymbolByName(const std::string& name, uint32_t& out)
{
    static const char* const kNames[] = {
#define GAME_ADDR(name, kind, ...) #name,
#include "../game_addrs.inc"
#undef GAME_ADDR
    };
    for (size_t k = 0; k < std::size(kNames); k++)
        if (name == kNames[k])
        {
            out = GameAddr((GA)k);
            return out != 0;
        }
    std::call_once(s_symOnce, LoadSymbols);
    auto it = s_symbols.find(name);
    if (it == s_symbols.end()) return false;
    out = it->second;
    return true;
}

// ------------------------------------------------------------------ guest values
// Address expressions: hex constants, names, "+"/"-" offsets and [expr] for reading a
// pointer, e.g. "[g_pWorld]+0x44".
bool ValidGuest(uint32_t a, uint32_t size)
{
    uint32_t seg = a >> 28, phys = a & 0x0FFFFFFF;
    if (seg == 0x8 || seg == 0xC) return phys + size <= MEM1_SIZE;
    if (seg == 0x9 || seg == 0xD) return phys + size <= MEM2_SIZE;
    return false;
}

bool EvalAddr(const std::string& s, size_t& i, uint32_t& out);

bool IsNameChar(char c) { return isalnum((unsigned char)c) || c == '_' || c == '@' || c == '$' || c == '<' || c == '>' || c == ','; }

bool EvalTerm(const std::string& s, size_t& i, uint32_t& out)
{
    if (i < s.size() && s[i] == '[')
    {
        i++;
        uint32_t p;
        if (!EvalAddr(s, i, p) || i >= s.size() || s[i] != ']') return false;
        i++;
        if (!ValidGuest(p, 4)) return false;
        out = Read32(p);
        return true;
    }
    // a number: 0x..., or plain hex digits that are not a name
    size_t j = i;
    bool prefixed = s.compare(i, 2, "0x") == 0 || s.compare(i, 2, "0X") == 0;
    if (prefixed) j += 2;
    size_t k = j;
    while (k < s.size() && isxdigit((unsigned char)s[k])) k++;
    if (k > j && (prefixed || k == s.size() || !IsNameChar(s[k])))
    {
        out = (uint32_t)strtoul(s.substr(j, k - j).c_str(), nullptr, 16);
        i = k;
        return true;
    }
    k = i;
    while (k < s.size() && IsNameChar(s[k])) k++;
    if (auto a = s_aliases.find(s.substr(i, k - i)); k > i && a != s_aliases.end())
    {
        size_t ai = 0;
        if (!EvalAddr(a->second, ai, out) || ai != a->second.size()) return false;
        i = k;
        return true;
    }
    if (k > i && SymbolByName(s.substr(i, k - i), out))
    {
        i = k;
        return true;
    }
    return false;
}

bool EvalAddr(const std::string& s, size_t& i, uint32_t& out)
{
    if (!EvalTerm(s, i, out)) return false;
    while (i < s.size() && (s[i] == '+' || s[i] == '-'))
    {
        char op = s[i++];
        uint32_t v;
        if (!EvalTerm(s, i, v)) return false;
        out = op == '+' ? out + v : out - v;
    }
    return true;
}

enum class VType { U8, S8, U16, S16, U32, S32, F32 };

bool ParseType(const std::string& t, VType& v)
{
    static const struct { const char* n; VType v; } kTypes[] = {
        { "u8", VType::U8 }, { "s8", VType::S8 }, { "u16", VType::U16 }, { "s16", VType::S16 },
        { "u32", VType::U32 }, { "s32", VType::S32 }, { "f32", VType::F32 },
    };
    for (auto& k : kTypes)
        if (t == k.n) { v = k.v; return true; }
    return false;
}

// Reads a typed value; false if the address does not resolve to guest RAM.
bool ReadValue(VType t, const std::string& expr, double& out)
{
    size_t i = 0;
    uint32_t a;
    if (!EvalAddr(expr, i, a) || i != expr.size()) return false;
    uint32_t size = (t == VType::U8 || t == VType::S8) ? 1 : (t == VType::U16 || t == VType::S16) ? 2 : 4;
    if (!ValidGuest(a, size)) return false;
    switch (t)
    {
    case VType::U8: out = Read8(a); break;
    case VType::S8: out = (int8_t)Read8(a); break;
    case VType::U16: out = Read16(a); break;
    case VType::S16: out = (int16_t)Read16(a); break;
    case VType::U32: out = Read32(a); break;
    case VType::S32: out = (int32_t)Read32(a); break;
    case VType::F32: { uint32_t u = Read32(a); float f; memcpy(&f, &u, 4); out = f; break; }
    }
    return true;
}

// ------------------------------------------------------------------ script
enum class Op { Let, Mark, Wait, Hold, Tap, Mash, Set, Point, NoPoint, Until, Check, BreakIf, Log, Shot, Dump, Calls, Repeat, End, Quit };

struct Input
{
    uint32_t buttons = 0;
    int8_t lx = 0, ly = 0, rx = 0, ry = 0;
    uint8_t lt = 0, rt = 0;
    bool stick = false, cstick = false;
};

struct Cond
{
    enum Kind { Value, File, State, InState } kind = Value;
    std::string text;      // file substring, state class or address expression
    VType type = VType::U32;
    std::string op;
    double value = 0;
    std::string var;       // compare with a "let" variable (plus value) instead
    bool orGroup = false;  // the first condition after "or"
};

struct Cmd
{
    Op op;
    double secs = 0, period = 0;
    float u = 0, v = 0;           // point
    Input input;
    bool mash = false;
    std::vector<Cond> conds;      // "A and B or C": (A and B) or C
    std::string text;
    int count = 0;
    int line = 0;
    std::string file;
};

struct Watch
{
    std::string name, expr;
    VType type;
};

std::unordered_map<std::string, double> s_vars;   // "let" values

std::vector<Cmd> s_cmds;
std::vector<Watch> s_watches;
bool s_active = false;
size_t s_pc = 0;
uint64_t s_field = 0;                 // fields since the script started
int64_t s_cmdStart = -1;              // field the current command started at
Input s_background;
std::vector<int> s_repeatLeft;
int s_fails = 0, s_passes = 0;

std::mutex s_eventMutex;              // file reads and state changes come from other threads
std::vector<std::string> s_fileReads, s_states;
size_t s_fileMark = 0, s_stateMark = 0;
std::string s_curState;               // last state entered (telemetry)
std::unordered_map<uint32_t, std::string> s_machineState;   // state machine -> its current state

std::mutex s_presentMutex;            // shots, quit and telemetry are done at the next present
std::vector<std::string> s_pendingShots;
std::atomic<bool> s_quitPending{ false };
int s_quitCode = 0;
uint64_t s_quitField = 0;
std::string s_marker;
FILE* s_csv = nullptr;
std::string s_csvPath;
std::filesystem::path s_outDir;

double Secs() { return s_field / kFieldsPerSecond; }

int8_t Axis(float v) { return (int8_t)std::lround(std::clamp(v, -1.0f, 1.0f) * 100); }

bool ParseInput(const std::string& word, Input& in)
{
    std::istringstream parts(word);
    std::string tok;
    while (std::getline(parts, tok, '+'))
    {
        if (auto eq = tok.find('='); eq != std::string::npos)
        {
            float v = std::stof(tok.substr(eq + 1));
            std::string k = tok.substr(0, eq);
            if (k == "LX") in.lx = Axis(v), in.stick = true;
            else if (k == "LY") in.ly = Axis(v), in.stick = true;
            else if (k == "RX") in.rx = Axis(v), in.cstick = true;
            else if (k == "RY") in.ry = Axis(v), in.cstick = true;
            else if (k == "LT") in.lt = (uint8_t)std::lround(std::clamp(v, 0.0f, 1.0f) * 255);
            else if (k == "RT") in.rt = (uint8_t)std::lround(std::clamp(v, 0.0f, 1.0f) * 255);
            else return false;
            continue;
        }
        static const struct { const char* n; uint32_t b; } kNames[] = {
            { "A", BTN_A }, { "B", BTN_B }, { "C", BTN_C }, { "Z", BTN_Z }, { "1", BTN_1 }, { "2", BTN_2 },
            { "PLUS", BTN_PLUS }, { "START", BTN_PLUS }, { "MINUS", BTN_MINUS }, { "HOME", BTN_HOME },
            { "UP", BTN_UP }, { "DOWN", BTN_DOWN }, { "LEFT", BTN_LEFT }, { "RIGHT", BTN_RIGHT },
        };
        bool found = tok == "NONE";
        for (auto& n : kNames)
            if (tok == n.n) { in.buttons |= n.b; found = true; }
        if (!found) return false;
    }
    return true;
}

bool IsNumber(const std::string& t) { return !t.empty() && (isdigit((unsigned char)t[0]) || t[0] == '.'); }

bool ParseCond(std::istringstream& ss, const std::string& first, Cond& c)
{
    if (first == "file") { c.kind = Cond::File; return (bool)(ss >> c.text); }
    if (first == "state") { c.kind = Cond::State; return (bool)(ss >> c.text); }
    if (first == "instate") { c.kind = Cond::InState; return (bool)(ss >> c.text); }
    if (!ParseType(first, c.type)) return false;
    std::string value;
    if (!(ss >> c.text >> c.op >> value)) return false;
    if (value[0] == '$')
    {
        // $NAME, $NAME+N or $NAME-N
        size_t k = value.find_first_of("+-", 1);
        c.var = value.substr(1, k == std::string::npos ? std::string::npos : k - 1);
        if (k != std::string::npos) c.value = std::stod(value.substr(k));
    }
    else
        c.value = std::stod(value);
    return c.op == "==" || c.op == "!=" || c.op == "<" || c.op == "<=" || c.op == ">" || c.op == ">=";
}

bool TestCond(const Cond& c)
{
    switch (c.kind)
    {
    case Cond::File:
    {
        std::lock_guard lk(s_eventMutex);
        for (size_t i = s_fileMark; i < s_fileReads.size(); i++)
            if (s_fileReads[i].find(c.text) != std::string::npos) return true;
        return false;
    }
    case Cond::State:
    {
        std::lock_guard lk(s_eventMutex);
        for (size_t i = s_stateMark; i < s_states.size(); i++)
            if (s_states[i] == c.text) return true;
        return false;
    }
    case Cond::InState:
    {
        std::lock_guard lk(s_eventMutex);
        for (auto& [m, s] : s_machineState)
            if (s == c.text) return true;
        return false;
    }
    case Cond::Value:
        break;
    }
    double v, ref = c.value;
    if (!ReadValue(c.type, c.text, v)) return false;
    if (!c.var.empty())
    {
        auto it = s_vars.find(c.var);
        if (it == s_vars.end()) return false;
        ref += it->second;
    }
    const std::string& o = c.op;
    if (o == "==") return v == ref;
    if (o == "!=") return v != ref;
    if (o == "<") return v < ref;
    if (o == "<=") return v <= ref;
    if (o == ">") return v > ref;
    return v >= ref;
}

// (A and B) or (C and D) ...
bool TestConds(const std::vector<Cond>& conds)
{
    bool any = false, group = true;
    for (auto& c : conds)
    {
        if (c.orGroup)
        {
            any = any || group;
            group = true;
        }
        group = group && TestCond(c);
    }
    return any || group;
}

void LoadFile(const std::filesystem::path& path, int depth)
{
    std::ifstream f(path);
    if (!f) FATAL("TAS: cannot open {}", path.string());
    std::string line, fname = path.filename().string();
    int n = 0;
    while (std::getline(f, line))
    {
        n++;
        if (auto h = line.find('#'); h != std::string::npos) line.resize(h);
        std::istringstream ss(line);
        std::string w;
        if (!(ss >> w)) continue;
        auto bad = [&]() { FATAL("TAS: {}:{}: cannot parse '{}'", fname, n, line); };
        Cmd c{};
        c.line = n;
        c.file = fname;
        if (w == "include")
        {
            std::string inc;
            if (!(ss >> inc) || depth > 8) bad();
            LoadFile(path.parent_path() / inc, depth + 1);
            continue;
        }
        if (w == "alias")
        {
            std::string name, expr;
            if (!(ss >> name >> expr)) bad();
            s_aliases[name] = expr;
            continue;
        }
        if (w == "watch")
        {
            Watch wt;
            std::string t;
            if (!(ss >> wt.name >> t >> wt.expr) || !ParseType(t, wt.type)) bad();
            s_watches.push_back(wt);
            continue;
        }
        if (w == "let")
        {
            // let NAME TYPE EXPR: remember a value now, for conditions ($NAME)
            std::string t;
            Cond v;
            c.op = Op::Let;
            if (!(ss >> c.text >> t >> v.text) || !ParseType(t, v.type)) bad();
            c.conds.push_back(v);
        }
        else if (w == "mark" || w == "filemark") c.op = Op::Mark;
        else if (w == "wait") { c.op = Op::Wait; if (!(ss >> c.secs)) bad(); }
        else if (w == "hold" || w == "tap" || w == "set" || w == "mash")
        {
            c.op = w == "hold" ? Op::Hold : w == "tap" ? Op::Tap : w == "set" ? Op::Set : Op::Mash;
            if (c.op == Op::Hold && !(ss >> c.secs)) bad();
            std::string tok;
            bool haveSecs = c.op == Op::Hold;
            while (ss >> tok)
            {
                if (IsNumber(tok))
                {
                    // tap INPUT [secs], mash INPUT secs [period]
                    if (!haveSecs) c.secs = std::stod(tok), haveSecs = true;
                    else c.period = std::stod(tok);
                    continue;
                }
                if (!ParseInput(tok, c.input)) bad();
            }
            if (c.op == Op::Tap && c.secs == 0) c.secs = 0.1;
            if (c.op == Op::Mash && c.secs == 0) bad();
            if (c.period == 0) c.period = 0.2;
        }
        else if (w == "point")
        {
            c.op = Op::Point;
            if (!(ss >> c.u >> c.v)) bad();
        }
        else if (w == "nopoint") c.op = Op::NoPoint;
        else if (w == "click")
        {
            // click U V [settle]: point, let the game see it, tap A
            Cmd p = c;
            p.op = Op::Point;
            if (!(ss >> p.u >> p.v)) bad();
            double settle = 0.5;
            if (std::string t; ss >> t) { if (!IsNumber(t)) bad(); settle = std::stod(t); }
            s_cmds.push_back(p);
            Cmd wt = c;
            wt.op = Op::Wait;
            wt.secs = settle;
            s_cmds.push_back(wt);
            c.op = Op::Tap;
            c.secs = 0.1;
            c.input.buttons = BTN_A;
        }
        else if (w == "until" || w == "check" || w == "breakif")
        {
            c.op = w == "until" ? Op::Until : w == "check" ? Op::Check : Op::BreakIf;
            c.secs = 60;
            std::string tok;
            while (ss >> tok)
            {
                if (tok == "timeout") { if (!(ss >> c.secs)) bad(); continue; }
                if (tok == "mash")
                {
                    std::string in;
                    if (!(ss >> in) || !ParseInput(in, c.input)) bad();
                    c.mash = true;
                    c.period = 0.2;
                    // mash INPUT [period]
                    auto pos = ss.tellg();
                    if (std::string t; ss >> t && IsNumber(t)) c.period = std::stod(t);
                    else { ss.clear(); ss.seekg(pos); }
                    continue;
                }
                if (tok == "and") continue;
                if (tok == "or")
                {
                    if (c.conds.empty()) bad();
                    std::string first;
                    Cond cd;
                    if (!(ss >> first) || !ParseCond(ss, first, cd)) bad();
                    cd.orGroup = true;
                    c.conds.push_back(cd);
                    continue;
                }
                if (tok == "msg") { std::getline(ss, c.text); break; }
                Cond cd;
                if (!ParseCond(ss, tok, cd)) bad();
                c.conds.push_back(cd);
            }
            if (c.conds.empty()) bad();
            while (!c.text.empty() && c.text[0] == ' ') c.text.erase(0, 1);
        }
        else if (w == "log" || w == "shot" || w == "dump" || w == "calls")
        {
            c.op = w == "log" ? Op::Log : w == "shot" ? Op::Shot : w == "dump" ? Op::Dump : Op::Calls;
            std::getline(ss, c.text);
            while (!c.text.empty() && c.text[0] == ' ') c.text.erase(0, 1);
            if (c.op != Op::Log && c.text.empty()) bad();
        }
        else if (w == "repeat") { c.op = Op::Repeat; if (!(ss >> c.count) || c.count < 1) bad(); }
        else if (w == "end") c.op = Op::End;
        else if (w == "quit")
        {
            // quit [CODE]; without one, 1 if a check failed, else 0
            c.op = Op::Quit;
            c.count = -1;
            if (std::string t; ss >> t) c.count = std::stoi(t);
        }
        else bad();
        s_cmds.push_back(c);
    }
}

void Marker(const std::string& text)
{
    LOG_INFO("TAS: {:.2f} s: {}", Secs(), text);
    std::lock_guard lk(s_presentMutex);
    s_marker = text;
}

// Quits at the next presented frame (after this frame's screenshots), or two seconds of
// game time later if nothing presents. Exit code: the given one, else 1 if a check failed.
void RequestQuit(int code)
{
    if (s_quitPending) return;
    if (code < 0) code = s_fails ? 1 : 0;
    LOG_INFO("TAS: {} passed, {} failed; quit ({}) at {:.2f} s", s_passes, s_fails, code, Secs());
    s_quitCode = code;
    s_quitField = s_field;
    s_quitPending = true;
}

[[noreturn]] void Exit()
{
    if (s_csv) fflush(s_csv);
    Input_StopRumble();
    fflush(nullptr);
    _Exit(s_quitCode);
}

void DumpMemory(const std::string& name)
{
    std::filesystem::create_directories(s_outDir / "dumps");
    for (auto [suffix, addr, size] : { std::tuple{ ".mem1", 0x80000000u, MEM1_SIZE }, std::tuple{ ".mem2", 0x90000000u, MEM2_SIZE } })
        if (FILE* f = Host_Open(s_outDir / "dumps" / (name + suffix), "wb"))
        {
            fwrite(GuestPtr(addr), 1, size, f);
            fclose(f);
        }
    LOG_INFO("TAS: dumped memory as {}", name);
}

// "calls NAME": call counts per game function since the last "calls" to dumps/NAME.calls
// (index = (address - 0x80000000) / 4, tools/count_diff.py), then a reset; "calls -" only
// resets. Needs a call-counting build (tools\dev_build.bat counts).
void DumpCalls(const std::string& name)
{
#ifdef BKR_CALL_COUNTS
    if (name != "-")
    {
        std::filesystem::create_directories(s_outDir / "dumps");
        if (FILE* f = Host_Open(s_outDir / "dumps" / (name + ".calls"), "wb"))
        {
            fwrite(g_callCounts, sizeof(g_callCounts), 1, f);
            fclose(f);
        }
        LOG_INFO("TAS: call counts {}", name);
    }
    memset(g_callCounts, 0, sizeof(g_callCounts));
#else
    LOG_WARN("TAS: 'calls {}' needs a call-counting build", name);
#endif
}

void Merge(PadState& out, const Input& in)
{
    out.buttons |= in.buttons;
    if (in.stick) out.stickX = in.lx, out.stickY = in.ly;
    if (in.cstick) out.substickX = in.rx, out.substickY = in.ry;
    out.triggerL = std::max(out.triggerL, in.lt);
    out.triggerR = std::max(out.triggerR, in.rt);
}

std::string Where(const Cmd& c) { return std::format("{}:{}", c.file, c.line); }

// Runs commands until one needs more time, adding its input.
void Step(PadState& pad, PointerState& ptr)
{
    for (int guard = 0; guard < 10000 && s_pc < s_cmds.size() && !s_quitPending; guard++)
    {
        Cmd& c = s_cmds[s_pc];
        if (s_cmdStart < 0) s_cmdStart = (int64_t)s_field;
        double el = (s_field - s_cmdStart) / kFieldsPerSecond;
        auto next = [&]() { s_pc++; s_cmdStart = -1; };
        switch (c.op)
        {
        case Op::Let:
        {
            double v;
            if (!ReadValue(c.conds[0].type, c.conds[0].text, v))
            {
                s_fails++;
                LOG_ERROR("TAS: FAIL {}: let {}: {} does not resolve", Where(c), c.text, c.conds[0].text);
                RequestQuit(3);
                return;
            }
            s_vars[c.text] = v;
            LOG_INFO("TAS: {:.2f} s: {} = {}", Secs(), c.text, v);
            next();
            continue;
        }
        case Op::Mark:
        {
            std::lock_guard lk(s_eventMutex);
            s_fileMark = s_fileReads.size();
            s_stateMark = s_states.size();
            next();
            continue;
        }
        case Op::Wait:
            if (el < c.secs) return;
            next();
            continue;
        case Op::Hold:
            if (el < c.secs) { Merge(pad, c.input); return; }
            next();
            continue;
        case Op::Tap:
            if (el < c.secs) { Merge(pad, c.input); return; }
            if (el < c.secs + 0.1) return;
            next();
            continue;
        case Op::Mash:
            if (el < c.secs) { if (fmod(el, c.period) < std::min(c.period / 2, 0.1)) Merge(pad, c.input); return; }
            next();
            continue;
        case Op::Set:
            s_background = c.input;
            next();
            continue;
        case Op::Point:
            // screen fractions -> the game's pointer coordinates (input.cpp)
            ptr.x = (c.u - 0.488f) / 0.360f;
            ptr.y = (c.v - 0.462f) / 0.462f;
            ptr.valid = true;
            next();
            continue;
        case Op::NoPoint:
            ptr.valid = false;
            next();
            continue;
        case Op::Until:
        {
            bool all = TestConds(c.conds);
            if (all)
            {
                LOG_INFO("TAS: {:.2f} s: {} satisfied after {:.2f} s", Secs(), Where(c), el);
                next();
                continue;
            }
            if (el >= c.secs)
            {
                s_fails++;
                LOG_ERROR("TAS: FAIL {}: condition not met within {:.0f} s{}", Where(c), c.secs, c.text.empty() ? "" : " (" + c.text + ")");
                {
                    std::lock_guard lk(s_presentMutex);
                    s_pendingShots.push_back((s_outDir / "shots" / "FAIL.bmp").string());
                }
                RequestQuit(3);
                return;
            }
            if (c.mash && fmod(el, c.period) < std::min(c.period / 2, 0.1)) Merge(pad, c.input);
            return;
        }
        case Op::Check:
        {
            bool all = TestConds(c.conds);
            (all ? s_passes : s_fails)++;
            if (all) LOG_INFO("TAS: PASS {}{}", Where(c), c.text.empty() ? "" : ": " + c.text);
            else LOG_ERROR("TAS: FAIL {}{}", Where(c), c.text.empty() ? "" : ": " + c.text);
            next();
            continue;
        }
        case Op::BreakIf:
        {
            if (!TestConds(c.conds) || s_repeatLeft.empty()) { next(); continue; }
            // past the end of the innermost repeat
            int depth = 0;
            size_t j = s_pc;
            while (++j < s_cmds.size())
            {
                if (s_cmds[j].op == Op::Repeat) depth++;
                else if (s_cmds[j].op == Op::End && depth-- == 0) break;
            }
            s_repeatLeft.pop_back();
            LOG_INFO("TAS: {:.2f} s: {} leaves the loop", Secs(), Where(c));
            s_pc = j + 1;
            s_cmdStart = -1;
            continue;
        }
        case Op::Log: Marker(c.text); next(); continue;
        case Op::Shot:
        {
            std::lock_guard lk(s_presentMutex);
            s_pendingShots.push_back((s_outDir / "shots" / (c.text + ".bmp")).string());
            next();
            continue;
        }
        case Op::Dump: DumpMemory(c.text); next(); continue;
        case Op::Calls: DumpCalls(c.text); next(); continue;
        case Op::Repeat:
            s_repeatLeft.push_back(c.count);
            next();
            continue;
        case Op::End:
        {
            if (s_repeatLeft.empty()) { next(); continue; }
            if (--s_repeatLeft.back() > 0)
            {
                // back to the matching repeat
                int depth = 0;
                size_t j = s_pc;
                while (j-- > 0)
                {
                    if (s_cmds[j].op == Op::End) depth++;
                    else if (s_cmds[j].op == Op::Repeat && depth-- == 0) break;
                }
                s_pc = j + 1;
                s_cmdStart = -1;
            }
            else
            {
                s_repeatLeft.pop_back();
                next();
            }
            continue;
        }
        case Op::Quit: RequestQuit(c.count); return;
        }
    }
    if (s_pc >= s_cmds.size() && !s_quitPending)
    {
        Marker("script finished");
        RequestQuit(-1);
    }
}
}   // namespace

void Tas_Load(const char* path)
{
    std::error_code ec;
    std::filesystem::path p = std::filesystem::absolute(path, ec);
    if (!*path || ec || !std::filesystem::exists(p, ec)) FATAL("TAS: script '{}' not found", path);
    try
    {
        LoadFile(p, 0);
    }
    catch (const std::exception& e)   // e.g. a malformed number in the script
    {
        FATAL("TAS: cannot load {}: {}", p.string(), e.what());
    }
    s_active = true;
    s_outDir = std::filesystem::current_path(ec);
    LOG_INFO("TAS: loaded {} ({} commands, {} watches)", p.string(), s_cmds.size(), s_watches.size());
}

void Tas_SetTelemetry(const char* csvPath) { s_csvPath = csvPath; }

bool Tas_Active() { return s_active; }

void Tas_OnField(PadState& pad, PointerState& ptr)
{
    if (!s_active) return;
    s_field++;
    if (s_quitPending)
    {
        // nothing presented for two seconds: do not wait for a frame to quit on
        if (s_field - s_quitField > 120) Exit();
        return;
    }
    PadState p{};
    Step(p, ptr);
    Merge(p, s_background);
    p.connected = true;
    pad = p;
}

void Tas_OnPresent(void (*saveShot)(const char* path))
{
    if (!s_active) return;
    std::vector<std::string> shots;
    std::string marker;
    {
        std::lock_guard lk(s_presentMutex);
        shots.swap(s_pendingShots);
        marker.swap(s_marker);
    }
    for (auto& s : shots)
    {
        std::error_code ec;
        std::filesystem::create_directories(std::filesystem::path(s).parent_path(), ec);
        saveShot(s.c_str());
        LOG_INFO("TAS: saved {}", s);
    }
    if (!s_csvPath.empty())
    {
        if (!s_csv)
        {
            s_csv = Host_Open(s_csvPath, "w");
            if (!s_csv) s_csvPath.clear();
            else
            {
                fprintf(s_csv, "t,last_state,marker");
                for (auto& w : s_watches) fprintf(s_csv, ",%s", w.name.c_str());
                fprintf(s_csv, "\n");
            }
        }
        if (s_csv)
        {
            for (char& ch : marker) if (ch == ',' || ch == '"') ch = ' ';
            std::string state;
            {
                std::lock_guard lk(s_eventMutex);
                state = s_curState;
            }
            fprintf(s_csv, "%.4f,%s,%s", Secs(), state.c_str(), marker.c_str());
            for (auto& w : s_watches)
            {
                double v;
                if (ReadValue(w.type, w.expr, v)) fprintf(s_csv, ",%.6g", v);
                else fprintf(s_csv, ",");
            }
            fprintf(s_csv, "\n");
            static int rows = 0;
            if (++rows % 60 == 0) fflush(s_csv);
        }
    }
    if (s_quitPending) Exit();
}

void Tas_OnFileRead(const std::string& path)
{
    if (!s_active) return;
    std::lock_guard lk(s_eventMutex);
    if (!s_fileReads.empty() && s_fileReads.back() == path) return;   // the same file, read on
    static std::vector<std::string> logged;   // archives are read in turns: log each file once
    if (std::find(logged.begin(), logged.end(), path) == logged.end())
    {
        LOG_INFO("TAS: {:.2f} s: file {}", Secs(), path);
        logged.push_back(path);
    }
    s_fileReads.push_back(path);
}

void Tas_OnState(uint32_t machine, uint32_t vtable)
{
    if (!s_active) return;
    std::lock_guard lk(s_eventMutex);
    if (!vtable)
    {
        s_machineState.erase(machine);
        return;
    }
    std::call_once(s_symOnce, LoadSymbols);
    auto it = s_vtables.find(vtable);
    std::string name = it != s_vtables.end() ? it->second : std::format("{:08X}", vtable);
    std::string& cur = s_machineState[machine];
    if (name == cur) return;
    LOG_INFO("TAS: {:.2f} s: state {}", Secs(), name);
    cur = name;
    s_curState = name;
    s_states.push_back(std::move(name));
}
