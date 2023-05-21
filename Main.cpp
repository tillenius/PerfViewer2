#include "Renderer.h"

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <windowsx.h>
#include <algorithm>
#include <unordered_map>
#include <iostream>

const wchar_t * title = L"PerfViewer";
const wchar_t * window_class = L"PerfViewer";

Render g_render;

bool selection = false;
RECT g_rect;
int g_button_down_x = 0;
int g_button_down_y = 0;
float g_start_scale = 1.0f;
float g_bounds[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
uint64_t g_selrowidx = 0;
uint64_t g_seltask = 0;

float selectionx0, selectionx1;
float g_seltime;

struct Entry {
    uint64_t proc = 0;
    uint64_t thread = 0;
    uint64_t start = 0;
    uint64_t length = 0;
    uint32_t name_index = 0;
    uint32_t vert_index = 0;
};

extern std::vector< float > rowpos;
extern std::vector< std::pair< uint64_t, uint64_t > > rowdata;
extern std::vector< std::vector< std::vector< Entry * > > > g_tasksperproc;
extern std::string format(uint64_t a);
extern std::vector<std::string> g_names;

void select_task(unsigned long long proc, unsigned long long thread, size_t pos) {
    std::vector<Entry *> &tasks(g_tasksperproc[proc][thread]);
    g_seltask = pos;
    selection = true;

    const int index = tasks[pos]->name_index;
    const std::string & name = g_names[index];
    std::cout << "(" << proc << ", " << thread
        << ") [ " << format(tasks[pos]->start)
        << ", " << format(tasks[pos]->length) << " ]"
        << " name=[" << name << "]"
        << std::endl;
    g_render.m_selected_index = tasks[pos]->vert_index;

    selectionx0 = (float) tasks[pos]->start;
    selectionx1 = (float) (tasks[pos]->start + tasks[pos]->length);
}

void find_task(size_t rowidx, float time, uint64_t &proc, uint64_t &thread, size_t &pos) {
    if (rowdata.size() <= rowidx)
        return;

    proc = rowdata[rowidx].first;
    thread = rowdata[rowidx].second;

    std::vector<Entry *> &tasks(g_tasksperproc[proc][thread]);

    size_t a = 0;
    size_t b = tasks.size()-1;
    pos = (a+b)/2;

    while (b >= a) {
        pos = (a+b)/2;
        if (1e-3f*tasks[pos]->start > time) {
            if (pos == 0)
                break;
            b = pos-1;
            continue;
        }
        if (1e-3f*tasks[pos]->start + 1e-3f*tasks[pos]->length < time) {
            a = pos+1;
            continue;
        }
        break;
    }
    if (pos > 0) {
        if (fabs(time - 1e-3f*tasks[pos-1]->start + 1e-3f*tasks[pos-1]->length) <
            fabs(time - 1e-3f*tasks[pos]->start))
            pos = pos - 1;
    }
    if (pos < tasks.size()-1) {
        if (fabs(time - 1e-3f*tasks[pos+1]->start) <
            fabs(time - (1e-3f*tasks[pos]->start+1e-3f*tasks[pos]->length)))
            pos = pos + 1;
    }
}

static void get_coords(int x, int y, float & fx, float & fy) {
    const float width = float(g_rect.right-g_rect.left);
    const float height = float(g_rect.bottom-g_rect.top);
    const float xPos = x/width*2.0f - 1.0f;
    const float yPos = y/height*2.0f - 1.0f;
    fx = (xPos - g_render.m_x)/g_render.m_sx;
    fy = (yPos - g_render.m_y)/g_render.m_sy;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_CLOSE:
            PostQuitMessage(0);
            break;
        case WM_GETMINMAXINFO:
            ((MINMAXINFO*)lParam)->ptMinTrackSize = POINT{ 100, 100 };
            return 0;
        case WM_ERASEBKGND:
            ValidateRect(hwnd, NULL);
            return 1;
        case WM_SIZE:
            GetClientRect(hwnd, &g_rect);
            g_render.draw();
            break;
        case WM_PAINT:
            g_render.draw();
            ValidateRect(hwnd, NULL);
            return 0;
        case WM_CHAR: {
            switch (wParam) {
                case '*': {
                    g_render.m_x = -0.9f;
                    g_render.m_y = -0.9f;
                    g_render.m_sx = 1.8f/(g_bounds[2]-g_bounds[0]);
                    g_render.m_sy = 1.8f/(g_bounds[3]-g_bounds[1]);
                    break;
                }
            }
            InvalidateRect(hwnd, NULL, FALSE);
            break;
        }
        case WM_KEYDOWN: {
            switch (wParam) {
                case VK_UP: {
                    if (g_selrowidx == 0)
                        break;
                    --g_selrowidx;
                    uint64_t proc;
                    uint64_t thread;
                    size_t pos;
                    find_task(g_selrowidx, (selectionx0 + selectionx1) / 2.0f, proc, thread, pos);
                    select_task(proc, thread, pos);
                    break;
                }
                case VK_DOWN: {
                    if (g_selrowidx + 1 >= rowdata.size())
                        break;
                    ++g_selrowidx;
                    uint64_t proc;
                    uint64_t thread;
                    size_t pos;
                    find_task(g_selrowidx, (selectionx0 + selectionx1) / 2.0f, proc, thread, pos);
                    select_task(proc, thread, pos);
                    break;
                }
                case VK_LEFT: {
                    uint64_t proc = rowdata[g_selrowidx].first;
                    uint64_t thread = rowdata[g_selrowidx].second;
                    if (g_seltask == 0)
                        break;
                    --g_seltask;
                    select_task(proc, thread, g_seltask);
                    break;
                }
                case VK_RIGHT: {
                    uint64_t proc = rowdata[g_selrowidx].first;
                    uint64_t thread = rowdata[g_selrowidx].second;
                    std::vector<Entry *> &tasks(g_tasksperproc[proc][thread]);
                    if (g_seltask+1 == tasks.size())
                        break;
                    ++g_seltask;
                    select_task(proc, thread, g_seltask);
                    break;
                }
            }
            InvalidateRect(hwnd, NULL, FALSE);
            break;
        }
        case WM_MOUSEWHEEL: {
            POINT p{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ScreenToClient(hwnd, &p);

            const float fx = 2.0f*p.x/float(g_rect.right-g_rect.left)-1.0f;
            const float x = (fx - g_render.m_x)/g_render.m_sx;

            const int zDelta = GET_WHEEL_DELTA_WPARAM(wParam);
            const float oldSx = g_render.m_sx;
            const float factor = 1.0f + (0.1f * zDelta / WHEEL_DELTA);
            g_render.m_sx *= factor;
            if (g_render.m_sx < 0.00000001f) {
                g_render.m_sx = 0.00000001f;
            }

            g_render.m_x = fx - g_render.m_sx/oldSx*(fx-g_render.m_x);
            InvalidateRect(hwnd, NULL, FALSE);
            break;
        }
        case WM_RBUTTONDOWN:
        case WM_LBUTTONDOWN: {
            g_button_down_x = GET_X_LPARAM(lParam);
            g_button_down_y = GET_Y_LPARAM(lParam);
            g_start_scale = g_render.m_sx;

            if (message == WM_LBUTTONDOWN) {
                float xx, yy;
                get_coords(g_button_down_x, g_button_down_y, xx, yy);
                size_t bestidx = 0;
                float best = fabs(rowpos[0] - yy);

                for (size_t i = 0; i < rowpos.size(); ++i) {
                    float dist = fabs(rowpos[i] - yy);
                    if (dist < best) {
                        bestidx = i;
                        best = dist;
                    }
                }
                g_selrowidx = bestidx;

                unsigned long long proc;
                unsigned long long thread;
                size_t pos;
                find_task(g_selrowidx, xx, proc, thread, pos);

                g_seltask = pos;
                g_seltime = xx;

                //std::cout<<"pos=" << xx << ", " << yy << std::endl;
                select_task(proc, thread, pos);
            }
            InvalidateRect(hwnd, NULL, FALSE);
            break;
        }
        case WM_MOUSEMOVE: {
            const int xPos = GET_X_LPARAM(lParam);
            const int yPos = GET_Y_LPARAM(lParam);
            float xx, yy;
            get_coords(xPos, yPos, xx, yy);

            if (wParam & MK_LBUTTON) {
                const int dx = xPos - g_button_down_x;
                const int dy = yPos - g_button_down_y;
                g_button_down_x = xPos;
                g_button_down_y = yPos;
                g_render.m_x += 2.0f*dx/(g_rect.right-g_rect.left);
                InvalidateRect(hwnd, NULL, FALSE);
            } else if (wParam & MK_RBUTTON) {
                const float fx = 2.0f*g_button_down_x/float(g_rect.right-g_rect.left)-1.0f;
                const int orig = g_button_down_x-50;
                const int dx = xPos - orig;
                const float oldSx = g_render.m_sx;
                if (dx > 0) {
                    g_render.m_sx = g_start_scale * (dx/50.0f)*(dx/50.0f);
                } else if (dx < 0) {
                    g_render.m_sx = g_start_scale * -(1.0f/(50.0f*dx));
                }
                g_render.m_x = fx - g_render.m_sx/oldSx*(fx-g_render.m_x);
                InvalidateRect(hwnd, NULL, FALSE);
            }
            break;
        }
        case WM_DESTROY:
            PostQuitMessage(0);
            break;
        default:
            break;
    }
    return DefWindowProc(hwnd, message, wParam, lParam);
}

ATOM register_class(HINSTANCE hinstance) {
    WNDCLASSEXW wcex = {0};

    wcex.cbSize = sizeof(WNDCLASSEX);

    wcex.style          = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc    = WndProc;
    wcex.hInstance      = hinstance;
    wcex.hCursor        = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground  = (HBRUSH)(COLOR_WINDOW+1);
    wcex.lpszClassName  = window_class;

    return RegisterClassExW(&wcex);
}

extern bool parse(const char * filename, std::vector<vertex_t> & vertices, std::vector<uint32_t> & indices_line, std::vector<uint32_t> & indices_tri);

int main(int argc, const char * argv[]) {
    std::vector<vertex_t> vertices;
    std::vector<uint32_t> indices_line;
    std::vector<uint32_t> indices_tri;

    const char * filename = "g:/dump.log";
    if (argc>1) {
        filename = argv[1];
    }

    if (!parse(filename, vertices, indices_line, indices_tri)) {
        return 0;
    }

    const HINSTANCE hinstance = GetModuleHandle(NULL);
    const ATOM win_class = register_class(hinstance);

    if (win_class == 0) {
        return 1;
    }

    const HWND hwnd = CreateWindowW(window_class, title, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, 0, CW_USEDEFAULT, 0, nullptr, nullptr, hinstance, nullptr);
    if (hwnd == NULL) {
        return 1;
    }

    if (!g_render.init(hinstance, hwnd, vertices, indices_line, indices_tri)) {
        return 1;
    }

    g_bounds[0] = vertices[0].pos.x;
    g_bounds[1] = vertices[0].pos.y;
    g_bounds[2] = vertices[0].pos.x;
    g_bounds[3] = vertices[0].pos.y;
    for ( const vertex_t & v : vertices ) {
        if (v.pos.x < g_bounds[0]) {
            g_bounds[0] = v.pos.x;
        }
        if (v.pos.y < g_bounds[1]) {
            g_bounds[1] = v.pos.y;
        }
        if (v.pos.x > g_bounds[2]) {
            g_bounds[2] = v.pos.x;
        }
        if (v.pos.y > g_bounds[3]) {
            g_bounds[3] = v.pos.y;
        }
    }
    g_render.m_x = -0.9f;
    g_render.m_y = -0.9f;
    g_render.m_sx = 1.8f/(g_bounds[2]-g_bounds[0]);
    g_render.m_sy = 1.8f/(g_bounds[3]-g_bounds[1]);

    ShowWindow(hwnd, SW_NORMAL);
    UpdateWindow(hwnd);

    MSG msg;
    BOOL bRet;
    while( (bRet = GetMessage( &msg, NULL, 0, 0 )) != 0) { 
        if (bRet == -1) {
            break;
        } else {
            TranslateMessage(&msg); 
            DispatchMessage(&msg); 
        }
    } 

    return 0;
}
