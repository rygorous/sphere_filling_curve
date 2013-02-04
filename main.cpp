#include <Windows.h>
#include <WindowsX.h>
#include <gl/GL.h>
#include <gl/GLU.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <math.h>
#include <vector>

#include "imgui.h"
#include "imguiRenderGL.h"

#pragma comment(lib, "opengl32.lib")
#pragma comment(lib, "glu32.lib")
#pragma comment(lib, "winmm.lib")

static HWND hWnd = 0;
static HDC hDC = 0;
static HGLRC hRC = 0;

static int mouseX = 0, mouseY = 0, mouseButtons = 0;
static int width, height;

static void errorExit(const char *fmt, ...)
{
	char buffer[2048];
	va_list arg;

	va_start(arg, fmt);
	vsprintf_s(buffer, fmt, arg);
	va_end(arg);

	MessageBox(hWnd, buffer, "sfc", MB_ICONERROR|MB_OK);
	exit(1);
}

static void renderFrame();

// ---- windows blurb

static int mouseEvent(LPARAM lParam, int button)
{
	mouseX = GET_X_LPARAM(lParam);
	mouseY = GET_Y_LPARAM(lParam);
	if (button > 0)
		mouseButtons |= button;
	else if (button < 0)
		mouseButtons &= button - 1;

	return 0;
}

static LRESULT CALLBACK windowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch (uMsg) {
	case WM_DESTROY:
		PostQuitMessage(0);
		break;

	case WM_ERASEBKGND:
		return 0;

	case WM_PAINT:
		renderFrame();
		return 0;

	case WM_MOUSEMOVE:		return mouseEvent(lParam, 0);
	case WM_LBUTTONDOWN:	return mouseEvent(lParam, 1);
	case WM_LBUTTONUP:		return mouseEvent(lParam, -1);
	case WM_RBUTTONDOWN:	return mouseEvent(lParam, 2);
	case WM_RBUTTONUP:		return mouseEvent(lParam, -2);

	case WM_SIZE:
		InvalidateRect(hWnd, NULL, FALSE);
		width = LOWORD(lParam);
		height = HIWORD(lParam);
		glViewport(0, 0, width, height);
		break;

	default:
		break;
	}

	return DefWindowProc(hWnd, uMsg, wParam, lParam);
}

static void createWindow(HINSTANCE hInstance)
{
	WNDCLASS wc;

	wc.style = 0;
	wc.lpfnWndProc = windowProc;
	wc.cbClsExtra = 0;
	wc.cbWndExtra = 0;
	wc.hInstance = hInstance;
	wc.hIcon = NULL;
	wc.hCursor = LoadCursor(0, IDC_ARROW);
	wc.hbrBackground = (HBRUSH) GetStockObject(WHITE_BRUSH);
	wc.lpszMenuName = NULL;
	wc.lpszClassName = "tesstest";
	if (!RegisterClass(&wc))
		errorExit("RegisterClass failed!\n");

	RECT rc = { 0, 0, 1024, 768 };
	AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);

	hWnd = CreateWindow("tesstest", "Tess test", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
		rc.right - rc.left, rc.bottom - rc.top, NULL, NULL, hInstance, NULL);
	if (!hWnd)
		errorExit("CreateWindow failed!\n");
}

static void createGLContext()
{
	hDC = GetDC(hWnd);
	PIXELFORMATDESCRIPTOR pfd = { 0 };
	pfd.nSize = sizeof(pfd);
	pfd.nVersion = 1;
	pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
	pfd.iPixelType = PFD_TYPE_RGBA;
	pfd.cColorBits = 24;
	pfd.cDepthBits = 24;
	pfd.iLayerType = PFD_MAIN_PLANE;
	if (!SetPixelFormat(hDC, ChoosePixelFormat(hDC, &pfd), &pfd))
		errorExit("SetPixelFormat failed!\n");

	hRC = wglCreateContext(hDC);
	wglMakeCurrent(hDC, hRC);
}

