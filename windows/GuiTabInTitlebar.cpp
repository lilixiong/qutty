#include "GuiTabInTitlebar.h"
#include <QApplication>
#include <QStyle>
#include <QDesktopWidget>
#include <QToolBar>
#include <QHBoxLayout>
#include <QWidget>
#include <QDebug>

/*
 * Tabs In Titlebar is only supported in Windows Vista+ with DWM(Aero) enabled
 *  * If DWM/Aero is disabled in Win Vista/7 Tabs will not be in titlebar
 *  * DWM is always enabled (cannot be disabled) in Windows 8 onwards
 *  * So older windows versions are not supported, since that adds more code to maintain
 *
 * Much of the logic is based on below URL
 * http://msdn.microsoft.com/en-us/library/windows/desktop/bb688195%28v=vs.85%29.aspx
 *
 * Following issues are seen:
 * * top, left portion of the mainwindow gets clipped
 * * Also if 'autohide the taskbar' is enabled in Windows, taskbar doesn't popup when
 *   mouse is moved to bottom of screen with application in maximized mode
 * These issues are fixed based on
 * http://stackoverflow.com/questions/137005/auto-hide-taskbar-not-appearing-when-my-application-is-maximized
 */

GuiTabInTitlebar::GuiTabInTitlebar(QMainWindow *mainwindow, QTabWidget *tabarea, QTabBar *tabbar, bool enable)
    : mainWindow(mainwindow),
      tabArea(tabarea),
      tabBar(tabbar),
      tabAreaCornerWidget(NULL),
      isCompositionEnabled(false)
{
    if (!enable || !dwmApi.dwmIsCompositionEnabled())
        return;

    isCompositionEnabled = true;

    tabbar_height = mainWindow->style()->pixelMetric(QStyle::PM_TitleBarHeight);

    mainWindow->setAttribute(Qt::WA_TranslucentBackground, true);

    // Handle window creation.
    RECT rcClient;
    HWND hwnd = (HWND) mainWindow->winId();
    GetWindowRect(hwnd, &rcClient);

    // Inform application of the frame change.
    SetWindowPos(hwnd,
                 NULL,
                 rcClient.left, rcClient.top,
                 rcClient.right - rcClient.left,
                 rcClient.bottom - rcClient.top,
                 SWP_FRAMECHANGED);

    handleWindowStateChangeEvent(mainWindow->windowState());
}

bool GuiTabInTitlebar::handleWinEvent(MSG *msg, long *result)
{
    bool fCallDWP = true;
    LRESULT lRet = 0;
    HRESULT hr = S_OK;
    HWND hWnd       = msg->hwnd;
    UINT message    = msg->message;
    WPARAM wParam   = msg->wParam;
    LPARAM lParam   = msg->lParam;

    if (!isCompositionEnabled)
        return false;

    if (message == WM_NCHITTEST)
    {
        return hitTestNCA(msg, result);
    }

    fCallDWP = !dwmApi.dwmDefWindowProc(hWnd, message, wParam, lParam, &lRet);

    // Handle window activation.
    if (message == WM_ACTIVATE)
    {
        // Extend the frame into the client area.
        MARGINS margins;

        margins.cxLeftWidth     = -1;
        margins.cxRightWidth    = -1;
        margins.cyBottomHeight  = -1;
        margins.cyTopHeight     = -1;

        hr = dwmApi.dwmExtendFrameIntoClientArea(hWnd, &margins);

        if (!SUCCEEDED(hr))
        {
            // Handle error.
        }

        fCallDWP = true;
        lRet = 0;
    }

    // Handle the non-client size message.
    if ((message == WM_NCCALCSIZE) && (wParam == TRUE))
    {
        // No need to pass the message on to the DefWindowProc.
        lRet = 0;
        fCallDWP = false;
    }

    if (message == WM_GETMINMAXINFO) {
        MINMAXINFO *mmi = (MINMAXINFO*)lParam;
        QRect rect = QApplication::desktop()->availableGeometry();
        mmi->ptMaxSize.x = rect.width();
        mmi->ptMaxSize.y = rect.height()-1;
        mmi->ptMaxPosition.x = 0;
        mmi->ptMaxPosition.y = 0;
        lRet = 0;
        fCallDWP = false;
    }

    if (!fCallDWP)
        *result = lRet;

    return !fCallDWP;
}

