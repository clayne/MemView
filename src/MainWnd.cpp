/*
 * PROJECT:     MemView
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     The main application window
 * COPYRIGHT:   Copyright 2021 Mark Jansen <mark.jansen@reactos.org>
 */

#include "MemView.h"
#include <Commctrl.h>
#include <algorithm>
#include "MemInfo.h"

static HWND g_Static;
static HWND g_Listview;
static std::vector<MemInfo> g_Info;

#ifndef GWL_WNDPROC
#define GWL_WNDPROC         (-4)
#endif

static wchar_t* Columns[] =
{
    L"",
    L"Address",
    L"Size",
    L"Type",
    L"Access",
    L"Initial Acess",
    L"Mapped"
};

static int Sizes[] = {
    16,
    76,
    70,
    40,
    110,
    110,
    600
};


static void UpdateListView()
{
    int Top = ListView_GetTopIndex(g_Listview);
    PBYTE FirstItem = 0;
    if (Top >= 0 && Top < (int)g_Info.size())
        FirstItem = g_Info[Top].start();
    Top = -1;
    int End = -1;

    INT Selected = ListView_GetNextItem(g_Listview, -1, LVNI_SELECTED);
    PBYTE SelectedValue = 0;
    if (Selected >= 0 && Selected < (int)g_Info.size())
        SelectedValue = g_Info[Selected].start();
    Selected = -1;

    std::vector<MemInfo> Info;
    MemInfo::read(g_ProcessHandle, Info);

    for (size_t n = 0; n < Info.size();)
    {
        if (Top < 0 && Info[n].start() == FirstItem)
        {
            Top = (int)n;
            End = Top + ListView_GetCountPerPage(g_Listview);
        }

        if (SelectedValue && SelectedValue == Info[n].start())
            Selected = (int)n;

        bool checkExpand = false;
        if (n < g_Info.size())
        {
            int cmp = g_Info[n].cmp(Info[n]);
            if (!cmp)
            {
                g_Info[n].update(Info[n]);
                checkExpand = true;
                ++n;
            }
            else if (cmp < 0)
            {
                g_Info.erase(g_Info.begin() + n);
            }
            else
            {
                g_Info.insert(g_Info.begin() + n, Info[n]);
                checkExpand = true;
                ++n;
            }
        }
        else
        {
            g_Info.push_back(Info[n]);
            checkExpand = true;
            ++n;
        }
        if (checkExpand)
        {
            PBYTE allocationStart = g_Info[n-1].allocationStart();
            bool isFirstEntryOfMapping = g_Info[n-1].start() == allocationStart;
            if (isFirstEntryOfMapping && n < Info.size())
            {
                g_Info[n-1].CanExpand = Info[n].allocationStart() == allocationStart;

                if (!g_Info[n-1].CanExpand)
                    g_Info[n-1].IsExpanded = false;

                if (!g_Info[n-1].IsExpanded)
                {
                    while (n < Info.size() && Info[n].allocationStart() == allocationStart)
                    {
                        Info.erase(Info.begin() + n);
                    }
                }
            }
        }
    }

    if (g_Info.size() > Info.size())
        g_Info.resize(Info.size());

    SetWindowRedraw(g_Listview, FALSE);
    ListView_SetItemCountEx(g_Listview, g_Info.size(), LVSICF_NOSCROLL);

    if (Top >= 0)
    {
        if (ListView_GetTopIndex(g_Listview) != Top)
        {
            int jump = std::min<int>(End, g_Info.size()-1);
            ListView_EnsureVisible(g_Listview, jump, FALSE); // jump forward
            ListView_EnsureVisible(g_Listview, Top, TRUE); // step back
        }
    }
    INT CurrentSelected = ListView_GetNextItem(g_Listview, -1, LVNI_SELECTED);
    if (Selected >= 0 && Selected != CurrentSelected)
    {
        ListView_SetItemState(g_Listview, CurrentSelected, 0, 0x000F);
        ListView_SetItemState(g_Listview, Selected, LVIS_FOCUSED | LVIS_SELECTED, 0x000F);
    }

    SetWindowRedraw(g_Listview, TRUE);
}

