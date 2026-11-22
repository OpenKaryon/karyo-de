// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef UNICODE
#define UNICODE
#endif
#include <windows.h>
#include <stdio.h>
#include <wchar.h>
static HWND main_frame;
static const wchar_t *class_name=L"KaryoCaptionProbe";
static LRESULT CALLBACK procedure(HWND window,UINT message,WPARAM wp,LPARAM lp)
{
    if(message==WM_PAINT) {
        PAINTSTRUCT paint;HDC dc=BeginPaint(window,&paint);
        FillRect(dc,&paint.rcPaint,GetSysColorBrush(COLOR_WINDOW));
        EndPaint(window,&paint);return 0;
    }
    if(message==WM_DESTROY && window==main_frame){PostQuitMessage(0);return 0;}
    return DefWindowProcW(window,message,wp,lp);
}
static HWND create(const wchar_t *title,DWORD style,DWORD ex,HWND parent,int x,int y)
{
    HWND window=CreateWindowExW(ex,class_name,title,style,x,y,260,160,parent,NULL,GetModuleHandleW(NULL),NULL);
    if(!window)ExitProcess(10);
    ShowWindow(window,SW_SHOWNOACTIVATE);UpdateWindow(window);return window;
}
static void observe(HWND window,const char *name)
{
    RECT rect;GetWindowRect(window,&rect);int width=rect.right-rect.left;
    RedrawWindow(window,NULL,NULL,RDW_INVALIDATE|RDW_FRAME|RDW_UPDATENOW|RDW_ALLCHILDREN);
    HDC dc=GetWindowDC(window);if(!dc)ExitProcess(11);
    int left=width,top=40,right=0,bottom=0,red=0,white=0,valid=0;
    for(int y=0;y<40;++y)for(int x=width-55;x<width;++x) {
        COLORREF pixel=GetPixel(dc,x,y);
        if(pixel!=CLR_INVALID)++valid;
        if(pixel==RGB(196,43,28)) {
            ++red;if(x<left)left=x;if(x>right)right=x;if(y<top)top=y;if(y>bottom)bottom=y;
        }
    }
    if(valid<200)ExitProcess(12);
    if(red)for(int y=top;y<=bottom;++y)for(int x=left;x<=right;++x)
        if(GetPixel(dc,x,y)==RGB(255,255,255))++white;
    printf("KARYO_CAPTION=%s,%d,%d,%d,%d\n",name,red,white,red?right-left+1:0,red?bottom-top+1:0);
    ReleaseDC(window,dc);fflush(stdout);
}
int wmain(void)
{
    SetProcessDPIAware();WNDCLASSW klass={0};klass.lpfnWndProc=procedure;
    klass.hInstance=GetModuleHandleW(NULL);klass.lpszClassName=class_name;
    if(!RegisterClassW(&klass))return 1;
    main_frame=create(L"Karyo Caption Main",WS_OVERLAPPEDWINDOW,0,NULL,20,40);
    SetWindowPos(main_frame,NULL,20,40,900,600,SWP_NOZORDER|SWP_NOACTIVATE);
    HWND child=create(L"Child",WS_CHILD|WS_CAPTION|WS_SYSMENU,0,main_frame,20,60);
    HWND tool=create(L"Tool",WS_POPUP|WS_CAPTION|WS_SYSMENU,WS_EX_TOOLWINDOW,main_frame,360,130);
    HWND disabled=create(L"Disabled tool",WS_POPUP|WS_CAPTION|WS_SYSMENU,WS_EX_TOOLWINDOW,main_frame,670,130);
    EnableMenuItem(GetSystemMenu(disabled,FALSE),SC_CLOSE,MF_BYCOMMAND|MF_GRAYED);
    DrawMenuBar(disabled);
    HWND ordinary=create(L"Ordinary",WS_OVERLAPPEDWINDOW,0,NULL,960,320);
    puts("KARYO_CAPTION_READY");fflush(stdout);
    MSG message;DWORD deadline=GetTickCount()+1000;
    while((LONG)(deadline-GetTickCount())>0) {
        while(PeekMessageW(&message,NULL,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}
        Sleep(10);
    }
    observe(child,"child");observe(tool,"tool");observe(disabled,"disabled");observe(ordinary,"ordinary");
    puts("KARYO_CAPTION_DONE");fflush(stdout);
    DestroyWindow(ordinary);DestroyWindow(main_frame);return 0;
}
