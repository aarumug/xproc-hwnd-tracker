#include <windows.h>
#include <iostream>
#include <string>
#include <sstream>
#include <userenv.h>
#include <sddl.h>
#include <securitybaseapi.h>

// Custom window message for sharing follower HWND
#define WM_REGISTER_FOLLOWER (WM_USER + 1)

// Global variables
HWND g_hwndMain = NULL;   // First window (main)
HWND g_hwndFollower = NULL;  // Second window (follower)
HWND g_hwndFollowerInChild = NULL; // Follower HWND stored in parent process
HANDLE g_hChildProcess = NULL; // Handle to child process for cleanup
wchar_t g_appContainerName[256] = L"WindowFollower.AppContainer.Fixed"; // Fixed app container name
bool g_VerboseLogs = false;

// Window dimensions
const int WINDOW_WIDTH = 300;
const int WINDOW_HEIGHT = 200;
const int OFFSET_X = 10;

// Function declarations
LRESULT CALLBACK MainWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK FollowerWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
HWND CreateMainWindow(HINSTANCE hInstance);
HWND CreateFollowerWindow(HINSTANCE hInstance);
bool CheckChildProcessParam();
bool CheckLaunchChildAcParam();
bool SpawnChildProcess(bool useAppContainer);
bool SpawnChildProcessNormal();
bool SpawnChildProcessInAppContainer();
void CleanupAppContainer();
void TerminateChildProcess();
int RunParentProcess(HINSTANCE hInstance, int nCmdShow);
int RunChildProcess(HINSTANCE hInstance);

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
  // Check if we have a --child parameter (child process)
  bool isChildProcess = CheckChildProcessParam();

  if (isChildProcess)
  {
    // This is the child process - create follower window and register with parent
    return RunChildProcess(hInstance);
  }
  else
  {
    // This is the parent process - check if we should use app container
    bool useAppContainer = CheckLaunchChildAcParam();

    // Create main window and spawn child
    return RunParentProcess(hInstance, nCmdShow);
  }
}

HWND CreateMainWindow(HINSTANCE hInstance)
{
  return CreateWindowEx(
    0,    // Removed WS_EX_NOREDIRECTIONBITMAP - causing transparency issue
    L"MainWindowClass",     // Class name
    L"Main Window",          // Window title
    WS_OVERLAPPEDWINDOW,   // Style
    CW_USEDEFAULT,      // X position
    CW_USEDEFAULT,// Y position
    WINDOW_WIDTH, // Width
    WINDOW_HEIGHT,// Height
    NULL, // Parent window
    NULL,   // Menu
    hInstance,    // Instance handle
    NULL       // Additional application data
  );
}

HWND CreateFollowerWindow(HINSTANCE hInstance)
{
  return CreateWindowEx(
    0,  // Removed WS_EX_TOOLWINDOW and WS_EX_NOACTIVATE - incompatible with child windows
    L"FollowerWindowClass",   // Class name
    L"Follower Window", // Window title
    WS_POPUP | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,  // Style (removed WS_VISIBLE - will be shown after SetParent)
    100, // X position (will be repositioned after SetParent)
    100, // Y position (will be repositioned after SetParent)
    WINDOW_WIDTH - 6,   // Width (6px smaller than main window)
    WINDOW_HEIGHT - 6, // Height (6px smaller than main window)
    NULL,     // Parent window (will be set via SetParent)
    NULL,// Menu
    hInstance,       // Instance handle
    NULL    // Additional application data
  );
}

