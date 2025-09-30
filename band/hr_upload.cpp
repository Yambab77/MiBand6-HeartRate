#include "hr_upload.h"
#include <windows.h>
#include <wininet.h>
#include <string>
#include <iostream>
#include <fstream>

#pragma comment(lib, "wininet.lib")

void UploadHeartRateToServer(int hr) {
    HINTERNET hSession = InternetOpenW(L"HRUploader", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (!hSession) {
        OutputDebugStringW(L"[HR-UP] InternetOpenW failed\n");
        return;
    }
    HINTERNET hConnect = InternetConnectW(hSession, L"www.woyoudu.cn", INTERNET_DEFAULT_HTTPS_PORT, NULL, NULL, INTERNET_SERVICE_HTTP, 0, 0);
    if (!hConnect) {
        OutputDebugStringW(L"[HR-UP] InternetConnectW failed\n");
        InternetCloseHandle(hSession);
        return;
    }

    const wchar_t* acceptTypes[] = { L"*/*", NULL };
    HINTERNET hRequest = HttpOpenRequestW(hConnect, L"POST", L"/wp-json/hr/v1/push/", NULL, NULL, acceptTypes, INTERNET_FLAG_SECURE, 0);
    if (!hRequest) {
        OutputDebugStringW(L"[HR-UP] HttpOpenRequestW failed\n");
        InternetCloseHandle(hConnect);
        InternetCloseHandle(hSession);
        return;
    }

    std::string postData = "hr=" + std::to_string(hr);
    std::wstring headers = L"Content-Type: application/x-www-form-urlencoded\r\n";
    BOOL bSend = HttpSendRequestW(hRequest, headers.c_str(), (DWORD)headers.length(), (LPVOID)postData.c_str(), (DWORD)postData.length());

    if (!bSend) {
        OutputDebugStringW(L"[HR-UP] HttpSendRequestW failed\n");
    }
    else {
        char buffer[256] = { 0 };
        DWORD bytesRead = 0;
        if (InternetReadFile(hRequest, buffer, sizeof(buffer) - 1, &bytesRead) && bytesRead > 0) {
            buffer[bytesRead] = 0;
            std::string resp(buffer);
            OutputDebugStringA(("[HR-UP] Server response: " + resp + "\n").c_str());
        }
        else {
            OutputDebugStringW(L"[HR-UP] No response or read failed\n");
        }
    }

    InternetCloseHandle(hRequest);
    InternetCloseHandle(hConnect);
    InternetCloseHandle(hSession);
}