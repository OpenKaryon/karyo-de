// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef UNICODE
#define UNICODE
#endif
#define _WIN32_WINNT 0x0a00
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <wchar.h>
static HWND frame,mdi_client,current_target;
static WNDPROC mdi_client_original;
static HBRUSH parent_brush,child_brush;
static const wchar_t *child_class=L"KaryoChildRegion",*mdi_class=L"KaryoMDIRegion";
static const wchar_t *advance_path;
static int current_stage=-1;
static void input_event(HWND window,UINT message)
{
    if(current_stage>=0 && (message==WM_LBUTTONDOWN || message==WM_NCLBUTTONDOWN)) {
        printf("KARYO_ROUND_INPUT={\"stage\":%d,\"target_hit\":%d}\n",current_stage,window==current_target);fflush(stdout);
    }
}
static LRESULT paint(HWND window)
{
    PAINTSTRUCT ps;HDC dc=BeginPaint(window,&ps);
    FillRect(dc,&ps.rcPaint,window==frame?parent_brush:child_brush);EndPaint(window,&ps);return 0;
}
static LRESULT CALLBACK child_proc(HWND w,UINT m,WPARAM a,LPARAM b)
{
    input_event(w,m);if(m==WM_PAINT)return paint(w);return DefWindowProcW(w,m,a,b);
}
static LRESULT CALLBACK mdi_proc(HWND w,UINT m,WPARAM a,LPARAM b)
{
    input_event(w,m);if(m==WM_PAINT)return paint(w);return DefMDIChildProcW(w,m,a,b);
}
static LRESULT CALLBACK client_proc(HWND w,UINT m,WPARAM a,LPARAM b)
{
    input_event(w,m);
    if(m==WM_PAINT){PAINTSTRUCT ps;HDC dc=BeginPaint(w,&ps);FillRect(dc,&ps.rcPaint,parent_brush);EndPaint(w,&ps);return 0;}
    if(m==WM_ERASEBKGND){RECT r;GetClientRect(w,&r);FillRect((HDC)a,&r,parent_brush);return 1;}
    return CallWindowProcW(mdi_client_original,w,m,a,b);
}
static LRESULT CALLBACK frame_proc(HWND w,UINT m,WPARAM a,LPARAM b)
{
    input_event(w,m);if(m==WM_PAINT)return paint(w);
    if(m==WM_DESTROY){PostQuitMessage(0);return 0;}
    return DefFrameProcW(w,mdi_client,m,a,b);
}
static void pump(DWORD milliseconds)
{
    ULONGLONG until=GetTickCount64()+milliseconds;MSG message;
    do {
        while(PeekMessageW(&message,NULL,0,0,PM_REMOVE)) {
            if(message.message==WM_QUIT)ExitProcess(20);
            TranslateMessage(&message);DispatchMessageW(&message);
        }
        Sleep(10);
    }while(GetTickCount64()<until);
}
static void phase(HWND target,const char *name,HWND visible_client,const RECT *previous)
{
    RECT rect,region_bounds={0},old={0};POINT center,points[4],edges[4];int hits[4],edge_hits[4];
    current_target=target;++current_stage;pump(350);
    if(visible_client) {
        if(!GetClientRect(visible_client,&rect))ExitProcess(21);
        MapWindowPoints(visible_client,NULL,(POINT*)&rect,2);
    }else if(!GetWindowRect(target,&rect))ExitProcess(22);
    if(IsRectEmpty(&rect))ExitProcess(23);
    points[0]=(POINT){rect.left,rect.top};points[1]=(POINT){rect.right-1,rect.top};
    points[2]=(POINT){rect.left,rect.bottom-1};points[3]=(POINT){rect.right-1,rect.bottom-1};
    for(int i=0;i<4;++i)hits[i]=WindowFromPoint(points[i])==target;
    edges[0]=(POINT){(rect.left+rect.right)/2,rect.top};edges[1]=(POINT){rect.left,(rect.top+rect.bottom)/2};
    edges[2]=(POINT){(rect.left+rect.right)/2,rect.bottom-1};edges[3]=(POINT){rect.right-1,(rect.top+rect.bottom)/2};
    for(int i=0;i<4;++i)edge_hits[i]=WindowFromPoint(edges[i])==target;
    center=(POINT){(rect.left+rect.right)/2,(rect.top+rect.bottom)/2};
    HRGN region=CreateRectRgn(0,0,0,0);int region_type=GetWindowRgn(target,region);
    if(region_type!=ERROR)GetRgnBox(region,&region_bounds);
    DeleteObject(region);if(previous)old=*previous;
    printf("KARYO_ROUND={\"stage\":%d,\"name\":\"%s\",\"dpi\":%u,\"style\":%lu,\"extended\":%lu,"
           "\"rect\":[%ld,%ld,%ld,%ld],\"corner_hits\":[%d,%d,%d,%d],\"edge_hits\":[%d,%d,%d,%d],\"center_hit\":%d,"
           "\"region_type\":%d,\"region_bounds\":[%ld,%ld,%ld,%ld],\"previous\":[%ld,%ld,%ld,%ld]}\n",
           current_stage,name,GetDpiForWindow(target),(unsigned long)GetWindowLongPtrW(target,GWL_STYLE),
           (unsigned long)GetWindowLongPtrW(target,GWL_EXSTYLE),rect.left,rect.top,rect.right,rect.bottom,
           hits[0],hits[1],hits[2],hits[3],edge_hits[0],edge_hits[1],edge_hits[2],edge_hits[3],WindowFromPoint(center)==target,region_type,
           region_bounds.left,region_bounds.top,region_bounds.right,region_bounds.bottom,
           old.left,old.top,old.right,old.bottom);fflush(stdout);
    ULONGLONG deadline=GetTickCount64()+30000;
    for(;;) {
        int stage=-1;FILE *input=_wfopen(advance_path,L"rt");
        if(input){int read=fscanf(input,"%d",&stage);fclose(input);if(read==1 && stage>=current_stage)break;}
        if(GetTickCount64()>deadline)ExitProcess(24);
        pump(20);
    }
}
static HWND child(const wchar_t *title,DWORD style,DWORD ex,int width,int height)
{
    HWND window=CreateWindowExW(ex,child_class,title,style|WS_CHILD,20,320,width,height,frame,NULL,GetModuleHandleW(NULL),NULL);
    if(!window)ExitProcess(25);
    ShowWindow(window,SW_SHOWNOACTIVATE);UpdateWindow(window);return window;
}
int wmain(int argc,wchar_t **argv)
{
    if(argc!=2)return 1;
    advance_path=argv[1];SetProcessDPIAware();
    parent_brush=CreateSolidBrush(RGB(21,53,87));child_brush=CreateSolidBrush(RGB(55,155,85));
    WNDCLASSW klass={0};klass.hInstance=GetModuleHandleW(NULL);klass.hCursor=LoadCursorW(NULL,IDC_ARROW);
    klass.hbrBackground=parent_brush;klass.lpszClassName=L"KaryoRegionFrame";klass.lpfnWndProc=frame_proc;
    if(!RegisterClassW(&klass))return 2;
    klass.hbrBackground=child_brush;klass.lpszClassName=child_class;klass.lpfnWndProc=child_proc;
    if(!RegisterClassW(&klass))return 3;
    klass.lpszClassName=mdi_class;klass.lpfnWndProc=mdi_proc;if(!RegisterClassW(&klass))return 4;
    frame=CreateWindowW(L"KaryoRegionFrame",L"Karyo child region QA",WS_OVERLAPPEDWINDOW,60,40,1120,650,NULL,NULL,klass.hInstance,NULL);
    if(!frame)return 5;
    ShowWindow(frame,SW_SHOW);UpdateWindow(frame);
    HWND regular=CreateWindowW(child_class,L"Child",WS_CHILD|WS_VISIBLE|WS_CAPTION|WS_SYSMENU|WS_THICKFRAME,20,40,320,200,frame,NULL,klass.hInstance,NULL);
    CLIENTCREATESTRUCT client={0};client.idFirstChild=100;
    mdi_client=CreateWindowExW(WS_EX_CLIENTEDGE,L"MDICLIENT",NULL,WS_CHILD|WS_VISIBLE|WS_CLIPCHILDREN,430,40,620,430,frame,NULL,klass.hInstance,&client);
    if(!regular || !mdi_client)return 6;
    mdi_client_original=(WNDPROC)SetWindowLongPtrW(mdi_client,GWLP_WNDPROC,(LONG_PTR)client_proc);
    MDICREATESTRUCTW creation={0};creation.szClass=mdi_class;creation.szTitle=L"MDI child";creation.hOwner=klass.hInstance;
    creation.x=20;creation.y=20;creation.cx=350;creation.cy=230;creation.style=WS_VISIBLE|WS_OVERLAPPEDWINDOW;
    HWND mdi=(HWND)SendMessageW(mdi_client,WM_MDICREATE,0,(LPARAM)&creation);if(!mdi)return 7;
    phase(regular,"regular",NULL,NULL);
    for(int i=0;i<12;++i){SetWindowPos(regular,NULL,20+(i%2)*4,40,320+(i%5)*8,200+(i%5)*5,SWP_NOZORDER|SWP_NOACTIVATE);pump(15);}
    SetWindowPos(regular,NULL,20,40,370,240,SWP_NOZORDER|SWP_NOACTIVATE);phase(regular,"grow",NULL,NULL);
    RECT old;GetWindowRect(regular,&old);SetWindowPos(regular,NULL,20,40,260,160,SWP_NOZORDER|SWP_NOACTIVATE);phase(regular,"shrink",NULL,&old);
    phase(mdi,"mdi",NULL,NULL);SendMessageW(mdi_client,WM_MDIMAXIMIZE,(WPARAM)mdi,0);phase(mdi,"mdi-max",mdi_client,NULL);
    SendMessageW(mdi_client,WM_MDIRESTORE,(WPARAM)mdi,0);phase(mdi,"mdi-restore",NULL,NULL);
    ShowWindow(mdi,SW_MINIMIZE);phase(mdi,"mdi-min",NULL,NULL);
    SendMessageW(mdi_client,WM_MDIRESTORE,(WPARAM)mdi,0);phase(mdi,"mdi-after-min",NULL,NULL);
    HWND explicit=child(L"Explicit region",WS_CAPTION|WS_SYSMENU,0,260,140);
    RECT bounds;GetWindowRect(explicit,&bounds);int width=bounds.right-bounds.left,height=bounds.bottom-bounds.top;
    if(!SetWindowRgn(explicit,CreateRectRgn(0,0,width,height),TRUE))return 8;
    phase(explicit,"explicit-rect",NULL,NULL);
    HRGN shape=CreateRectRgn(0,0,width,height),hole=CreateRectRgn(width/2-12,height/2-12,width/2+12,height/2+12);
    CombineRgn(shape,shape,hole,RGN_DIFF);DeleteObject(hole);if(!SetWindowRgn(explicit,shape,TRUE))return 9;
    phase(explicit,"explicit-hole",NULL,NULL);SetWindowRgn(explicit,NULL,TRUE);phase(explicit,"explicit-reset",NULL,NULL);DestroyWindow(explicit);
    HWND layered=child(L"Layered",WS_CAPTION|WS_SYSMENU,WS_EX_LAYERED,260,140);
    if(!SetLayeredWindowAttributes(layered,0,255,LWA_ALPHA))return 10;
    phase(layered,"layered",NULL,NULL);DestroyWindow(layered);
    HWND plain=child(L"No caption",WS_BORDER,0,260,140);phase(plain,"no-caption",NULL,NULL);DestroyWindow(plain);
    HWND tiny=child(L"Tiny",WS_CAPTION|WS_SYSMENU,0,4,4);phase(tiny,"tiny",NULL,NULL);DestroyWindow(tiny);
    POINT origin={20,480};ClientToScreen(frame,&origin);
    HWND popup=CreateWindowExW(WS_EX_TOOLWINDOW,child_class,L"Popup",WS_POPUP|WS_CAPTION|WS_SYSMENU,origin.x,origin.y,260,100,frame,NULL,klass.hInstance,NULL);
    if(!popup)return 11;
    ShowWindow(popup,SW_SHOWNOACTIVATE);phase(popup,"popup",NULL,NULL);DestroyWindow(popup);
    current_stage=-1;DestroyWindow(frame);DeleteObject(parent_brush);DeleteObject(child_brush);
    puts("KARYO_ROUND_DONE");fflush(stdout);return 0;
}