LRESULT CALLBACK MainWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  switch (uMsg)
  {
  case WM_REGISTER_FOLLOWER:
  {
    HWND followerHwnd = (HWND)wParam;
    OutputDebugString(L"MainWindowProc: Received WM_REGISTER_FOLLOWER\n");

    wchar_t buffer[256];
    swprintf_s(buffer, L"MainWindowProc: Follower HWND: %p\n", followerHwnd);
    OutputDebugString(buffer);

    // Store the follower HWND for later use
    g_hwndFollowerInChild = followerHwnd;

    // Modify the follower window to be a child window
    LONG_PTR styles = GetWindowLongPtr(followerHwnd, GWL_STYLE);
    styles |= WS_CHILD;  // Add WS_CHILD flag
    styles &= ~WS_POPUP; // Remove WS_POPUP flag
    styles |= WS_VISIBLE; // Ensure it's visible
    styles |= WS_CLIPSIBLINGS; // Prevent clipping by siblings
    SetWindowLongPtr(followerHwnd, GWL_STYLE, styles);

    // Remove extended styles that are incompatible with child windows
    LONG_PTR exStyles = GetWindowLongPtr(followerHwnd, GWL_EXSTYLE);
    exStyles &= ~WS_EX_TOOLWINDOW;
    exStyles &= ~WS_EX_NOACTIVATE;
    SetWindowLongPtr(followerHwnd, GWL_EXSTYLE, exStyles);

    // Set the parent of the follower window
    HWND previousParent = SetParent(followerHwnd, hwnd);

    if (previousParent != NULL || GetLastError() == 0)
    {
      OutputDebugString(L"MainWindowProc: SetParent successful\n");

      // Get client area size
      RECT clientRect;
      GetClientRect(hwnd, &clientRect);

      swprintf_s(buffer, L"MainWindowProc: Client rect: %d x %d\n", clientRect.right, clientRect.bottom);
      OutputDebugString(buffer);

      // Reposition the follower window in client coordinates
      BOOL posResult = SetWindowPos(followerHwnd, HWND_TOP,
        3, 3,  // Client coordinates offset (simplified)
        clientRect.right - 6, clientRect.bottom - 6,  // Resize based on client area
        SWP_NOACTIVATE | SWP_SHOWWINDOW | SWP_FRAMECHANGED);

      if (posResult)
      {
        OutputDebugString(L"MainWindowProc: Follower window positioned and shown\n");

        // Force a redraw
        InvalidateRect(followerHwnd, NULL, TRUE);
        UpdateWindow(followerHwnd);

        // Ensure it's on top
        BringWindowToTop(followerHwnd);
      }
      else
      {
        DWORD error = GetLastError();
        swprintf_s(buffer, L"MainWindowProc: SetWindowPos failed with error: %d\n", error);
        OutputDebugString(buffer);
      }
    }
    else
    {
      DWORD error = GetLastError();
      swprintf_s(buffer, L"MainWindowProc: SetParent failed with error: %d\n", error);
      OutputDebugString(buffer);
    }
  }
  return 0;

  case WM_SIZE:
  {
    // Resize the follower window when the main window is resized
    if (g_hwndFollowerInChild != NULL && wParam != SIZE_MINIMIZED)
    {
      RECT clientRect;
      GetClientRect(hwnd, &clientRect);

      wchar_t buffer[256];
      swprintf_s(buffer, L"MainWindowProc: WM_SIZE (wParam=%lld) - Client rect: %d x %d\n",
        (long long)wParam, clientRect.right, clientRect.bottom);
      OutputDebugString(buffer);

      // Resize follower to match client area with offsets
      BOOL result = SetWindowPos(g_hwndFollowerInChild, HWND_TOP,
        3, 3,  // Position
        clientRect.right - 6, clientRect.bottom - 6,  // Size based on client area
        SWP_NOACTIVATE | SWP_SHOWWINDOW);

      if (result)
      {
        // Force redraw of the follower window
        InvalidateRect(g_hwndFollowerInChild, NULL, TRUE);
        UpdateWindow(g_hwndFollowerInChild);

        swprintf_s(buffer, L"MainWindowProc: Follower window resized to %d x %d\n",
          clientRect.right - 6, clientRect.bottom - 6);
        OutputDebugString(buffer);
      }
      else
      {
        DWORD error = GetLastError();
        swprintf_s(buffer, L"MainWindowProc: SetWindowPos in WM_SIZE failed with error: %d\n", error);
        OutputDebugString(buffer);
      }
    }
  }
  return 0;

  case WM_MOVE:
  {
    // Ensure follower window stays visible when main window is moved
    if (g_hwndFollowerInChild != NULL)
    {
      SetWindowPos(g_hwndFollowerInChild, HWND_TOP, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
      InvalidateRect(g_hwndFollowerInChild, NULL, TRUE);
      OutputDebugString(L"MainWindowProc: Main window moved, refreshing follower\n");
    }
  }
  return 0;

  case WM_WINDOWPOSCHANGED:
  {
    // Only handle z-order changes here, not size changes
    WINDOWPOS* pWinPos = (WINDOWPOS*)lParam;
    if (g_hwndFollowerInChild != NULL && !(pWinPos->flags & SWP_NOSIZE))
    {
      // Window was resized, ensure follower stays visible
      SetWindowPos(g_hwndFollowerInChild, HWND_TOP, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
      RedrawWindow(g_hwndFollowerInChild, NULL, NULL,
        RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
      OutputDebugString(L"MainWindowProc: Window position changed, refreshing follower\n");
    }
    // Let DefWindowProc handle it
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
  }

  case WM_PAINT:
  {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);

    // Fill background
    RECT rect;
    GetClientRect(hwnd, &rect);
    FillRect(hdc, &rect, (HBRUSH)(COLOR_WINDOW + 1));

    // Draw some text to identify the window
    DrawText(hdc, L"Main Window\nMove or resize me!", -1, &rect,
      DT_CENTER | DT_VCENTER | DT_WORDBREAK);

    EndPaint(hwnd, &ps);

    // Ensure follower window stays visible and on top after painting
    if (g_hwndFollowerInChild != NULL)
    {
      SetWindowPos(g_hwndFollowerInChild, HWND_TOP, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
      InvalidateRect(g_hwndFollowerInChild, NULL, TRUE);
    }
  }
  return 0;

  case WM_DESTROY:
    // Terminate the child process before closing
    TerminateChildProcess();
    PostQuitMessage(0);
    return 0;

  default:
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
  }
}

LRESULT CALLBACK FollowerWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  switch (uMsg)
  {
  case WM_ERASEBKGND:
  {
    // Explicitly erase background
    HDC hdc = (HDC)wParam;
    RECT rect;
    GetClientRect(hwnd, &rect);
    FillRect(hdc, &rect, (HBRUSH)(COLOR_BTNFACE + 1));
    return 1;
  }

  case WM_PAINT:
  {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);

    // Fill with a solid color to ensure visibility
    RECT rect;
    GetClientRect(hwnd, &rect);

    // Draw a colored background
    HBRUSH hBrush = CreateSolidBrush(RGB(200, 220, 255)); // Light blue
    FillRect(hdc, &rect, hBrush);
    DeleteObject(hBrush);

    // Draw a border
    HPEN hPen = CreatePen(PS_SOLID, 2, RGB(0, 0, 255)); // Blue border
    HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);
    HBRUSH hOldBrush = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Rectangle(hdc, 0, 0, rect.right, rect.bottom);
    SelectObject(hdc, hOldPen);
    SelectObject(hdc, hOldBrush);
    DeleteObject(hPen);

    // Draw text
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(0, 0, 0)); // Black text
    DrawText(hdc, L"Follower Window\nI follow the main window!", -1, &rect,
      DT_CENTER | DT_VCENTER | DT_WORDBREAK);

    EndPaint(hwnd, &ps);
  }
  return 0;

  case WM_CLOSE:
    // Handle WM_CLOSE to allow graceful shutdown
    OutputDebugString(L"FollowerWindowProc: Received WM_CLOSE\n");
    DestroyWindow(hwnd);
    return 0;

  case WM_DESTROY:
    // In child process, if follower window is destroyed, terminate the child process
    OutputDebugString(L"FollowerWindowProc: Received WM_DESTROY, posting quit message\n");
    PostQuitMessage(0);
    return 0;

  default:
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
  }
}

