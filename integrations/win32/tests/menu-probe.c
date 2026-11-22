// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef UNICODE
#define UNICODE
#endif
#include <windows.h>
#include <stdio.h>
static HMENU menus[3];
static unsigned int step;
static BOOL mouse_mode;
static void key( BYTE code )
{
    keybd_event(code,0,0,0);keybd_event(code,0,KEYEVENTF_KEYUP,0);
}
static LRESULT CALLBACK procedure(HWND window,UINT message,WPARAM wparam,LPARAM lparam)
{
    switch(message) {
    case WM_INITMENUPOPUP:
        for(int i=0;i<3;i++)if((HMENU)wparam==menus[i]){printf("KARYO_MENU=%d\n",i);fflush(stdout);}
        break;
    case WM_EXITMENULOOP:puts("KARYO_MENU_EXIT");fflush(stdout);break;
    case WM_TIMER:
        if(wparam==1) {
            KillTimer(window,1);
            if(mouse_mode) {
                for(int i=0;i<3;i++) {
                    RECT rect;if(!GetMenuItemRect(window,GetMenu(window),i,&rect))ExitProcess(3);
                    printf("KARYO_RECT_%d=%ld,%ld\n",i,(rect.left+rect.right)/2,(rect.top+rect.bottom)/2);
                }
                fflush(stdout);SetTimer(window,3,12000,NULL);
            } else {
                SetTimer(window,2,600,NULL);PostMessageW(window,WM_SYSCOMMAND,SC_KEYMENU,L'f');
            }
        } else if(wparam==3) {
            KillTimer(window,3);key(VK_ESCAPE);PostMessageW(window,WM_CLOSE,0,0);
        } else if(wparam==2) {
            if(step++<8)key(VK_RIGHT);
            else {KillTimer(window,2);key(VK_ESCAPE);PostMessageW(window,WM_CLOSE,0,0);}
        }
        return 0;
    case WM_DESTROY:PostQuitMessage(0);return 0;
    }
    return DefWindowProcW(window,message,wparam,lparam);
}
int wmain(int argc,wchar_t **argv)
{
    mouse_mode=argc>1 && !wcscmp(argv[1],L"--mouse");
    HINSTANCE instance=GetModuleHandleW(NULL);
    WNDCLASSW klass={0};klass.lpfnWndProc=procedure;klass.hInstance=instance;
    klass.lpszClassName=L"KaryoMenuQA";klass.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);
    if(!RegisterClassW(&klass))return 1;
    HMENU bar=CreateMenu();const wchar_t *names[]={L"&File",L"&Edit",L"&View"};
    for(int i=0;i<3;i++) {
        menus[i]=CreatePopupMenu();AppendMenuW(menus[i],MF_STRING,100+i,L"QA entry");
        AppendMenuW(bar,MF_POPUP,(UINT_PTR)menus[i],names[i]);
    }
    HWND window=CreateWindowW(klass.lpszClassName,L"Karyo Menu QA",WS_OVERLAPPEDWINDOW,
                            120,100,480,260,NULL,bar,instance,NULL);
    if(!window)return 2;
    ShowWindow(window,SW_SHOW);UpdateWindow(window);SetTimer(window,1,500,NULL);
    MSG message;while(GetMessageW(&message,NULL,0,0)>0){TranslateMessage(&message);DispatchMessageW(&message);}
    puts("KARYO_MENU_PROBE_DONE");return 0;
}
