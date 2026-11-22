// SPDX-License-Identifier: GPL-3.0-or-later
/* Ordinary-prefix QA only: observe a real application's menus and use Win32 input. */
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define _WIN32_WINNT 0x0600
#include <windows.h>
#include <stdio.h>
#include <wchar.h>
#include <wctype.h>

static const wchar_t *wanted;
static HWND main_window;

static BOOL is_target(HWND window)
{
    DWORD pid=0; GetWindowThreadProcessId(window,&pid);
    HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);
    if(!process)return FALSE;
    wchar_t path[32768]; DWORD size=32768;
    BOOL ok=QueryFullProcessImageNameW(process,0,path,&size); CloseHandle(process);
    if(!ok)return FALSE;
    const wchar_t *name=wcsrchr(path,L'\\');
    return !_wcsicmp(name?name+1:path,wanted);
}

static BOOL CALLBACK find_main(HWND window,LPARAM unused)
{
    (void)unused;
    if(IsWindowVisible(window) && !GetWindow(window,GW_OWNER) && GetMenu(window) && is_target(window)) {
        main_window=window; return FALSE;
    }
    return TRUE;
}

static void string(const wchar_t *value)
{
    char text[131072];
    int count=WideCharToMultiByte(CP_UTF8,0,value,-1,text,sizeof(text),NULL,NULL);
    putchar('"');
    if(count>0)for(const unsigned char *p=(const unsigned char *)text;*p;p++) {
        if(*p=='"'||*p=='\\')putchar('\\');
        if(*p<32)printf("\\u%04x",*p);else putchar(*p);
    }
    putchar('"');
}

static BOOL CALLBACK dump_window(HWND window,LPARAM data)
{
    int *first=(int *)data;
    if(!IsWindowVisible(window)||!is_target(window))return TRUE;
    if(!*first)putchar(',');
    *first=0;
    wchar_t title[2048],klass[256]; RECT rect;
    GetWindowTextW(window,title,2048);GetClassNameW(window,klass,256);GetWindowRect(window,&rect);
    printf("{\"handle\":%llu,\"owner\":%llu,\"title\":",(unsigned long long)(UINT_PTR)window,
        (unsigned long long)(UINT_PTR)GetWindow(window,GW_OWNER));string(title);
    printf(",\"class\":");string(klass);
    printf(",\"rect\":[%ld,%ld,%ld,%ld]}",rect.left,rect.top,rect.right,rect.bottom);
    return TRUE;
}

static void key(WORD code,BOOL down)
{
    INPUT input={0};input.type=INPUT_KEYBOARD;input.ki.wVk=code;
    input.ki.dwFlags=down?0:KEYEVENTF_KEYUP;
    if(SendInput(1,&input,sizeof(input))!=1)ExitProcess(8);
}

int wmain(int argc,wchar_t **argv)
{
    if(argc<2)return 2;
    wanted=argv[1];EnumWindows(find_main,0);if(!main_window)return 3;
    if(argc==2 || !wcscmp(argv[2],L"inspect")) {
        printf("{\"main\":%llu,\"menus\":[",(unsigned long long)(UINT_PTR)main_window);
        HMENU menu=GetMenu(main_window);int count=GetMenuItemCount(menu);
        for(int i=0;i<count;i++) {
            wchar_t label[1024];RECT rect;
            GetMenuStringW(menu,i,label,1024,MF_BYPOSITION);
            if(!GetMenuItemRect(main_window,menu,(UINT)i,&rect))return 4;
            if(i)putchar(',');
            printf("{\"label\":");string(label);
            printf(",\"center\":[%ld,%ld]}",(rect.left+rect.right)/2,(rect.top+rect.bottom)/2);
        }
        printf("],\"windows\":[");int first=1;EnumWindows(dump_window,(LPARAM)&first);puts("]}");return 0;
    }
    /* Preserve an already focused owned dialog instead of activating its owner. */
    HWND active=GetForegroundWindow();
    if(!active || !is_target(active)) {
        HWND popup=GetLastActivePopup(main_window);
        SetForegroundWindow(popup && IsWindowVisible(popup)?popup:main_window);
    }
    Sleep(80);
    if(!wcscmp(argv[2],L"close"))return PostMessageW(main_window,WM_CLOSE,0,0)?0:5;
    if(argc<4)return 2;
    if(!wcscmp(argv[2],L"key")) {
        BOOL control=!wcsncmp(argv[3],L"C-",2),alt=!wcsncmp(argv[3],L"A-",2);
        const wchar_t *value=argv[3]+(control||alt?2:0);
        WORD code=!wcscmp(value,L"ESC")?VK_ESCAPE:!wcscmp(value,L"END")?VK_END:
            !wcscmp(value,L"ENTER")?VK_RETURN:(wcslen(value)==1?(WORD)value[0]:0);
        if(!code)return 7;
        if(control)key(VK_CONTROL,TRUE);
        if(alt)key(VK_MENU,TRUE);
        key(code,TRUE);key(code,FALSE);
        if(alt)key(VK_MENU,FALSE);
        if(control)key(VK_CONTROL,FALSE);
        return 0;
    }
    if(!wcscmp(argv[2],L"text")) {
        for(const wchar_t *p=argv[3];*p;p++) {
            INPUT input[2];ZeroMemory(input,sizeof(input));
            input[0].type=input[1].type=INPUT_KEYBOARD;
            input[0].ki.wScan=input[1].ki.wScan=*p;
            input[0].ki.dwFlags=KEYEVENTF_UNICODE;
            input[1].ki.dwFlags=KEYEVENTF_UNICODE|KEYEVENTF_KEYUP;
            if(SendInput(2,input,sizeof(input[0]))!=2)return 8;
        }
        return 0;
    }
    return 2;
}