bool CheckChildProcessParam()
{
  int argc;
  LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);

  if (argv == NULL)
    return false;

  bool isChild = false;
  for (int i = 0; i < argc; i++)
  {
    if (wcscmp(argv[i], L"--child") == 0)
    {
      isChild = true;
      break;
    }
    if (wcscmp(argv[i], L"--verbose") == 0)
    {
      // Enable verbose output
      g_VerboseLogs = true;
    }
  }

  LocalFree(argv);
  return isChild;
}

bool CheckLaunchChildAcParam()
{
  int argc;
  LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);

  if (argv == NULL)
    return false;

  bool useAppContainer = false;
  for (int i = 0; i < argc; i++)
  {
    if (wcscmp(argv[i], L"--launch_child_ac") == 0)
    {
      useAppContainer = true;
      break;
    }
  }

  LocalFree(argv);
  return useAppContainer;
}

bool SpawnChildProcess(bool useAppContainer)
{
  if (useAppContainer)
  {
    OutputDebugString(L"Spawning child process in app container\n");
    return SpawnChildProcessInAppContainer();
  }
  else
  {
    OutputDebugString(L"Spawning child process normally\n");
    return SpawnChildProcessNormal();
  }
}

bool SpawnChildProcessNormal()
{
  wchar_t exePath[MAX_PATH];
  if (GetModuleFileName(NULL, exePath, MAX_PATH) == 0)
  {
    OutputDebugString(L"Failed to get executable path\n");
    return false;
  }

  wchar_t cmdLine[512];
  swprintf_s(cmdLine, L"\"%s\" --child", exePath);

  STARTUPINFO si = { 0 };
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi = { 0 };

  if (!CreateProcess(NULL, cmdLine, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi))
  {
    DWORD error = GetLastError();
    wchar_t errorMsg[256];
    swprintf_s(errorMsg, L"Failed to spawn child process. Error code: %d", error);
    OutputDebugString(errorMsg);
    return false;
  }

  // Store the child process handle for later cleanup
  g_hChildProcess = pi.hProcess;
  CloseHandle(pi.hThread);

  OutputDebugString(L"Child process spawned successfully (normal)\n");
  return true;
}

