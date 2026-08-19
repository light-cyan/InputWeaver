// Original experimental mouse-hook prototype. It is retained as reference material and is not part of the application implementation.

#include <Windows.h>
#include <iostream>
#include <chrono>
#include <cmath>

// 全局变量存储鼠标状态
POINT g_lastMousePos = { 0, 0 };          // 上次鼠标位置
LONG g_lastWheelDelta = 0;              // 上次滚轮值
auto g_lastMoveTime = std::chrono::steady_clock::now(); // 上次移动时间
auto g_lastWheelTime = std::chrono::steady_clock::now(); // 上次滚轮时间

// 定义监控区域 (示例: 屏幕中心400x400区域)
RECT g_monitorRect = {
    (GetSystemMetrics(SM_CXSCREEN) - 1000) / 2, // 左上角X
    (GetSystemMetrics(SM_CYSCREEN) - 1000) / 2, // 左上角Y
    (GetSystemMetrics(SM_CXSCREEN) + 1000) / 2, // 右下角X
    (GetSystemMetrics(SM_CYSCREEN) + 1000) / 2  // 右下角Y
};

// 鼠标钩子处理函数
LRESULT CALLBACK LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam) {
    // 检查事件是否有效
    if (nCode < HC_ACTION) {
        // 无效事件，传递给下一个钩子
        return CallNextHookEx(NULL, nCode, wParam, lParam);
    }

    // 解析鼠标事件结构体
    MSLLHOOKSTRUCT* pMouseInfo = (MSLLHOOKSTRUCT*)lParam;

    // 获取当前时间
    auto now = std::chrono::steady_clock::now();

    // 处理鼠标移动事件
    if (wParam == WM_MOUSEMOVE) {
        // 计算时间差 (毫秒)
        auto timeDiff = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - g_lastMoveTime).count();

        // 计算移动距离
        LONG dx = pMouseInfo->pt.x - g_lastMousePos.x;
        LONG dy = pMouseInfo->pt.y - g_lastMousePos.y;

        // 计算速度 (像素/秒)
        double speedX = (timeDiff > 0) ? (dx * 1000.0) / timeDiff : 0;
        double speedY = (timeDiff > 0) ? (dy * 1000.0) / timeDiff : 0;
        double totalSpeed = sqrt(speedX * speedX + speedY * speedY);

        // 检查是否在监控区域内
        bool inArea = PtInRect(&g_monitorRect, pMouseInfo->pt);

        // 打印移动信息
        std::cout << "鼠标移动: "
            << "X=" << pMouseInfo->pt.x << ", "
            << "Y=" << pMouseInfo->pt.y << ", "
            << "速度X=" << speedX << "px/s, "
            << "速度Y=" << speedY << "px/s, "
            << "总速度=" << totalSpeed << "px/s, "
            << "在区域中: " << (inArea ? "是" : "否")
            << std::endl;

        // 更新位置和时间
        g_lastMousePos = pMouseInfo->pt;
        g_lastMoveTime = now;
    }
    // 处理鼠标滚轮事件
    else if (wParam == WM_MOUSEWHEEL) {
        // 提取滚轮增量 (高位字)
        SHORT delta = HIWORD(pMouseInfo->mouseData);

        // 计算时间差 (毫秒)
        auto timeDiff = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - g_lastWheelTime).count();

        // 计算滚轮速度 (10单位/0.1秒)
        double wheelSpeed = (timeDiff > 0) ?
            (delta / 10.0) / (timeDiff / 100.0) :
            (delta > 0) ? INFINITY : -INFINITY;

        // 打印滚轮信息
        std::cout << "滚轮滚动: "
            << "Delta=" << delta << ", "
            << "速度=" << wheelSpeed << "单位/s"
            << std::endl;

        // 更新时间
        g_lastWheelTime = now;
    }

    // 传递事件给下一个钩子
    return CallNextHookEx(NULL, nCode, wParam, lParam);
}

int main() {
    // 安装低级鼠标钩子
    HHOOK mouseHook = SetWindowsHookEx(
        WH_MOUSE_LL,       // 钩子类型: 低级鼠标钩子
        LowLevelMouseProc,  // 钩子处理函数
        GetModuleHandle(NULL), // 当前模块句柄
        0                  // 线程ID (0=全局钩子)
    );

    if (!mouseHook) {
        std::cerr << "钩子安装失败! 错误代码: " << GetLastError() << std::endl;
        return 1;
    }

    std::cout << "鼠标监控已启动. 按任意键退出..." << std::endl;

    // 消息循环 (保持程序运行)
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    // 卸载钩子
    UnhookWindowsHookEx(mouseHook);

    return 0;
}

/*
#include <Windows.h>
#include <iostream>
#include <chrono>
#include <cmath>

// 获取屏幕尺寸
int g_screenWidth = GetSystemMetrics(SM_CXSCREEN);
int g_screenHeight = GetSystemMetrics(SM_CYSCREEN);

// 全局变量存储鼠标状态
POINT g_lastMousePos = {0, 0};          // 上次鼠标位置
LONG g_lastWheelDelta = 0;              // 上次滚轮值
auto g_lastMoveTime = std::chrono::steady_clock::now(); // 上次移动时间

// 鼠标钩子处理函数
LRESULT CALLBACK LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode < HC_ACTION) {
        return CallNextHookEx(NULL, nCode, wParam, lParam);
    }

    MSLLHOOKSTRUCT* pMouseInfo = (MSLLHOOKSTRUCT*)lParam;
    auto now = std::chrono::steady_clock::now();

    if (wParam == WM_MOUSEMOVE) {
        // 计算时间差 (毫秒)
        auto timeDiff = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - g_lastMoveTime).count();

        // 计算移动距离 (像素)
        LONG dx = pMouseInfo->pt.x - g_lastMousePos.x;
        LONG dy = pMouseInfo->pt.y - g_lastMousePos.y;

        // 计算屏幕百分比移动量
        double percentX = static_cast<double>(dx) / g_screenWidth * 100.0;
        double percentY = static_cast<double>(dy) / g_screenHeight * 100.0;

        // 计算速度 (屏幕百分比/0.1秒)
        double speedX = (timeDiff > 0) ? (percentX * 100.0) / timeDiff : 0;
        double speedY = (timeDiff > 0) ? (percentY * 100.0) / timeDiff : 0;

        // 打印移动信息
        std::cout << "鼠标移动: "
                  << "X=" << pMouseInfo->pt.x << ", "
                  << "Y=" << pMouseInfo->pt.y << ", "
                  << "速度X=" << speedX << "%/0.1s, "
                  << "速度Y=" << speedY << "%/0.1s"
                  << std::endl;

        // 更新位置和时间
        g_lastMousePos = pMouseInfo->pt;
        g_lastMoveTime = now;
    }

    return CallNextHookEx(NULL, nCode, wParam, lParam);
}



*/
