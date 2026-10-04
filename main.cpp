// language: C++17, file: main.cpp, app: whalefeed — bitcoin whale transaction feed
// Windows 11, MSVC, WinAPI + WinHTTP. No external dependencies.
// Pulls recent confirmed txs from the public mempool.space API (no key),
// filters by USD value and lists the large ones. Refresh on demand.
#include <windows.h>
#include <winhttp.h>
#include <commctrl.h>
#include <string>
#include <vector>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "comctl32.lib")

struct WhaleTx { std::wstring txid; double btc, usd; };

static HWND g_list, g_threshold, g_price;
static double g_btcUsd = 64000.0;

static std::string HttpsGet(const std::wstring& host, const std::wstring& path) {
    std::string out;
    HINTERNET sess = WinHttpOpen(L"whalefeed/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                 WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!sess) return out;
    HINTERNET conn = WinHttpConnect(sess, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (conn) {
        HINTERNET req = WinHttpOpenRequest(conn, L"GET", path.c_str(), nullptr,
                                           WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                           WINHTTP_FLAG_SECURE);
        if (req && WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                      WINHTTP_NO_REQUEST_DATA, 0, 0, 0)
                && WinHttpReceiveResponse(req, nullptr)) {
            DWORD bytes = 0; char buf[16384];
            for (;;) {
                if (!WinHttpReadData(req, buf, sizeof(buf), &bytes) || bytes == 0) break;
                out.append(buf, bytes);
            }
        }
        if (req) WinHttpCloseHandle(req);
        WinHttpCloseHandle(conn);
    }
    WinHttpCloseHandle(sess);
    return out;
}

// Extract every "txid":"...","value":<num> pair from a tx list JSON.
static void ParseTxs(const std::string& json, std::vector<WhaleTx>& out) {
    size_t p = 0;
    while ((p = json.find("\"txid\":\"", p)) != std::string::npos) {
        WhaleTx tx;
        p += 8;
        size_t e = json.find('"', p);
        tx.txid.assign(json.begin() + p, json.begin() + e);
        size_t v = json.find("\"value\":", e);
        if (v != std::string::npos) {
            v += 8;
            double sats = std::stod(json.substr(v, json.find_first_of(",}", v) - v));
            tx.btc = sats * 1e-8;
            tx.usd = tx.btc * g_btcUsd;
            out.push_back(tx);
        }
        p = e;
    }
}

static DWORD WINAPI Fetch(LPVOID) {
    // latest confirmed block txs — public, no key
    std::string body = HttpsGet(L"mempool.space", L"/api/v1/blocks");
    std::vector<WhaleTx> txs;
    if (!body.empty()) {
        ParseTxs(body, txs);
        double thr = 100000;
        wchar_t buf[32]; GetWindowTextW(g_threshold, buf, 32);
        thr = _wtof(buf);
        txs.erase(std::remove_if(txs.begin(), txs.end(),
                  [thr](const WhaleTx& t){ return t.usd < thr; }), txs.end());
    }
    ListView_DeleteAllItems(g_list);
    LVITEMW it{}; it.mask = LVIF_TEXT;
    wchar_t b1[64], b2[64], b3[64];
    for (size_t i = 0; i < txs.size() && i < 100; ++i) {
        it.iItem = (int)i;
        it.pszText = b1;
        swprintf(b1, 64, L"%s…", txs[i].txid.substr(0, 14).c_str());
        it.iItem = ListView_InsertItem(g_list, &it);
        swprintf(b2, 64, L"%.4f BTC", txs[i].btc);
        ListView_SetItemText(g_list, it.iItem, 1, b2);
        swprintf(b3, 64, L"$%.0f", txs[i].usd);
        ListView_SetItemText(g_list, it.iItem, 2, b3);
    }
    return 0;
}

static LRESULT CALLBACK WndProc(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_LISTVIEW_CLASSES };
        InitCommonControlsEx(&icc);
        HFONT f = CreateFontW(16, 0, 0, 0, FW_NORMAL, 0, 0, 0, 0, 0, 0, 0, 0, L"Segoe UI");
        auto label = [&](const wchar_t* t, int x, int y) {
            HWND h = CreateWindowW(L"STATIC", t, WS_CHILD | WS_VISIBLE, x, y, 140, 22, w, nullptr, nullptr, nullptr);
            SendMessageW(h, WM_SETFONT, (WPARAM)f, TRUE);
        };
        label(L"Min value $", 16, 22);
        g_threshold = CreateWindowW(L"EDIT", L"100000", WS_CHILD | WS_VISIBLE | WS_BORDER, 160, 20, 110, 26, w, nullptr, nullptr, nullptr);
        label(L"BTC price $", 290, 22);
        g_price = CreateWindowW(L"EDIT", L"64000", WS_CHILD | WS_VISIBLE | WS_BORDER, 400, 20, 110, 26, w, nullptr, nullptr, nullptr);
        CreateWindowW(L"BUTTON", L"Fetch", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 530, 18, 110, 30, w, (HMENU)1, nullptr, nullptr);
        g_list = CreateWindowExW(0, WC_LISTVIEWW, nullptr,
                                 WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL,
                                 16, 62, 740, 340, w, (HMENU)2, nullptr, nullptr);
        LVCOLUMNW col{}; col.mask = LVCF_TEXT | LVCF_WIDTH;
        const wchar_t* heads[] = { L"TxID", L"Value", L"USD" };
        int widths[] = { 300, 200, 200 };
        for (int i = 0; i < 3; ++i) {
            col.pszText = (LPWSTR)heads[i]; col.cx = widths[i];
            ListView_InsertColumn(g_list, i, &col);
        }
        CreateThread(nullptr, 0, Fetch, nullptr, 0, nullptr);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == 1) {
            wchar_t buf[32]; GetWindowTextW(g_price, buf, 32);
            g_btcUsd = _wtof(buf);
            CreateThread(nullptr, 0, Fetch, nullptr, 0, nullptr);
        }
        return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(w, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, LPWSTR, int show) {
    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProc; wc.hInstance = inst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(RGB(13, 17, 23));
    wc.lpszClassName = L"WhaleFeedWnd";
    RegisterClassW(&wc);
    HWND w = CreateWindowExW(0, L"WhaleFeedWnd", L"WhaleFeed — btc whale transactions",
                             WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 790, 460,
                             nullptr, nullptr, inst, nullptr);
    ShowWindow(w, show);
    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0)) { TranslateMessage(&m); DispatchMessageW(&m); }
    return 0;
}