bool SpawnChildProcessInAppContainer()
{
  wchar_t exePath[MAX_PATH];
  if (GetModuleFileName(NULL, exePath, MAX_PATH) == 0)
  {
    OutputDebugString(L"Failed to get executable path\n");
    return false;
  }

  wchar_t cmdLine[512];
  swprintf_s(cmdLine, L"\"%s\" --child", exePath);

  // Create an app container profile for low trust execution
  PSID appContainerSid = NULL;
  SECURITY_CAPABILITIES securityCapabilities = { 0 };
  STARTUPINFOEX siEx = { 0 };
  PROCESS_INFORMATION pi = { 0 };
  SIZE_T attributeListSize = 0;
  LPPROC_THREAD_ATTRIBUTE_LIST pAttributeList = NULL;
  bool success = false;

  // Create app container profile using fixed name
  HRESULT hr = CreateAppContainerProfile(
    g_appContainerName,    // Profile name
    L"Window Follower App Container",           // Display name
    L"Low trust container for follower window", // Description
    NULL,     // Capabilities (none for low trust)
    0,    // Capability count
    &appContainerSid        // App container SID
  );

  if (FAILED(hr) && hr != HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS))
  {
    wchar_t errorMsg[256];
    swprintf_s(errorMsg, L"Failed to create app container profile. HRESULT: 0x%08X", hr);
    OutputDebugString(errorMsg);
    goto cleanup;
  }

  // If profile already exists, get the SID
  if (hr == HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS))
  {
    hr = DeriveAppContainerSidFromAppContainerName(g_appContainerName, &appContainerSid);
    if (FAILED(hr))
    {
      OutputDebugString(L"Failed to derive app container SID\n");
      goto cleanup;
    }
  }

  // Set up security capabilities with UI-related permissions
  securityCapabilities.AppContainerSid = appContainerSid;
  securityCapabilities.Capabilities = NULL;// Keep as NULL for maximum restriction
  securityCapabilities.CapabilityCount = 0;  // But test if this works
  securityCapabilities.Reserved = 0;

  // Initialize extended startup info
  siEx.StartupInfo.cb = sizeof(STARTUPINFOEX);

  // Determine the size needed for the attribute list
  InitializeProcThreadAttributeList(NULL, 1, 0, &attributeListSize);
  pAttributeList = (LPPROC_THREAD_ATTRIBUTE_LIST)HeapAlloc(GetProcessHeap(), 0, attributeListSize);
  if (!pAttributeList)
  {
    OutputDebugString(L"Failed to allocate memory for attribute list\n");
    goto cleanup;
  }

  // Initialize the attribute list
  if (!InitializeProcThreadAttributeList(pAttributeList, 1, 0, &attributeListSize))
  {
    OutputDebugString(L"Failed to initialize proc thread attribute list\n");
    goto cleanup;
  }

  // Add security capabilities to the attribute list
  if (!UpdateProcThreadAttribute(
    pAttributeList,
    0,
    PROC_THREAD_ATTRIBUTE_SECURITY_CAPABILITIES,
    &securityCapabilities,
    sizeof(securityCapabilities),
    NULL,
    NULL))
  {
    OutputDebugString(L"Failed to update proc thread attribute\n");
    goto cleanup;
  }

  siEx.lpAttributeList = pAttributeList;

  // Create the process in app container
  if (!CreateProcess(
    NULL,    // Application name
    cmdLine, // Command line
    NULL,             // Process security attributes
    NULL,           // Thread security attributes
    FALSE,     // Inherit handles
    EXTENDED_STARTUPINFO_PRESENT,   // Creation flags
    NULL,        // Environment
    NULL,    // Current directory
    &siEx.StartupInfo,    // Startup info
    &pi))     // Process information
  {
    DWORD error = GetLastError();
    wchar_t errorMsg[256];
    swprintf_s(errorMsg, L"Failed to spawn child process in app container. Error code: %d", error);
    OutputDebugString(errorMsg);
    goto cleanup;
  }

  OutputDebugString(L"Child process spawned successfully in low trust app container\n");
  success = true;

  // Store the child process handle for later cleanup
  g_hChildProcess = pi.hProcess;
  CloseHandle(pi.hThread);

