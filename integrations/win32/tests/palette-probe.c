// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef UNICODE
#define UNICODE
#endif
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <wchar.h>
static HWND owner,main_frame;
static HINSTANCE instance;
static const wchar_t *class_name=L"KaryoOwnedPaletteQA";
static const COLORREF colors[]={RGB(22,132,64),RGB(200,68,34),RGB(45,85,175),RGB(164,122,32)};
static HWND create(const wchar_t *title,DWORD style,DWORD extended,HWND parent,int x,int y,int width,int height,COLORREF color)
{
    return CreateWindowExW(extended,class_name,title,style,x,y,width,height,parent,NULL,instance,(void*)(uintptr_t)color);
}
static void palette(int number)
{
    wchar_t title[64];swprintf(title,64,L"Karyo Palette %d",number);
    HWND window=create(title,WS_POPUP|WS_CAPTION|WS_SYSMENU,WS_EX_TOOLWINDOW,owner,
                       180+(number%2)*320,180+(number/2)*220,200,120,colors[number]);
    if(!window)ExitProcess(4);
    ShowWindow(window,SW_SHOWNOACTIVATE);UpdateWindow(window);
    printf("KARYO_PALETTE_READY=%d\n",number);fflush(stdout);
}
static LRESULT CALLBACK procedure(HWND window,UINT message,WPARAM wparam,LPARAM lparam)
{
    if(message==WM_CREATE) {
        SetWindowLongPtrW(window,GWLP_USERDATA,(LONG_PTR)((CREATESTRUCTW*)lparam)->lpCreateParams);return 0;
    }
    if(message==WM_PAINT) {
        PAINTSTRUCT paint;HDC dc=BeginPaint(window,&paint);
        HBRUSH brush=CreateSolidBrush((COLORREF)GetWindowLongPtrW(window,GWLP_USERDATA));
        FillRect(dc,&paint.rcPaint,brush);DeleteObject(brush);EndPaint(window,&paint);return 0;
    }
    if(message==WM_TIMER && window==main_frame) {
        if(wparam==1){KillTimer(window,1);palette(2);palette(3);puts("KARYO_ALL_PALETTES_READY");fflush(stdout);}
        if(wparam==2)PostMessageW(window,WM_CLOSE,0,0);
        return 0;
    }
    if(message==WM_DESTROY && window==main_frame){PostQuitMessage(0);return 0;}
    return DefWindowProcW(window,message,wparam,lparam);
}
int wmain(int argc,wchar_t **argv)
{
    instance=GetModuleHandleW(NULL);WNDCLASSW klass={0};klass.lpfnWndProc=procedure;
    klass.hInstance=instance;klass.lpszClassName=class_name;
    if(!RegisterClassW(&klass))return 1;
    owner=create(L"Karyo Palette Owner",WS_POPUP,WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,NULL,0,0,1,1,RGB(0,0,0));
    if(!owner)return 2;
    if(argc>1 && !wcscmp(argv[1],L"--mapped-owner"))ShowWindow(owner,SW_SHOWNOACTIVATE);
    main_frame=create(L"Karyo Palette Main",WS_OVERLAPPEDWINDOW,0,owner,100,80,900,580,RGB(80,80,80));
    if(!main_frame)return 3;
    ShowWindow(main_frame,SW_SHOW);UpdateWindow(main_frame);palette(0);palette(1);
    HWND foreign=create(L"Karyo Palette Foreign",WS_OVERLAPPEDWINDOW,WS_EX_APPWINDOW,NULL,1000,30,200,140,RGB(156,36,156));
    if(!foreign)return 5;
    ShowWindow(foreign,SW_SHOWNOACTIVATE);UpdateWindow(foreign);
    SetTimer(main_frame,1,3000,NULL);SetTimer(main_frame,2,60000,NULL);
    puts("KARYO_PALETTE_PROBE_READY");fflush(stdout);
    MSG message;while(GetMessageW(&message,NULL,0,0)>0){TranslateMessage(&message);DispatchMessageW(&message);}
    puts("KARYO_PALETTE_PROBE_DONE");return 0;
}
