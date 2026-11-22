// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <stdio.h>
#include <wchar.h>

static LRESULT CALLBACK window_proc(HWND window,UINT message,WPARAM wparam,LPARAM lparam)
{
    if (message==WM_PAINT) {
        PAINTSTRUCT paint;HDC dc=BeginPaint(window,&paint);
        HBRUSH brush=CreateSolidBrush(RGB(25,105,175));FillRect(dc,&paint.rcPaint,brush);
        DeleteObject(brush);EndPaint(window,&paint);return 0;
    }
    return DefWindowProcW(window,message,wparam,lparam);
}

static void print_wide(const char *key,const wchar_t *value)
{
    char text[16384];int count=WideCharToMultiByte(CP_UTF8,0,value,-1,text,sizeof(text),NULL,NULL);
    if (count>0)printf("%s=%s\n",key,text);
}

int wmain(int argc,wchar_t **argv)
{
    if (argc>1 && !wcscmp(argv[1],L"--exit42"))return 42;
    for (int index=1;index<argc;index++) {
        char key[40];snprintf(key,sizeof(key),"KARYO_ARG_%d",index);print_wide(key,argv[index]);
    }
    wchar_t variable[4096];
    printf("KARYO_QT_PLATFORM=%lu\n",GetEnvironmentVariableW(L"QT_QPA_PLATFORM",variable,4096));
    printf("KARYO_QT_STYLE=%lu\n",GetEnvironmentVariableW(L"QT_STYLE_OVERRIDE",variable,4096));
    if (GetEnvironmentVariableW(L"DBUS_SESSION_BUS_ADDRESS",variable,4096))print_wide("KARYO_BUS",variable);
    HANDLE status=CreateFileW(L"Z:\\proc\\self\\status",GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,0,NULL);
    if (status==INVALID_HANDLE_VALUE)return 20;
    char bytes[16384];DWORD count=0;
    if (!ReadFile(status,bytes,sizeof(bytes)-1,&count,NULL)){CloseHandle(status);return 21;}
    CloseHandle(status);bytes[count]=0;printf("KARYO_STATUS_BEGIN\n%sKARYO_STATUS_END\n",bytes);
    for (int index=1;index+1<argc;index++) {
        if (wcscmp(argv[index],L"--write-file"))continue;
        HANDLE file=CreateFileW(argv[index+1],GENERIC_WRITE,0,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
        if (file==INVALID_HANDLE_VALUE)return 22;
        const char text[]="Karyo Windows UTF8: \xd0\x9f\xd1\x80\xd0\xb8\xd0\xbc\xd0\xb5\xd1\x80\n";
        DWORD written=0;BOOL ok=WriteFile(file,text,sizeof(text)-1,&written,NULL);CloseHandle(file);
        if (!ok || written!=sizeof(text)-1)return 23;
        puts("KARYO_FILE_IO=1");break;
    }
    for (int index=1;index<argc;index++) {
        if (wcscmp(argv[index],L"--show-window"))continue;
        WNDCLASSW klass={0};klass.lpfnWndProc=window_proc;klass.hInstance=GetModuleHandleW(NULL);
        klass.lpszClassName=L"KaryoWin32QA";klass.hIcon=LoadIconW(klass.hInstance,MAKEINTRESOURCEW(1));
        if (!RegisterClassW(&klass))return 24;
        HWND window=CreateWindowW(klass.lpszClassName,L"Karyo Win32 QA",WS_OVERLAPPEDWINDOW,120,100,480,260,NULL,NULL,klass.hInstance,NULL);
        if (!window)return 25;
        ShowWindow(window,SW_SHOW);UpdateWindow(window);puts("KARYO_WINDOW_CREATED=1");fflush(stdout);
        ULONGLONG end=GetTickCount64()+4000;MSG message;
        while (GetTickCount64()<end) {
            while (PeekMessageW(&message,NULL,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}
            Sleep(10);
        }
        DestroyWindow(window);break;
    }
    puts("KARYO_WIN32_QA=PASS");return 0;
}