cleanup:
  // Clean up resources
  if (pAttributeList)
  {
    DeleteProcThreadAttributeList(pAttributeList);
    HeapFree(GetProcessHeap(), 0, pAttributeList);
  }

  if (appContainerSid)
  {
    FreeSid(appContainerSid);
  }

  return success;
}

void CleanupAppContainer()
{
  // Delete the app container profile when done
  HRESULT hr = DeleteAppContainerProfile(g_appContainerName);
  if (SUCCEEDED(hr))
  {
    OutputDebugString(L"App container profile deleted successfully\n");
  }
  else if (hr == HRESULT_FROM_WIN32(ERROR_NOT_FOUND))
  {
    OutputDebugString(L"App container profile not found (already deleted or never created)\n");
  }
  else
  {
    wchar_t errorMsg[256];
    swprintf_s(errorMsg, L"Failed to delete app container profile. HRESULT: 0x%08X\n", hr);
    OutputDebugString(errorMsg);
  }
}

void TerminateChildProcess()
{
  if (g_hChildProcess != NULL)
  {
    OutputDebugString(L"Terminating child process...\n");

    // Destroy the follower window first to trigger child process exit
    if (g_hwndFollowerInChild != NULL)
    {
      OutputDebugString(L"Destroying follower window to signal child process...\n");
      // Post WM_CLOSE to the follower window to let it exit gracefully
      PostMessage(g_hwndFollowerInChild, WM_CLOSE, 0, 0);

      // Wait a bit for graceful exit
      DWORD waitResult = WaitForSingleObject(g_hChildProcess, 2000);

      if (waitResult == WAIT_TIMEOUT)
      {
        // Force terminate if it didn't exit gracefully
        OutputDebugString(L"Child process didn't exit gracefully, force terminating...\n");
        TerminateProcess(g_hChildProcess, 0);
        WaitForSingleObject(g_hChildProcess, 1000);
      }
    }
    else
    {
      // No follower window handle, just force terminate
      OutputDebugString(L"No follower window handle, force terminating child process...\n");
      TerminateProcess(g_hChildProcess, 0);
      WaitForSingleObject(g_hChildProcess, 1000);
    }

    CloseHandle(g_hChildProcess);
    g_hChildProcess = NULL;
    g_hwndFollowerInChild = NULL;
    OutputDebugString(L"Child process terminated and handle closed\n");
  }
}

