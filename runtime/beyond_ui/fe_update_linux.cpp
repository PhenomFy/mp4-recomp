// Front end on Linux: updates of the AppImage from the project's GitHub releases (fe_update.cpp is the Windows one).
//
// The AppImage is the whole game program (runtime, recompiler, compiler, Python), so an update replaces that one file.
// A release carries update-linux.txt next to it (linux/make_appimage.sh writes it, linux/publish_release.sh uploads
// both): the build's version (its UTC build time), commit, the AppImage's SHA-256 and size, and an ID of the recompiler
// it brings. The running build knows its own version and recompiler ID (compiled in by make_appimage.sh).
// Updating downloads the new AppImage next to the one that was started ($APPIMAGE), checks its size and SHA-256, keeps
// the old one as <name>.previous (Undo puts it back) and renames the new one over it: the running AppImage stays mounted
// until it exits. Then the new one is started. HTTPS goes through the system's libcurl (loaded at run time, so the
// AppImage needs none of its own and uses the system's certificates). Nothing else is downloaded or started.
// If the recompiler ID differs from the one the game code was last built with (recompiler.id next to the game), the
// Play screen asks for the rebuild: setup with the game's own _data folder, as after a Windows update of recompiler\.
#include "fe.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cctype>
#include <mutex>
#include <thread>
#include <dlfcn.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>

#ifndef SWR_APPIMAGE_VERSION   // a build outside make_appimage.sh: no version, so it never offers an update
#define SWR_APPIMAGE_VERSION ""
#endif
#ifndef SWR_APPIMAGE_COMMIT
#define SWR_APPIMAGE_COMMIT ""
#endif
#ifndef SWR_RECOMPILER_ID
#define SWR_RECOMPILER_ID ""
#endif