// ---- octahedron

struct Vec3f
{
	float x, y, z;

	Vec3f() { }
	Vec3f(float x, float y, float z) : x(x), y(y), z(z) { }
};

Vec3f operator * (float f, const Vec3f &v)
{
	return Vec3f(f * v.x, f * v.y, f * v.z);
}

Vec3f operator + (const Vec3f &a, const Vec3f &b)
{
	return Vec3f(a.x + b.x, a.y + b.y, a.z + b.z);
}

static float dot(const Vec3f &a, const Vec3f &b)
{
	return a.x*b.x + a.y*b.y + a.z*b.z;
}

static Vec3f normalize(const Vec3f &v)
{
	return (1.0f / sqrtf(dot(v, v))) * v;
}

struct Mesh
{
	std::vector<Vec3f> verts;
	std::vector<GLuint> inds;

	GLuint addVert(const Vec3f &pos)
	{
		GLuint index = (GLuint)verts.size();
		verts.push_back(pos);
		return index;
	}

	void draw();
	void drawPoints();
};

void Mesh::draw()
{
	if (!inds.size())
		return;

	glEnableClientState(GL_VERTEX_ARRAY);
	glVertexPointer(3, GL_FLOAT, 0, &verts[0]);
	glDrawElements(GL_TRIANGLES, inds.size(), GL_UNSIGNED_INT, &inds[0]);
	glDisableClientState(GL_VERTEX_ARRAY);
}

void Mesh::drawPoints()
{
	if (!verts.size())
		return;

	glEnableClientState(GL_VERTEX_ARRAY);
	glVertexPointer(3, GL_FLOAT, 0, &verts[0]);
	glDrawArrays(GL_POINTS, 0, verts.size());
	glDisableClientState(GL_VERTEX_ARRAY);
}

static void genSubdFace(Mesh &m, GLuint va, GLuint vb, GLuint vc, int depth)
{
	if (depth <= 0) {
		// just write out the face
		m.inds.push_back(va);
		m.inds.push_back(vb);
		m.inds.push_back(vc);
	} else {
		// subdivide some more
		//
		//     c                 c
		//    / \			    /|\
		//   /   \			   / | \
		//  /     \    -->    /  |  \ 
		// a-------b		 a---d---b

		// split edge ab and generate d
		GLuint vd = m.addVert(0.5f * (m.verts[va] + m.verts[vb]));

		// recurse, ordering vertices so that new "ab" (edge to
		// be split) is the hypotenuse
		genSubdFace(m, vc, va, vd, depth - 1);
		genSubdFace(m, vb, vc, vd, depth - 1);
	}
}

static void genSubdOctahedron(Mesh &m, int depth)
{
	float t = sqrtf(0.5f);

	// vertices
	GLuint vDn = m.addVert(Vec3f(0.0f, -1.0f, 0.0f));
	GLuint vBL = m.addVert(Vec3f(-t, 0.0f, -t));
	GLuint vFL = m.addVert(Vec3f(-t, 0.0f,  t));
	GLuint vFR = m.addVert(Vec3f( t, 0.0f,  t));
	GLuint vBR = m.addVert(Vec3f( t, 0.0f, -t));
	GLuint vUp = m.addVert(Vec3f(0.0f, 1.0f, 0.0f));

	// faces
	genSubdFace(m, vBL, vBR, vDn, depth);
	genSubdFace(m, vBR, vFR, vDn, depth);
	genSubdFace(m, vFR, vFL, vDn, depth);
	genSubdFace(m, vFL, vBL, vDn, depth);
	genSubdFace(m, vBL, vFL, vUp, depth);
	genSubdFace(m, vFL, vFR, vUp, depth);
	genSubdFace(m, vFR, vBR, vUp, depth);
	genSubdFace(m, vBR, vBL, vUp, depth);
}

static void spherize(std::vector<Vec3f> &verts)
{
	for (auto it = verts.begin(); it != verts.end(); ++it)
		*it = normalize(*it);
}