static void HandleSize(HWND hwnd)
{
    RECT client;
    GetClientRect(hwnd, &client);
    LONG  w = client.right - client.left;
    HDWP wp = BeginDeferWindowPos(2);
    LONG ItemHeight = 16;
    wp = DeferWindowPos(wp, g_Static, 0, client.left, client.top, w, ItemHeight, 0);
    client.top += ItemHeight;
    wp = DeferWindowPos(wp, g_Listview, 0, client.left, client.top, w, client.bottom - client.top, 0);
    EndDeferWindowPos(wp);
    ListView_SetColumnWidth(g_Listview, _countof(Columns) - 1, LVSCW_AUTOSIZE_USEHEADER);
}


static WNDPROC g_OriginalProc;
static LRESULT APIENTRY ListViewSubclassProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    if(uMsg == WM_ERASEBKGND)
    {
        return 0l;
    }

    return CallWindowProc(g_OriginalProc, hwnd, uMsg, wParam, lParam);
}


static LRESULT ListviewWM_NOTIFY(HWND hWnd, WPARAM wParam, LPNMHDR lParam)
{
    switch (lParam->code)
    {
    case LVN_GETDISPINFO:
    {
        NMLVDISPINFO* plvdi = (NMLVDISPINFO*)lParam;
        if (plvdi->item.mask & LVIF_TEXT)
        {
            const MemInfo& info = g_Info[plvdi->item.iItem];
            int column = plvdi->item.iSubItem;

            if (column == 0)
            {
                StringCchCopy(plvdi->item.pszText, plvdi->item.cchTextMax, TEXT(""));
            }
            else
            {
                info.columnText(plvdi->item.pszText, plvdi->item.cchTextMax, column - 1);
            }
            return TRUE;
        }
    }
    break;
    case LVN_ODFINDITEM:
    {
        LPNMLVFINDITEM pnmfi = (LPNMLVFINDITEM)lParam;

        // Call a user-defined function that finds the index according to
        // LVFINDINFO (which is embedded in the LPNMLVFINDITEM structure).
        // If nothing is found, then set the return value to -1.
    }
        return -1;
    case  NM_CLICK:
    {
        NMITEMACTIVATE* nm = (NMITEMACTIVATE*)lParam;

        if (nm->iSubItem == 0 && nm->iItem >= 0 && (size_t)nm->iItem < g_Info.size())
        {
            MemInfo& info = g_Info[nm->iItem];
            if (info.CanExpand)
            {
                info.IsExpanded = !info.IsExpanded;
                UpdateListView();
            }
        }
    }
        return TRUE;
    case NM_DBLCLK:
    {
        INT Num = ListView_GetNextItem(g_Listview, -1, LVNI_SELECTED);
        if (Num >= 0 && (size_t)Num < g_Info.size())
            ShowMemory(hWnd, g_Info[Num], g_ProcessHandle, g_ProcessName);
    }
        return TRUE;

    case NM_CUSTOMDRAW:
    {
        LPNMLVCUSTOMDRAW lplvcd = reinterpret_cast<LPNMLVCUSTOMDRAW>(lParam);

        switch (lplvcd->nmcd.dwDrawStage)
        {
        case CDDS_PREPAINT:
            return CDRF_NOTIFYITEMDRAW;
        case CDDS_ITEMPREPAINT:
            return CDRF_NOTIFYSUBITEMDRAW;
        case CDDS_SUBITEM | CDDS_ITEMPREPAINT:
        {
            MemInfo& info = g_Info[lplvcd->nmcd.dwItemSpec];

            if (lplvcd->iSubItem == 0)
            {
                if (info.CanExpand)
                {
                    RECT rc;
                    ListView_GetItemRect(g_Listview, lplvcd->nmcd.dwItemSpec, &rc, LVIR_LABEL);
                    HBRUSH hbrush = (HBRUSH)GetCurrentObject(lplvcd->nmcd.hdc, OBJ_BRUSH);
                    DrawIconEx(lplvcd->nmcd.hdc, rc.left, rc.top, info.IsExpanded ? getCollapseIcon() : getExpandIcon(), 0, 0, 0, hbrush, DI_NORMAL);
                    return CDRF_SKIPDEFAULT;
                }
                else
                {
                    return CDRF_DODEFAULT;
                }
            }

            if (info.isImage())
                lplvcd->clrTextBk = RGB(170, 204, 255);
            else if (info.isMapped())
                lplvcd->clrTextBk = RGB(255, 170, 0);
            else if (info.isPrivate())
                lplvcd->clrTextBk = RGB(255, 255, 170);
            else
                lplvcd->clrTextBk = RGB(255, 255, 255);

            if (lplvcd->iSubItem > 0 && ((info.changed() & MemInfo::Index2Info(lplvcd->iSubItem-1)) != Info::None))
            {
                lplvcd->clrText = RGB(255, 0, 0);
            }
            else
            {
                lplvcd->clrText = RGB(0, 0, 0);
            }
        }
        return CDRF_NEWFONT;
        }
    }
        return CDRF_DODEFAULT;
    }
    return DefWindowProc(hWnd, WM_NOTIFY, wParam, (LPARAM)lParam);
}