namespace fe {

static const char* REPO = getenv("SWR_UPDATE_REPO") ? getenv("SWR_UPDATE_REPO") : MP4_REPO;
static const char* MANIFEST = "update-linux.txt";

struct Manifest { std::string version, commit, tag, name, sha, recompiler; long long size = 0; bool ok = false; };

static std::mutex g_um; static UpdateState g_us; static Manifest g_latest;
UpdateState update_state() { std::lock_guard<std::mutex> l(g_um); return g_us; }
static void set_error(const std::string& e) { std::lock_guard<std::mutex> l(g_um); g_us.stage = UP_ERROR; g_us.error = e; }

std::string installed_version() {
    std::string v = SWR_APPIMAGE_VERSION, c = SWR_APPIMAGE_COMMIT;
    return v.empty() ? std::string() : c.empty() ? v : v + " (" + c + ")";
}

// ---------------------------------------------------------------- manifest
// version <v> | commit <c> | tag <t> | appimage <sha> <size> <name> | recompiler <id>
static Manifest parse_manifest(const std::string& t) {
    Manifest m; size_t p = 0;
    while (p < t.size()) {
        size_t e = t.find('\n', p); std::string l = t.substr(p, e == std::string::npos ? std::string::npos : e - p); p = e == std::string::npos ? t.size() : e + 1;
        if (!l.empty() && l.back() == '\r') l.pop_back();
        if (l.empty() || l[0] == '#') continue;
        size_t s1 = l.find(' '); if (s1 == std::string::npos) continue;
        std::string k = l.substr(0, s1), v = l.substr(s1 + 1);
        if (k == "version") m.version = v; else if (k == "commit") m.commit = v; else if (k == "tag") m.tag = v; else if (k == "recompiler") m.recompiler = v;
        else if (k == "appimage") { size_t a = v.find(' '), b = a == std::string::npos ? a : v.find(' ', a + 1); if (b == std::string::npos) continue;
            m.sha = v.substr(0, a); m.size = atoll(v.substr(a + 1, b - a - 1).c_str()); m.name = v.substr(b + 1); }
    }
    m.ok = !m.version.empty() && m.sha.size() == 64 && m.size > 0 && !m.name.empty() && m.name.find('/') == std::string::npos && m.name.find("..") == std::string::npos;
    return m;
}
static int vcmp(const std::string& a, const std::string& b) {   // dotted numbers: 2026.11.04.0327 > 2026.11.03.2210
    const char *x = a.c_str(), *y = b.c_str();
    while (*x || *y) { long p = strtol(x, (char**)&x, 10), q = strtol(y, (char**)&y, 10); if (p != q) return p < q ? -1 : 1; if (*x == '.') x++; if (*y == '.') y++;
        if (*x && !isdigit((unsigned char)*x)) break; if (*y && !isdigit((unsigned char)*y)) break; }
    return 0;
}

// ---------------------------------------------------------------- HTTPS through the system's libcurl
typedef void CURL;
static struct { void* mod; CURL* (*init)(); int (*setopt)(CURL*, int, ...); int (*perform)(CURL*); void (*cleanup)(CURL*);
    int (*getinfo)(CURL*, int, ...); const char* (*strerror)(int); int (*global_init)(long); bool tried; } C;
enum { O_WRITEDATA = 10001, O_URL = 10002, O_USERAGENT = 10018, O_WRITEFUNCTION = 20011, O_LOW_SPEED_LIMIT = 19, O_LOW_SPEED_TIME = 20,
       O_NOPROGRESS = 43, O_FAILONERROR = 45, O_FOLLOWLOCATION = 52, O_XFERINFODATA = 10057, O_CONNECTTIMEOUT = 78, O_XFERINFOFUNCTION = 20219,
       O_PROTOCOLS = 181, O_REDIR_PROTOCOLS = 182, O_PROTOCOLS_STR = 10318, O_REDIR_PROTOCOLS_STR = 10319, I_RESPONSE_CODE = 0x200002 };
static bool curl_load(std::string* err) {
    static std::mutex m; std::lock_guard<std::mutex> l(m);
    if (!C.tried) { C.tried = true;
        for (const char* n : {"libcurl.so.4", "libcurl-gnutls.so.4", "libcurl.so"}) if ((C.mod = dlopen(n, RTLD_NOW | RTLD_LOCAL))) break;
        if (C.mod) { *(void**)&C.init = dlsym(C.mod, "curl_easy_init"); *(void**)&C.setopt = dlsym(C.mod, "curl_easy_setopt"); *(void**)&C.perform = dlsym(C.mod, "curl_easy_perform");
            *(void**)&C.cleanup = dlsym(C.mod, "curl_easy_cleanup"); *(void**)&C.getinfo = dlsym(C.mod, "curl_easy_getinfo"); *(void**)&C.strerror = dlsym(C.mod, "curl_easy_strerror");
            *(void**)&C.global_init = dlsym(C.mod, "curl_global_init");
            if (!C.init || !C.setopt || !C.perform || !C.cleanup || !C.getinfo || !C.strerror) C.mod = nullptr; else if (C.global_init) C.global_init(3); } }
    if (!C.mod) *err = "updates need libcurl (the curl package) on this system";
    return C.mod != nullptr;
}
struct Sink { std::string* body; FILE* f; long long expect, got; bool too_big; };
static size_t on_data(char* p, size_t s, size_t n, void* u) {
    Sink* k = (Sink*)u; size_t len = s * n; k->got += (long long)len;
    if (k->body) { if (k->body->size() + len > (1u << 20)) { k->too_big = true; return 0; } k->body->append(p, len); return len; }
    return fwrite(p, 1, len, k->f);
}
static int on_progress(void* u, long long, long long now, long long, long long) {
    Sink* k = (Sink*)u; if (k->expect > 0) { std::lock_guard<std::mutex> l(g_um); g_us.pct = (float)(100.0 * now / k->expect); } return 0;
}
// GET url into *body (small) or into the file `path`; https only, except http://127.0.0.1 for tests
static bool http_get(const std::string& url, std::string* body, const std::string& path, long long expect, std::string* err) {
    if (url.rfind("https://", 0) != 0 && url.rfind("http://127.0.0.1", 0) != 0) { *err = "refusing a non-HTTPS address"; return false; }
    if (!curl_load(err)) return false;
    CURL* h = C.init(); if (!h) { *err = "libcurl could not start"; return false; }
    Sink k = {body, nullptr, expect, 0, false};
    if (!body && !(k.f = fopen(path.c_str(), "wb"))) { C.cleanup(h); *err = "cannot write " + path; return false; }
    bool local = url.rfind("http://127.0.0.1", 0) == 0;
    C.setopt(h, O_URL, url.c_str()); C.setopt(h, O_USERAGENT, "MetroidPrime4Recomp-updater"); C.setopt(h, O_FOLLOWLOCATION, 1L); C.setopt(h, O_FAILONERROR, 0L);
    if (C.setopt(h, O_PROTOCOLS_STR, local ? "http,https" : "https") != 0) C.setopt(h, O_PROTOCOLS, local ? 3L : 2L);   // libcurl before 7.85
    if (C.setopt(h, O_REDIR_PROTOCOLS_STR, "https") != 0) C.setopt(h, O_REDIR_PROTOCOLS, 2L);
    C.setopt(h, O_CONNECTTIMEOUT, 15L); C.setopt(h, O_LOW_SPEED_LIMIT, 512L); C.setopt(h, O_LOW_SPEED_TIME, 30L);
    C.setopt(h, O_WRITEFUNCTION, on_data); C.setopt(h, O_WRITEDATA, &k);
    C.setopt(h, O_NOPROGRESS, 0L); C.setopt(h, O_XFERINFOFUNCTION, on_progress); C.setopt(h, O_XFERINFODATA, &k);
    int rc = C.perform(h); long code = 0; C.getinfo(h, I_RESPONSE_CODE, &code); C.cleanup(h);
    if (k.f && fclose(k.f) != 0 && rc == 0) { *err = "disk full?"; return false; }
    if (k.too_big) { *err = "update information too large"; return false; }
    if (rc != 0) { *err = std::string("could not reach the update server (") + C.strerror(rc) + ")"; return false; }
    if (code != 200) { *err = code == 404 ? "no update information on the server yet" : "the update server answered " + std::to_string(code); return false; }
    return true;
}
static std::string base_url() { const char* e = getenv("SWR_UPDATE_URL"); return e && *e ? e : ""; }   // tests: a local server
static std::string manifest_url() { std::string b = base_url(); return b.empty() ? std::string("https://github.com/") + REPO + "/releases/latest/download/" + MANIFEST : b + "/" + MANIFEST; }
static std::string appimage_url(const Manifest& m) {
    std::string b = base_url(); if (!b.empty()) return b + "/" + m.name;
    return m.tag.empty() ? std::string("https://github.com/") + REPO + "/releases/latest/download/" + m.name
                         : std::string("https://github.com/") + REPO + "/releases/download/" + m.tag + "/" + m.name;
}
std::string update_notes_url() { std::lock_guard<std::mutex> l(g_um); return std::string("https://github.com/") + REPO + "/releases" + (g_latest.tag.empty() ? std::string("/latest") : "/tag/" + g_latest.tag); }

// ---------------------------------------------------------------- SHA-256
static std::string sha256_file(const std::string& path) {
    static const unsigned K[64] = {0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,
        0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,0x983e5152,0xa831c66d,
        0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
        0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,
        0x5b9cca4f,0x682e6ff3,0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
    unsigned H[8] = {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    FILE* f = fopen(path.c_str(), "rb"); if (!f) return "";
    auto rot = [](unsigned x, int n) { return (x >> n) | (x << (32 - n)); };
    auto block = [&](const unsigned char* p) {
        unsigned w[64]; for (int i = 0; i < 16; i++) w[i] = (unsigned)p[4*i] << 24 | (unsigned)p[4*i+1] << 16 | (unsigned)p[4*i+2] << 8 | p[4*i+3];
        for (int i = 16; i < 64; i++) w[i] = w[i-16] + (rot(w[i-15],7) ^ rot(w[i-15],18) ^ (w[i-15] >> 3)) + w[i-7] + (rot(w[i-2],17) ^ rot(w[i-2],19) ^ (w[i-2] >> 10));
        unsigned a=H[0],b=H[1],c=H[2],d=H[3],e=H[4],g=H[5],h2=H[6],k=H[7];
        for (int i = 0; i < 64; i++) { unsigned t1 = k + (rot(e,6) ^ rot(e,11) ^ rot(e,25)) + ((e & g) ^ (~e & h2)) + K[i] + w[i], t2 = (rot(a,2) ^ rot(a,13) ^ rot(a,22)) + ((a & b) ^ (a & c) ^ (b & c));
            k = h2; h2 = g; g = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2; }
        H[0]+=a; H[1]+=b; H[2]+=c; H[3]+=d; H[4]+=e; H[5]+=g; H[6]+=h2; H[7]+=k; };
    std::vector<unsigned char> buf(1 << 20); unsigned long long total = 0; size_t n, rest = 0;
    while ((n = fread(buf.data() + rest, 1, buf.size() - rest, f)) > 0) { total += n; n += rest; size_t i = 0; for (; i + 64 <= n; i += 64) block(&buf[i]); rest = n - i; memmove(buf.data(), &buf[i], rest); }
    fclose(f);
    unsigned char tail[128] = {0}; memcpy(tail, buf.data(), rest); tail[rest] = 0x80; size_t tl = rest + 1 + 8 <= 64 ? 64 : 128;
    for (int i = 0; i < 8; i++) tail[tl - 1 - i] = (unsigned char)((total * 8) >> (8 * i));
    block(tail); if (tl == 128) block(tail + 64);
    char x[65]; for (int i = 0; i < 8; i++) snprintf(x + 8 * i, 9, "%08x", H[i]); return x;
}

// ---------------------------------------------------------------- the AppImage that was started, and what the game code was built with
static std::string appimage_path() { const char* a = getenv("APPIMAGE"); return a && *a ? a : ""; }
static std::string recompiler_id_file() { return join(P.exedir, "recompiler.id"); }
static void trim(std::string& s) { while (!s.empty() && isspace((unsigned char)s.back())) s.pop_back(); }

// at start: after a new AppImage, the game code may need its recompiler; the first start of a build with an ID records it
void update_startup() {
    std::string have = SWR_RECOMPILER_ID; if (have.empty()) return;
    std::string built = read_text(recompiler_id_file()); trim(built);
    if (built.empty()) { if (check_installed().code_ok) write_text(recompiler_id_file(), have + "\n"); return; }
    if (built != have && check_installed().code_ok) { std::lock_guard<std::mutex> l(g_um); g_us.stage = UP_REBUILD; g_us.installed = installed_version(); g_us.rebuild = true; }
}
void update_setup_done() { std::string have = SWR_RECOMPILER_ID; if (!have.empty()) write_text(recompiler_id_file(), have + "\n");
    std::lock_guard<std::mutex> l(g_um); if (g_us.stage == UP_REBUILD) g_us = UpdateState(); }

// ---------------------------------------------------------------- check
void update_check_async() {
    { std::lock_guard<std::mutex> l(g_um);
      if (g_us.stage == UP_CHECKING || g_us.stage == UP_DOWNLOADING || g_us.stage == UP_REBUILD || (g_us.stage == UP_INSTALLED && g_us.restart)) return;
      g_us = UpdateState(); g_us.stage = UP_CHECKING; g_us.installed = installed_version(); }
    std::thread([] {
        std::string t, err;
        if (std::string(SWR_APPIMAGE_VERSION).empty() || appimage_path().empty()) { set_error("updates work in the AppImage only"); return; }
        if (!http_get(manifest_url(), &t, "", 0, &err)) { set_error(err); return; }
        Manifest m = parse_manifest(t);
        if (!m.ok) { set_error("the update information could not be read"); return; }
        std::lock_guard<std::mutex> l(g_um); g_latest = m; g_us.latest = m.commit.empty() ? m.version : m.version + " (" + m.commit + ")";
        g_us.stage = vcmp(SWR_APPIMAGE_VERSION, m.version) < 0 ? UP_AVAILABLE : UP_CURRENT;
    }).detach();
}

// ---------------------------------------------------------------- install: the new AppImage next to the old one, then renamed over it
void update_install_async() {
    { std::lock_guard<std::mutex> l(g_um); if (g_us.stage != UP_AVAILABLE) return; g_us.stage = UP_DOWNLOADING; g_us.pct = 0; g_us.what = "Downloading the new version"; }
    std::thread([] {
        Manifest m; { std::lock_guard<std::mutex> l(g_um); m = g_latest; }
        std::string app = appimage_path(), err;
        if (app.empty()) { set_error("updates work in the AppImage only"); return; }
        std::string dir = app.substr(0, app.find_last_of('/') + 1), tmp = app + ".download", prev = app + ".previous";
        if (access(dir.c_str(), W_OK) != 0) { set_error("cannot write to " + dir + " (where the AppImage is): move it to a folder of yours"); return; }
        unlink(tmp.c_str());
        if (!http_get(appimage_url(m), nullptr, tmp, m.size, &err)) { unlink(tmp.c_str()); set_error(err); return; }
        { std::lock_guard<std::mutex> l(g_um); g_us.what = "Checking the download"; }
        if (file_size(tmp) != m.size || sha256_file(tmp) != m.sha) { unlink(tmp.c_str()); set_error("the download is damaged (checksum mismatch): nothing was changed"); return; }
        struct stat st; mode_t mode = stat(app.c_str(), &st) == 0 ? (st.st_mode & 07777) | 0100 : 0755; chmod(tmp.c_str(), mode);
        // the old one is kept for Undo (a hard link: the running AppImage keeps its file), then the new one takes its name
        unlink(prev.c_str());
        if (link(app.c_str(), prev.c_str()) != 0) { FILE* a = fopen(app.c_str(), "rb"); FILE* b = a ? fopen(prev.c_str(), "wb") : nullptr; bool ok = a && b;
            if (ok) { std::vector<char> buf(1 << 20); size_t n; while ((n = fread(buf.data(), 1, buf.size(), a)) > 0) if (fwrite(buf.data(), 1, n, b) != n) { ok = false; break; } }
            if (a) fclose(a); if (b && fclose(b) != 0) ok = false;
            if (!ok) { unlink(prev.c_str()); unlink(tmp.c_str()); set_error("could not keep the current version as " + prev + ": nothing was changed"); return; } }
        if (rename(tmp.c_str(), app.c_str()) != 0) { unlink(tmp.c_str()); set_error("could not replace " + app + ": nothing was changed"); return; }
        std::lock_guard<std::mutex> l(g_um); g_us.stage = UP_INSTALLED; g_us.restart = true; g_us.installed = g_us.latest; g_us.files = 1; g_us.pct = 100;
    }).detach();
}

// ---------------------------------------------------------------- undo: <name>.previous back in place
bool update_backup_exists() { std::string a = appimage_path(); return !a.empty() && file_exists(a + ".previous"); }
bool update_rollback(std::string* msg, bool* rebuild, bool* restart) {
    *rebuild = false; *restart = false;
    std::string app = appimage_path(), prev = app + ".previous", newer = app + ".newer";
    if (app.empty() || !file_exists(prev)) { *msg = "No earlier version to go back to."; return false; }
    unlink(newer.c_str());
    if (rename(app.c_str(), newer.c_str()) != 0 || rename(prev.c_str(), app.c_str()) != 0) { rename(newer.c_str(), app.c_str()); *msg = "Could not put the earlier version back."; return false; }
    *restart = true;   // the rebuild, if its recompiler differs, is offered when it starts (update_startup)
    *msg = "Went back to the previous version. The newer one is kept as " + newer.substr(newer.find_last_of('/') + 1) + ".";
    std::lock_guard<std::mutex> l(g_um); g_us = UpdateState(); g_us.installed = installed_version(); return true;
}

// ---------------------------------------------------------------- restart: the AppImage (now the new one) again, on its own
bool update_restart() {
    std::string app = appimage_path(); if (app.empty()) return false;
    pid_t p = fork(); if (p < 0) return false;
    if (p == 0) { setsid(); signal(SIGHUP, SIG_IGN);
        int n = open("/dev/null", O_RDWR); if (n >= 0) { dup2(n, 0); if (n > 2) close(n); }
        unsetenv("APPIMAGE"); unsetenv("APPDIR"); unsetenv("ARGV0"); unsetenv("OWD"); unsetenv("SWR_RECOMPILER");   // the old mount's
        execl(app.c_str(), app.c_str(), "--launcher", (char*)nullptr); _exit(127); }
    return true;
}

} // namespace fe