static void triTraverse(std::vector<Vec3f> &verts, const Vec3f &a, const Vec3f &b, const Vec3f &c, int depth, bool flip)
{
	if (depth <= 0) {
		Vec3f mid = (1.0f / 3.0f) * (a + b + c);
		verts.push_back(mid);
	} else {
		Vec3f d = 0.5f * (a + b);
		if (!flip) {
			triTraverse(verts, c, a, d, depth - 1, !flip);
			triTraverse(verts, b, c, d, depth - 1, !flip);
		} else {
			triTraverse(verts, b, c, d, depth - 1, !flip);
			triTraverse(verts, c, a, d, depth - 1, !flip);
		}
	}
}

static void sphereTraverseOcta(std::vector<Vec3f> &verts, int depth)
{
	float t = sqrtf(0.5f);

	Vec3f vDn(0.0f, -1.0f, 0.0f);
	Vec3f vBL(-t, 0.0f, -t);
	Vec3f vFL(-t, 0.0f,  t);
	Vec3f vFR( t, 0.0f,  t);
	Vec3f vBR( t, 0.0f, -t);
	Vec3f vUp(0.0f, 1.0f, 0.0f);

	triTraverse(verts, vBL, vBR, vDn, depth, false);
	triTraverse(verts, vBR, vFR, vDn, depth, false);
	triTraverse(verts, vFR, vFL, vDn, depth, false);
	triTraverse(verts, vFL, vBL, vDn, depth, false);
	triTraverse(verts, vBL, vFL, vUp, depth, false);
	triTraverse(verts, vFL, vFR, vUp, depth, false);
	triTraverse(verts, vFR, vBR, vUp, depth, false);
	triTraverse(verts, vBR, vBL, vUp, depth, false);

	spherize(verts);
}

static void sphereTraverseTetra(std::vector<Vec3f> &verts, int depth)
{
	float t = sqrtf(0.5f);

	Vec3f vBL(-1.0f,  0.0f, -t);
	Vec3f vBR( 1.0f,  0.0f, -t);
	Vec3f vFD( 0.0f, -1.0f,  t);
	Vec3f vFU( 0.0f,  1.0f,  t);

	triTraverse(verts, vBL, vFD, vFU, depth, false);
	triTraverse(verts, vFD, vBR, vFU, depth, false);
	triTraverse(verts, vBR, vBL, vFU, depth, false);
	triTraverse(verts, vBL, vBR, vFD, depth, false);

	spherize(verts);
}

// ---- main