int RunParentProcess(HINSTANCE hInstance, int nCmdShow)
{
  // Check if we should use app container
  bool useAppContainer = CheckLaunchChildAcParam();

  // Register main window class
  WNDCLASSEX wcMain = { 0 };
  wcMain.cbSize = sizeof(WNDCLASSEX);
  wcMain.style = CS_HREDRAW | CS_VREDRAW;
  wcMain.lpfnWndProc = MainWindowProc;
  wcMain.hInstance = hInstance;
  wcMain.hCursor = LoadCursor(NULL, IDC_ARROW);
  wcMain.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
  wcMain.lpszClassName = L"MainWindowClass";

  if (!RegisterClassEx(&wcMain))
  {
    MessageBox(NULL, L"Failed to register main window class", L"Error", MB_OK);
    return 1;
  }

  // Create main window
  g_hwndMain = CreateMainWindow(hInstance);
  if (!g_hwndMain)
  {
    MessageBox(NULL, L"Failed to create main window", L"Error", MB_OK);
    return 1;
  }

  // Allow custom message from low IL process
  ChangeWindowMessageFilterEx(g_hwndMain, WM_REGISTER_FOLLOWER, MSGFLT_ALLOW, nullptr);

  OutputDebugString(L"Parent: ChangeWindowMessageFilterEx called for WM_REGISTER_FOLLOWER\n");

  // Show main window
  ShowWindow(g_hwndMain, nCmdShow);
  UpdateWindow(g_hwndMain);

  // Spawn child process (using app container if requested)
  if (!SpawnChildProcess(useAppContainer))
  {
    MessageBox(NULL, L"Failed to spawn child process", L"Error", MB_OK);
    return 1;
  }

  // Message loop for parent process (only handles main window)
  MSG msg;
  while (GetMessage(&msg, NULL, 0, 0))
  {
    TranslateMessage(&msg);
    DispatchMessage(&msg);
  }

  // Cleanup app container when parent process exits (only if we used it)
  if (useAppContainer)
  {
    CleanupAppContainer();
  }

  return (int)msg.wParam;
}

int RunChildProcess(HINSTANCE hInstance)
{
  OutputDebugString(L"Child process starting\n");

  // Register follower window class
  WNDCLASSEX wcFollower = { 0 };
  wcFollower.cbSize = sizeof(WNDCLASSEX);
  wcFollower.style = CS_HREDRAW | CS_VREDRAW;
  wcFollower.lpfnWndProc = FollowerWindowProc;
  wcFollower.hInstance = hInstance;
  wcFollower.hCursor = LoadCursor(NULL, IDC_ARROW);
  wcFollower.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
  wcFollower.lpszClassName = L"FollowerWindowClass";

  if (!RegisterClassEx(&wcFollower))
  {
    MessageBox(NULL, L"Failed to register follower window class", L"Error", MB_OK);
    return 1;
  }

  // Create follower window
  g_hwndFollower = CreateFollowerWindow(hInstance);
  if (!g_hwndFollower)
  {
    MessageBox(NULL, L"Failed to create follower window", L"Error", MB_OK);
    return 1;
  }

  // Show follower window
  ShowWindow(g_hwndFollower, SW_SHOW);
  UpdateWindow(g_hwndFollower);

  // Find the main window by class name
  HWND mainHwnd = FindWindow(L"MainWindowClass", NULL);

  if (mainHwnd == NULL)
  {
    OutputDebugString(L"Child: Failed to find main window\n");
    MessageBox(NULL, L"Failed to find main window", L"Error", MB_OK);
    return 1;
  }

  wchar_t buffer[256];
  swprintf_s(buffer, L"Child: Found main window HWND: %p\n", mainHwnd);
  OutputDebugString(buffer);

  OutputDebugString(L"Child: Sending WM_REGISTER_FOLLOWER to parent\n");

  // Send the follower window handle to the parent process
  // Using SendMessageCallback to avoid blocking and allow message pumping during SetParent
  BOOL sendResult = SendMessageCallback(
    mainHwnd,
    WM_REGISTER_FOLLOWER,
    (WPARAM)g_hwndFollower,
    0,
    nullptr,  // No callback function needed
    0);       // No user data

  if (sendResult)
  {
    OutputDebugString(L"Child: SendMessageCallback succeeded\n");
  }
  else
  {
    DWORD error = GetLastError();
    swprintf_s(buffer, L"Child: SendMessageCallback failed with error: %d\n", error);
    OutputDebugString(buffer);
  }

  // Message loop for child process - CRITICAL for avoiding deadlock
  // This ensures the child process continues pumping messages while
  // the parent process performs SetParent operation
  MSG msg;
  while (GetMessage(&msg, NULL, 0, 0))
  {
    TranslateMessage(&msg);
    DispatchMessage(&msg);
  }

  return (int)msg.wParam;
}