void GuiTabInTitlebar::setTabAreaCornerWidget(QWidget *w)
{
    if (!isCompositionEnabled) {
        tabArea->setCornerWidget(w, Qt::TopRightCorner);
        return;
    }

    tabAreaCornerWidget = w;
    tabArea->setCornerWidget(w, Qt::TopLeftCorner);
    tabbar_height = tabAreaCornerWidget->sizeHint().height();
}

bool GuiTabInTitlebar::hitTestNCA(MSG *msg, long *result)
{
    // Get the window rectangle.
    RECT rcWindow;
    GetWindowRect(msg->hwnd, &rcWindow);

    // Get the frame rectangle, adjusted for the style without a caption.
    RECT rcFrame = { 0 };
    AdjustWindowRectEx(&rcFrame, WS_OVERLAPPEDWINDOW & ~WS_CAPTION, FALSE, NULL);

    LRESULT lRet = 0;
    if (dwmApi.dwmDefWindowProc(msg->hwnd, msg->message, msg->wParam, msg->lParam, &lRet)) {
        *result = lRet;
        return true;    // handled for Titlebar min-max-close-button area
    }

    int x = GET_X_LPARAM(msg->lParam), y = GET_Y_LPARAM(msg->lParam);
    QPoint p(x - rcWindow.left - window_frame_width, y - rcWindow.top - titlebar_frame_width);

    if (p.x() >= 0 && p.y() >= 0 && p.y() <= tabbar_height) {
        // within tab-left-corner-icon area OR tab-area
        if ( p.x() <= tabAreaCornerWidget->width() ||
             tabBar->tabAt(QPoint(p.x()-tabAreaCornerWidget->width(), p.y())) != -1
           ) {
            *result = HTCLIENT;
            return true;
        }
    }

    USHORT uRow = 1;
    USHORT uCol = 1;

    // Determine if the point is at the top or bottom of the window.
    if (y >= rcWindow.top && y < rcWindow.top + titlebar_frame_width + tabbar_height)
        uRow = 0;
    else if (y < rcWindow.bottom && y >= rcWindow.bottom - window_frame_width)
        uRow = 2;

    // Determine if the point is at the left or right of the window.
    if (x >= rcWindow.left && x < rcWindow.left + window_frame_width)
        uCol = 0;
    else if (x < rcWindow.right && x >= rcWindow.right - window_frame_width)
        uCol = 2;

    LRESULT hitTests[3][3] =
    {
        { HTTOPLEFT,    y < (rcWindow.top - rcFrame.top) ? HTTOP : HTCAPTION,    HTTOPRIGHT },
        { HTLEFT,       HTNOWHERE,      HTRIGHT },
        { HTBOTTOMLEFT, HTBOTTOM,       HTBOTTOMRIGHT },
    };

    *result = hitTests[uRow][uCol];
    return *result != HTNOWHERE;
}

void GuiTabInTitlebar::handleWindowStateChangeEvent(Qt::WindowStates state)
{
    if (state & Qt::WindowMaximized) {
        window_frame_width = 0;
        titlebar_frame_width = 0;
        mainWindow->setContentsMargins(0, 0, 0, 0);
    } else {
        window_frame_width = mainWindow->style()->pixelMetric(QStyle::PM_MdiSubWindowFrameWidth);
        titlebar_frame_width = 3*mainWindow->style()->pixelMetric(QStyle::PM_TitleBarHeight)/4;
        mainWindow->setContentsMargins(window_frame_width, titlebar_frame_width,
                                       window_frame_width, window_frame_width);
    }
}