static void renderFrame()
{
	glClearColor(0 / 255.0f, 0x2b / 255.0f, 0x36 / 255.0f, 1.0f);
	glClearDepth(1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	// ---- UI

	imguiBeginFrame(mouseX, height - 1 - mouseY, mouseButtons, 0);

	static const int paramSize = 120;
	static int paramScroll = 0;
	imguiBeginScrollArea("Params", 0, height - paramSize, width, paramSize, &paramScroll);

	static float depth = 8.0f;
	imguiSlider("Depth", &depth, 0.0f, 12.0f, 1.0f);

	static float draw_ratio = 1.0f;
	imguiSlider("Draw ratio", &draw_ratio, 0.0f, 1.0f, 0.001f);

	static bool showWire = false;
	showWire ^= imguiCheck("Wireframe", showWire);

	static bool showPoints = false;
	showPoints ^= imguiCheck("Points", showPoints);

	static bool showPath = true;
	showPath ^= imguiCheck("Path", showPath);

	static bool showPathPoints = true;
	showPathPoints ^= imguiCheck("Path points", showPathPoints);

	imguiEndScrollArea();
	imguiEndFrame();

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	glEnable(GL_LINE_SMOOTH);
	glLineWidth(1.2f);

	// prepare to render octahedron
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	gluPerspective(60.0f, 1.333f, 0.01f, 10.0f);

	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	gluLookAt(0.5f, 0.5f, 2.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f);

	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LEQUAL);

	// model rot
	unsigned int loop_len = 30 * 1000;
	float phase = 1.0f * (timeGetTime() % loop_len) / loop_len;
	glRotatef(phase * 360.0f, 0.0f, 1.0f, 0.0f);

	// gen mesh
	Mesh octa;
	genSubdOctahedron(octa, (int) depth);
	spherize(octa.verts);

	// draw mesh as wireframe
	glEnable(GL_CULL_FACE);

	if (showWire) {
		glColor4ub(0xfd, 0xf6, 0xe3, 255);
		glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
		octa.draw();
	}

	// draw vertices
	if (showPoints) {
		glPolygonMode(GL_FRONT_AND_BACK, GL_POINT);

		glEnable(GL_POINT_SMOOTH);
		glHint(GL_POINT_SMOOTH_HINT, GL_NICEST);

		for (int pass=0; pass < 2; ++pass) {
			if (!pass) {
				glColor3ub(0x00, 0x2b, 0x36);
				glPointSize(4.0f);
			} else {
				glColor3ub(0xcb, 0x4b, 0x16);
				glPointSize(2.0f);
			}
			octa.draw();
		}
		glColor4ub(255, 0, 0, 255);
		glPointSize(2.0f);
		octa.draw();
		glDisable(GL_POINT_SMOOTH);
	}

	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

	// draw path
	if (showPath || showPathPoints) {
		std::vector<Vec3f> verts;
		sphereTraverseOcta(verts, (int) depth);

		//triTraverse(verts, Vec3f(1.0f, -1.0f, 0.0f), Vec3f(-1.0f, 1.0f, 0.0f), Vec3f(-1.0f, -1.0f, 0.0f), (int) depth, false);
		//triTraverse(verts, Vec3f(-1.0f, 1.0f, 0.0f), Vec3f(1.0f, -1.0f, 0.0f), Vec3f( 1.0f,  1.0f, 0.0f), (int) depth, false);

		glEnable(GL_POINT_SMOOTH);
		glHint(GL_POINT_SMOOTH_HINT, GL_NICEST);

		static const float bgcol[4] = { 0.0f, 0x2b / 255.0f, 0x36 / 255.0f, 1.0f };

		glEnableClientState(GL_VERTEX_ARRAY);
		glVertexPointer(3, GL_FLOAT, 0, &verts[0]);

		glEnable(GL_FOG);
		glFogi(GL_FOG_MODE, GL_EXP2);
		glFogfv(GL_FOG_COLOR, bgcol);
		glFogf(GL_FOG_DENSITY, 0.4f);

		if (showPath) {
			glLineWidth(1.8f);
			glColor3ub(255, 127, 0);
			glDrawArrays(GL_LINE_STRIP, 0, (GLsizei) (draw_ratio * verts.size()));
		}

		if (showPathPoints) {
			glPointSize(4.0f);
			glColor3ub(255, 0, 0);
			glDrawArrays(GL_POINTS, 0, (GLsizei) (draw_ratio * verts.size()));
		}

		glDisableClientState(GL_VERTEX_ARRAY);
		glDisable(GL_FOG);
		glDisable(GL_POINT_SMOOTH);
	}

	// back to GUI
	glDisable(GL_DEPTH_TEST);

	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();

	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glOrtho(0, width, 0, height, -1.0f, 1.0f);

	imguiRenderGLDraw();

	glDisable(GL_BLEND);

	SwapBuffers(hDC);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
	createWindow(hInstance);
	createGLContext();
	imguiRenderGLInit("c:\\windows\\fonts\\calibri.ttf");

	timeBeginPeriod(1);

	ShowWindow(hWnd, SW_SHOW);

	for (;;) {
		MSG msg;
		while (PeekMessage(&msg, 0, 0, 0, PM_REMOVE)) {
			if (msg.message == WM_QUIT)
				return 0;

			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}

		renderFrame();
	}

	timeEndPeriod(1);
}