LRESULT CALLBACK MainWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_CREATE:
    {
        SetWindowFont(hwnd, getFont(), FALSE);
        setWindowIcons(hwnd);

        RECT client;
        GetClientRect(hwnd, &client);
        LONG  w = client.right - client.left;
        LONG h = client.bottom - client.top;

        g_Listview = CreateWindow(WC_LISTVIEW, L"", WS_CHILD | LVS_REPORT | WS_VISIBLE | LVS_OWNERDATA | WS_TABSTOP,
            client.left, client.top + 16, w, h, hwnd, NULL, g_hInst, NULL);

        g_Static = CreateWindow(WC_STATIC, TEXT(""), WS_CHILD | WS_OVERLAPPED | WS_VISIBLE | SS_NOTIFY,
            client.left, client.top, w, 16, hwnd, NULL, g_hInst, 0);


        g_OriginalProc = (WNDPROC)SetWindowLongPtr(g_Listview, GWL_WNDPROC, (LONG_PTR)ListViewSubclassProc);

        ListView_SetExtendedListViewStyle(g_Listview, ListView_GetExtendedListViewStyle(g_Listview) | LVS_EX_FULLROWSELECT);

        SetWindowFont(g_Static, getFont(), FALSE);
        SetWindowFont(g_Listview, getFont(), FALSE);

        UpdateStatic(g_Static);

        LVCOLUMN lvc;
        for (size_t n = 0; n < _countof(Columns); ++n)
        {
            lvc.iSubItem = n;
            lvc.cx = Sizes[n];
            lvc.fmt = LVCFMT_LEFT;

            if (n == 0)
            {
                lvc.mask = LVCF_FMT | LVCF_WIDTH | LVCF_SUBITEM;
                lvc.pszText = NULL;
            }
            else
            {
                lvc.mask = LVCF_FMT | LVCF_WIDTH | LVCF_TEXT | LVCF_SUBITEM;
                lvc.pszText = Columns[n];
            }

            ListView_InsertColumn(g_Listview, n, &lvc);
        }
        ListView_SetColumnWidth(g_Listview, _countof(Columns) - 1, LVSCW_AUTOSIZE_USEHEADER);
        UpdateListView();
        SetFocus(g_Listview);
        SetTimer(hwnd, 0x1337, 1000, NULL);
    }
    return 0;

    case WM_SIZE:
    {
        HandleSize(hwnd);
    }
    return 0;

    case WM_TIMER:
        if (wParam == 0x1337)
        {
            UpdateListView();
            return TRUE;
        }
        break;
    case WM_COMMAND:
        if ((HWND)lParam == g_Static && HIWORD(wParam) == STN_CLICKED)
        {
            RECT rc;
            GetClientRect(g_Listview, &rc);
            MapWindowPoints(g_Listview, NULL, (LPPOINT)&rc, 2);

            if (UpdateProcessList(hwnd, rc.bottom - rc.top, rc.left, rc.top))
                UpdateStatic(g_Static);
        }
        break;

    case WM_SETCURSOR:
        if ((HWND)wParam == g_Static)
        {
            ::SetCursor(LoadCursorW(NULL, IDC_HAND));
            return TRUE;
        }
        break;

    case WM_NOTIFY:
        if (((LPNMHDR)lParam)->hwndFrom == g_Listview)
            return ListviewWM_NOTIFY(hwnd, wParam, (LPNMHDR)lParam);
        break;

    case WM_DESTROY:
        KillTimer(hwnd, 0x1337);
        SetWindowLongPtr(g_Listview, GWL_WNDPROC, (LONG_PTR)g_OriginalProc);
        DestroyWindow(g_Listview);
        DestroyWindow(g_Static);
        PostQuitMessage(0);
        return 0;

    case WM_ACTIVATE:
        if (LOWORD(wParam) == WA_ACTIVE)
        {
            //SendMessage(hwnd, WM_NEXTDLGCTL, (WPARAM)g_Listview, TRUE);
            SetFocus(g_Listview);
            return 0l;
        }
        break;
    //case WM_ACTIVATEAPP:

    default:
        break;
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

