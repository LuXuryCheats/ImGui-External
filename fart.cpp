#include <iostream>
#include <Windows.h>
#include "Utils.h"
#include "d3d.h"
#include <dwmapi.h>
#include <vector>
#include "offsets.h"
#include "settings.h"

static HWND Window = NULL;
IDirect3D9Ex* pObject = NULL;
static LPDIRECT3DDEVICE9 D3DDevice = NULL;
static LPDIRECT3DVERTEXBUFFER9 VertBuff = NULL;

#define OFFSET_UWORLD 0x9540700
int localplayerID;

DWORD_PTR Uworld;
DWORD_PTR LocalPawn;
DWORD_PTR Localplayer;
DWORD_PTR Rootcomp;
DWORD_PTR PlayerController;
DWORD_PTR Ulevel;

Vector3 localactorpos;
Vector3 Localcam;

static void WindowMain();
static void InitializeD3D();
static void Loop();
static void ShutDown();

FTransform GetBoneIndex(DWORD_PTR mesh, int index)
{
	DWORD_PTR bonearray = read<DWORD_PTR>(DrverInit, FNProcID, mesh + 0x4A8);  // 4A8  changed often 4u

	if (bonearray == NULL) // added 4u
	{
		bonearray = read<DWORD_PTR>(DrverInit, FNProcID, mesh + 0x4A8 + 0x10); // added 4u
	}

	return read<FTransform>(DrverInit, FNProcID, bonearray + (index * 0x30));  // doesn't change
}

Vector3 GetBoneWithRotation(DWORD_PTR mesh, int id)
{
	FTransform bone = GetBoneIndex(mesh, id);
	FTransform ComponentToWorld = read<FTransform>(DrverInit, FNProcID, mesh + 0x1C0);  // have never seen this change 4u

	D3DMATRIX Matrix;
	Matrix = MatrixMultiplication(bone.ToMatrixWithScale(), ComponentToWorld.ToMatrixWithScale());

	return Vector3(Matrix._41, Matrix._42, Matrix._43);
}

D3DMATRIX Matrix(Vector3 rot, Vector3 origin = Vector3(0, 0, 0))
{
	float radPitch = (rot.x * float(M_PI) / 180.f);
	float radYaw = (rot.y * float(M_PI) / 180.f);
	float radRoll = (rot.z * float(M_PI) / 180.f);

	float SP = sinf(radPitch);
	float CP = cosf(radPitch);
	float SY = sinf(radYaw);
	float CY = cosf(radYaw);
	float SR = sinf(radRoll);
	float CR = cosf(radRoll);

	D3DMATRIX matrix;
	matrix.m[0][0] = CP * CY;
	matrix.m[0][1] = CP * SY;
	matrix.m[0][2] = SP;
	matrix.m[0][3] = 0.f;

	matrix.m[1][0] = SR * SP * CY - CR * SY;
	matrix.m[1][1] = SR * SP * SY + CR * CY;
	matrix.m[1][2] = -SR * CP;
	matrix.m[1][3] = 0.f;

	matrix.m[2][0] = -(CR * SP * CY + SR * SY);
	matrix.m[2][1] = CY * SR - CR * SP * SY;
	matrix.m[2][2] = CR * CP;
	matrix.m[2][3] = 0.f;

	matrix.m[3][0] = origin.x;
	matrix.m[3][1] = origin.y;
	matrix.m[3][2] = origin.z;
	matrix.m[3][3] = 1.f;

	return matrix;
}

//4u note:  changes to projectw2s and camera are the most diffucult changes to understand reworking old camloc, be careful blindly making edits

extern Vector3 CameraEXT(0, 0, 0);
float FovAngle;
Vector3 ProjectWorldToScreen(Vector3 WorldLocation, Vector3 camrot)
{
	Vector3 Screenlocation = Vector3(0, 0, 0);
	Vector3 Camera;

	auto chain69 = read<uintptr_t>(DrverInit, FNProcID, Localplayer + 0xa8);
	uint64_t chain699 = read<uintptr_t>(DrverInit, FNProcID, chain69 + 8);

	Camera.x = read<float>(DrverInit, FNProcID, chain699 + 0x7F8);  //camera pitch  watch out for x and y swapped 4u
	Camera.y = read<float>(DrverInit, FNProcID, Rootcomp + 0x12C);  //camera yaw

	float test = asin(Camera.x);
	float degrees = test * (180.0 / M_PI);
	Camera.x = degrees;

	if (Camera.y < 0)
		Camera.y = 360 + Camera.y;

	D3DMATRIX tempMatrix = Matrix(Camera);
	Vector3 vAxisX, vAxisY, vAxisZ;

	vAxisX = Vector3(tempMatrix.m[0][0], tempMatrix.m[0][1], tempMatrix.m[0][2]);
	vAxisY = Vector3(tempMatrix.m[1][0], tempMatrix.m[1][1], tempMatrix.m[1][2]);
	vAxisZ = Vector3(tempMatrix.m[2][0], tempMatrix.m[2][1], tempMatrix.m[2][2]);

	uint64_t chain = read<uint64_t>(DrverInit, FNProcID, Localplayer + 0x70);
	uint64_t chain1 = read<uint64_t>(DrverInit, FNProcID, chain + 0x98);
	uint64_t chain2 = read<uint64_t>(DrverInit, FNProcID, chain1 + 0x130);

	Vector3 vDelta = WorldLocation - read<Vector3>(DrverInit, FNProcID, chain2 + 0x10); //camera location credits for Object9999
	Vector3 vTransformed = Vector3(vDelta.Dot(vAxisY), vDelta.Dot(vAxisZ), vDelta.Dot(vAxisX));

	if (vTransformed.z < 1.f)
		vTransformed.z = 1.f;

	float zoom = read<float>(DrverInit, FNProcID, chain699 + 0x590);

	FovAngle = 80.0f / (zoom / 1.19f);
	float ScreenCenterX = Width / 2.0f;
	float ScreenCenterY = Height / 2.0f;

	Screenlocation.x = ScreenCenterX + vTransformed.x * (ScreenCenterX / tanf(FovAngle * (float)M_PI / 360.f)) / vTransformed.z;
	Screenlocation.y = ScreenCenterY - vTransformed.y * (ScreenCenterX / tanf(FovAngle * (float)M_PI / 360.f)) / vTransformed.z;
	CameraEXT = Camera;

	return Screenlocation;
}


DWORD GUI(LPVOID in)
{
	while (1)
	{
		if (GetAsyncKeyState(VK_INSERT) & 1) {
			Settings::ShowMenu = !Settings::ShowMenu;
		}
		Sleep(2);
	}
}


std::string GetGNamesByObjID(int32_t ObjectID)
{
	return 0;
}


typedef struct _FNlEntity
{
	uint64_t Actor;
	int ID;
	uint64_t mesh;
}FNlEntity;

std::vector<FNlEntity> entityList;

#define DEBUG

int main(int argc, const char* argv[])
{
	Sleep(300);  //slowed it down a bit 4u
	SetConsoleTitleA("Overlay Console");
	system("Color b");
	printf("\n\n fart.club | 1.0");
	printf("\n Keep this Console and Task Manager open or the cheat will close!");
	printf("\n\n\ Status: Testing");
	Sleep(3000);

	CreateThread(NULL, NULL, GUI, NULL, NULL, NULL);
	DrverInit = CreateFileW((L"\\\\.\\matchdriver123"), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

	if (DrverInit == INVALID_HANDLE_VALUE)
	{
		printf("\n Driver Failure");
		Sleep(2000);
		exit(0);
	}

	info_t Input_Output_Data1;
	unsigned long int Readed_Bytes_Amount1;
	DeviceIoControl(DrverInit, ctl_clear, &Input_Output_Data1, sizeof Input_Output_Data1, &Input_Output_Data1, sizeof Input_Output_Data1, &Readed_Bytes_Amount1, nullptr);


	while (hWnd == NULL)
	{
		hWnd = FindWindowA(0, ("Fortnite  "));
		system("cls");
		printf("\n Looking for Fortnite Process!");
		Sleep(1000);
		printf("\n Process Found, Happy Cheating.");
	}
	GetWindowThreadProcessId(hWnd, &FNProcID);

	info_t Input_Output_Data;
	Input_Output_Data.pid = FNProcID;
	unsigned long int Readed_Bytes_Amount;

	DeviceIoControl(DrverInit, ctl_base, &Input_Output_Data, sizeof Input_Output_Data, &Input_Output_Data, sizeof Input_Output_Data, &Readed_Bytes_Amount, nullptr);
	base_address = (unsigned long long int)Input_Output_Data.data;
	std::printf(("Process base address: %p.\n"), (void*)base_address);


	//HANDLE handle = CreateThread(nullptr, NULL, reinterpret_cast<LPTHREAD_START_ROUTINE>(cache), nullptr, NULL, nullptr);
	//CloseHandle(handle);

	WindowMain();
	InitializeD3D();

	Loop();
	ShutDown();

	return 0;

}

HWND GameWnd = NULL;
void Window2Target()
{
	while (true)
	{
		hWnd = FindWindowW(TEXT("UnrealWindow"), NULL);
		if (hWnd)
		{
			ZeroMemory(&ProcessWH, sizeof(ProcessWH));
			GetWindowRect(hWnd, &ProcessWH);
			Width = ProcessWH.right - ProcessWH.left;
			Height = ProcessWH.bottom - ProcessWH.top;
			DWORD dwStyle = GetWindowLong(hWnd, GWL_STYLE);

			if (dwStyle & WS_BORDER)
			{
				ProcessWH.top += 32;
				Height -= 39;
			}
			ScreenCenterX = Width / 2;
			ScreenCenterY = Height / 2;
			MoveWindow(Window, ProcessWH.left, ProcessWH.top, Width, Height, true);
		}
		else
		{
			exit(0);
		}
	}
}


const MARGINS Margin = { -1 };
/*void WindowMain()
{
	CreateThread(0, 0, (LPTHREAD_START_ROUTINE)Window2Target, 0, 0, 0);

	WNDCLASSEX ctx;
	ZeroMemory(&ctx, sizeof(ctx));
	ctx.cbSize = sizeof(ctx);
	ctx.lpszClassName = L"fart.club";
	ctx.lpfnWndProc = WindowProc;
	RegisterClassEx(&ctx);

	if (hWnd)
	{
		GetClientRect(hWnd, &ProcessWH);
		POINT xy;
		ClientToScreen(hWnd, &xy);
		ProcessWH.left = xy.x;
		ProcessWH.top = xy.y;

		Width = ProcessWH.right;
		Height = ProcessWH.bottom;
	}
	else
		exit(2);

	Window = CreateWindowEx(NULL, L"fart.club", L"fart.club1", WS_POPUP | WS_VISIBLE, 0, 0, Width, Height, 0, 0, 0, 0);
	DwmExtendFrameIntoClientArea(Window, &Margin);
	SetWindowLong(Window, GWL_EXSTYLE, WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_LAYERED);
	ShowWindow(Window, SW_SHOW);
	UpdateWindow(Window);
}*/

void WindowMain()
{

	CreateThread(0, 0, (LPTHREAD_START_ROUTINE)Window2Target, 0, 0, 0);
	WNDCLASSEX wClass =
	{
		sizeof(WNDCLASSEX),
		0,
		WindowProc,
		0,
		0,
		nullptr,
		LoadIcon(nullptr, IDI_APPLICATION),
		LoadCursor(nullptr, IDC_ARROW),
		nullptr,
		nullptr,
		TEXT("Test1"),
		LoadIcon(nullptr, IDI_APPLICATION)
	};

	if (!RegisterClassEx(&wClass))
		exit(1);

	hWnd = FindWindowW(NULL, TEXT("Fortnite  "));

	//printf("GameWnd Found! : %p\n", GameWnd);

	if (hWnd)
	{
		GetClientRect(hWnd, &ProcessWH);
		POINT xy;
		ClientToScreen(hWnd, &xy);
		ProcessWH.left = xy.x;
		ProcessWH.top = xy.y;


		Width = ProcessWH.right;
		Height = ProcessWH.bottom;
	}
	else exit(2);

	Window = CreateWindowExA(NULL, "Test1", "Test1", WS_POPUP | WS_VISIBLE, ProcessWH.left, ProcessWH.top, Width, Height, NULL, NULL, 0, NULL);
	DwmExtendFrameIntoClientArea(Window, &Margin);
	SetWindowLong(Window, GWL_EXSTYLE, WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW);
	ShowWindow(Window, SW_SHOW);
	UpdateWindow(Window);

}

void InitializeD3D()
{
	if (FAILED(Direct3DCreate9Ex(D3D_SDK_VERSION, &pObject)))
		exit(3);

	ZeroMemory(&d3d, sizeof(d3d));
	d3d.BackBufferWidth = Width;
	d3d.BackBufferHeight = Height;
	d3d.BackBufferFormat = D3DFMT_A8R8G8B8;
	d3d.MultiSampleQuality = D3DMULTISAMPLE_NONE;
	d3d.AutoDepthStencilFormat = D3DFMT_D16;
	d3d.SwapEffect = D3DSWAPEFFECT_DISCARD;
	d3d.EnableAutoDepthStencil = TRUE;
	d3d.hDeviceWindow = Window;
	d3d.Windowed = TRUE;

	pObject->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, Window, D3DCREATE_SOFTWARE_VERTEXPROCESSING, &d3d, &D3DDevice);

	IMGUI_CHECKVERSION();

	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	(void)io;

	ImGui_ImplWin32_Init(Window);
	ImGui_ImplDX9_Init(D3DDevice);

	ImGui::StyleColorsClassic();
	ImGuiStyle* style = &ImGui::GetStyle();

	ImVec4* colors = ImGui::GetStyle().Colors;
	colors[ImGuiCol_Text] = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
	colors[ImGuiCol_TextDisabled] = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
	colors[ImGuiCol_WindowBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.83f);
	colors[ImGuiCol_ChildBg] = ImVec4(1.00f, 1.00f, 1.00f, 0.00f);
	colors[ImGuiCol_PopupBg] = ImVec4(0.08f, 0.08f, 0.08f, 0.94f);
	colors[ImGuiCol_Border] = ImVec4(0.43f, 0.43f, 0.50f, 0.50f);
	colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
	colors[ImGuiCol_FrameBg] = ImVec4(0.16f, 0.29f, 0.48f, 0.54f);
	colors[ImGuiCol_FrameBgHovered] = ImVec4(0.26f, 0.59f, 0.98f, 0.40f);
	colors[ImGuiCol_FrameBgActive] = ImVec4(0.26f, 0.59f, 0.98f, 0.67f);
	colors[ImGuiCol_TitleBg] = ImVec4(0.04f, 0.04f, 0.04f, 1.00f);
	colors[ImGuiCol_TitleBgActive] = ImVec4(0.16f, 0.29f, 0.48f, 1.00f);
	colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.00f, 0.00f, 0.00f, 1.00f);
	colors[ImGuiCol_MenuBarBg] = ImVec4(1.00f, 0.00f, 0.00f, 0.61f);
	colors[ImGuiCol_ScrollbarBg] = ImVec4(0.02f, 0.02f, 0.02f, 0.53f);
	colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.16f, 0.29f, 0.48f, 0.54f);
	colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.41f, 0.41f, 0.41f, 1.00f);
	colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.51f, 0.51f, 0.51f, 1.00f);
	colors[ImGuiCol_CheckMark] = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
	colors[ImGuiCol_SliderGrab] = ImVec4(0.24f, 0.52f, 0.88f, 1.00f);
	colors[ImGuiCol_SliderGrabActive] = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
	colors[ImGuiCol_Button] = ImVec4(0.46f, 0.4f, 0.98f, 0.40f);
	colors[ImGuiCol_ButtonHovered] = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
	colors[ImGuiCol_ButtonActive] = ImVec4(0.06f, 0.53f, 0.98f, 1.00f);
	colors[ImGuiCol_Header] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
	colors[ImGuiCol_HeaderHovered] = ImVec4(0.26f, 0.59f, 0.98f, 0.80f);
	colors[ImGuiCol_HeaderActive] = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
	colors[ImGuiCol_Separator] = ImVec4(0.43f, 0.43f, 0.50f, 0.50f);
	colors[ImGuiCol_SeparatorHovered] = ImVec4(0.10f, 0.40f, 0.75f, 0.78f);
	colors[ImGuiCol_SeparatorActive] = ImVec4(0.10f, 0.40f, 0.75f, 1.00f);
	colors[ImGuiCol_ResizeGrip] = ImVec4(0.26f, 0.59f, 0.98f, 0.25f);
	colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.26f, 0.59f, 0.98f, 0.67f);
	colors[ImGuiCol_ResizeGripActive] = ImVec4(0.26f, 0.59f, 0.98f, 0.95f);
	colors[ImGuiCol_PlotLines] = ImVec4(0.61f, 0.61f, 0.61f, 1.00f);
	colors[ImGuiCol_PlotLinesHovered] = ImVec4(1.00f, 0.43f, 0.35f, 1.00f);
	colors[ImGuiCol_PlotHistogram] = ImVec4(0.90f, 0.70f, 0.00f, 1.00f);
	colors[ImGuiCol_PlotHistogramHovered] = ImVec4(1.00f, 0.60f, 0.00f, 1.00f);
	colors[ImGuiCol_TextSelectedBg] = ImVec4(0.26f, 0.59f, 0.98f, 0.35f);
	colors[ImGuiCol_ModalWindowDarkening] = ImVec4(0.80f, 0.80f, 0.80f, 0.35f);
	colors[ImGuiCol_DragDropTarget] = ImVec4(1.00f, 1.00f, 0.00f, 0.90f);

	pObject->Release();
}

void RadarRange(float* x, float* y, float range)
{
	if (fabs((*x)) > range || fabs((*y)) > range)
	{
		if ((*y) > (*x))
		{
			if ((*y) > -(*x))
			{
				(*x) = range * (*x) / (*y);
				(*y) = range;
			}
			else
			{
				(*y) = -range * (*y) / (*x);
				(*x) = -range;
			}
		}
		else
		{
			if ((*y) > -(*x))
			{
				(*y) = range * (*y) / (*x);
				(*x) = range;
			}
			else
			{
				(*x) = -range * (*x) / (*y);
				(*y) = -range;
			}
		}
	}
}

/*

void CalcRadarPoint(FVector vOrigin, int& screenx, int& screeny)
{
	FRotator vAngle = FRotator{ CamRot.x, CamRot.y, CamRot.z };
	auto fYaw = vAngle.Yaw * PI / 180.0f;
	float dx = vOrigin.X - CamLoc.x;
	float dy = vOrigin.Y - CamLoc.y;

	float fsin_yaw = sinf(fYaw);
	float fminus_cos_yaw = -cosf(fYaw);

	float x = dy * fminus_cos_yaw + dx * fsin_yaw;
	x = -x;
	float y = dx * fminus_cos_yaw - dy * fsin_yaw;

	float range = (float)Settings.RadarESPRange;

	RadarRange(&x, &y, range);

	ImVec2 DrawPos = ImGui::GetCursorScreenPos();
	ImVec2 DrawSize = ImGui::GetContentRegionAvail();

	int rad_x = (int)DrawPos.x;
	int rad_y = (int)DrawPos.y;

	float r_siz_x = DrawSize.x;
	float r_siz_y = DrawSize.y;

	int x_max = (int)r_siz_x + rad_x - 5;
	int y_max = (int)r_siz_y + rad_y - 5;

	screenx = rad_x + ((int)r_siz_x / 2 + int(x / range * r_siz_x));
	screeny = rad_y + ((int)r_siz_y / 2 + int(y / range * r_siz_y));

	if (screenx > x_max)
		screenx = x_max;

	if (screenx < rad_x)
		screenx = rad_x;

	if (screeny > y_max)
		screeny = y_max;

	if (screeny < rad_y)
		screeny = rad_y;
}
*/
void renderRadar() {
	//or (auto pawn : radarPawns) {
	//	auto player = pawn;

	int screenx = 0;
	int screeny = 0;

	float Color_R = 255 / 255.f;
	float Color_G = 128 / 255.f;
	float Color_B = 0 / 255.f;

	//	FVector pos = *GetPawnRootLocation((PVOID)pawn);
	//CalcRadarPoint(pos, screenx, screeny);

	ImDrawList* Draw = ImGui::GetOverlayDrawList();

	//FVector viewPoint = { 0 };
	//if (IsTargetVisible(pawn)) {
	//	Color_R = 128 / 255.f;
	//	Color_G = 224 / 255.f;
	//	Color_B = 0 / 255.f;

	Draw->AddRectFilled(ImVec2((float)screenx, (float)screeny),
		ImVec2((float)screenx + 5, (float)screeny + 5),
		ImColor(Color_R, Color_G, Color_B));
}

bool firstS = false;
void RadarLoop()
{

}

Vector3 Camera(unsigned __int64 RootComponent)
{
	unsigned __int64 PtrPitch;
	Vector3 Camera;

	auto pitch = read<uintptr_t>(DrverInit, FNProcID, Offsets::LocalPlayer + 0xb0);
	Camera.x = read<float>(DrverInit, FNProcID, RootComponent + 0x12C);
	Camera.y = read<float>(DrverInit, FNProcID, pitch + 0x678);

	float test = asin(Camera.y);
	float degrees = test * (180.0 / M_PI);

	Camera.y = degrees;

	if (Camera.x < 0)
		Camera.x = 360 + Camera.x;

	return Camera;
}

typedef struct
{
	DWORD R;
	DWORD G;
	DWORD B;
	DWORD A;
}RGBA;

class Color
{
public:
	RGBA red = { 255,0,0,255 };
	RGBA Magenta = { 255,0,255,255 };
	RGBA yellow = { 255,255,0,255 };
	RGBA grayblue = { 128,128,255,255 };
	RGBA green = { 128,224,0,255 };
	RGBA darkgreen = { 0,224,128,255 };
	RGBA brown = { 192,96,0,255 };
	RGBA pink = { 255,168,255,255 };
	RGBA DarkYellow = { 216,216,0,255 };
	RGBA SilverWhite = { 236,236,236,255 };
	RGBA purple = { 144,0,255,255 };
	RGBA Navy = { 88,48,224,255 };
	RGBA skyblue = { 0,136,255,255 };
	RGBA graygreen = { 128,160,128,255 };
	RGBA blue = { 0,96,192,255 };
	RGBA orange = { 255,128,0,255 };
	RGBA peachred = { 255,80,128,255 };
	RGBA reds = { 255,128,192,255 };
	RGBA darkgray = { 96,96,96,255 };
	RGBA Navys = { 0,0,128,255 };
	RGBA darkgreens = { 0,128,0,255 };
	RGBA darkblue = { 0,128,128,255 };
	RGBA redbrown = { 128,0,0,255 };
	RGBA purplered = { 128,0,128,255 };
	RGBA greens = { 0,255,0,255 };
	RGBA envy = { 0,255,255,255 };
	RGBA black = { 0,0,0,255 };
	RGBA gray = { 128,128,128,255 };
	RGBA white = { 255,255,255,255 };
	RGBA blues = { 30,144,255,255 };
	RGBA lightblue = { 135,206,250,160 };
	RGBA Scarlet = { 220, 20, 60, 160 };
	RGBA white_ = { 255,255,255,200 };
	RGBA gray_ = { 128,128,128,200 };
	RGBA black_ = { 0,0,0,200 };
	RGBA red_ = { 255,0,0,200 };
	RGBA Magenta_ = { 255,0,255,200 };
	RGBA yellow_ = { 255,255,0,200 };
	RGBA grayblue_ = { 128,128,255,200 };
	RGBA green_ = { 128,224,0,200 };
	RGBA darkgreen_ = { 0,224,128,200 };
	RGBA brown_ = { 192,96,0,200 };
	RGBA pink_ = { 255,168,255,200 };
	RGBA darkyellow_ = { 216,216,0,200 };
	RGBA silverwhite_ = { 236,236,236,200 };
	RGBA purple_ = { 144,0,255,200 };
	RGBA Blue_ = { 88,48,224,200 };
	RGBA skyblue_ = { 0,136,255,200 };
	RGBA graygreen_ = { 128,160,128,200 };
	RGBA blue_ = { 0,96,192,200 };
	RGBA orange_ = { 255,128,0,200 };
	RGBA pinks_ = { 255,80,128,200 };
	RGBA Fuhong_ = { 255,128,192,200 };
	RGBA darkgray_ = { 96,96,96,200 };
	RGBA Navy_ = { 0,0,128,200 };
	RGBA darkgreens_ = { 0,128,0,200 };
	RGBA darkblue_ = { 0,128,128,200 };
	RGBA redbrown_ = { 128,0,0,200 };
	RGBA purplered_ = { 128,0,128,200 };
	RGBA greens_ = { 0,255,0,200 };
	RGBA envy_ = { 0,255,255,200 };

	RGBA glassblack = { 0, 0, 0, 160 };
	RGBA GlassBlue = { 65,105,225,80 };
	RGBA glassyellow = { 255,255,0,160 };
	RGBA glass = { 200,200,200,60 };


	RGBA Plum = { 221,160,221,160 };

};
Color Col;

void DrawFilledRect(int x, int y, int w, int h, RGBA* color)
{
	ImGui::GetOverlayDrawList()->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h), ImGui::ColorConvertFloat4ToU32(ImVec4(color->R / 255.0, color->G / 255.0, color->B / 255.0, color->A / 255.0)), 0, 0);
}

void DrawCornerBox(int x, int y, int w, int h, int borderPx, RGBA* color)
{
	DrawFilledRect(x + borderPx, y, w / 3, borderPx, color); //top 
	DrawFilledRect(x + w - w / 3 + borderPx, y, w / 3, borderPx, color); //top 
	DrawFilledRect(x, y, borderPx, h / 3, color); //left 
	DrawFilledRect(x, y + h - h / 3 + borderPx * 2, borderPx, h / 3, color); //left 
	DrawFilledRect(x + borderPx, y + h + borderPx, w / 3, borderPx, color); //bottom 
	DrawFilledRect(x + w - w / 3 + borderPx, y + h + borderPx, w / 3, borderPx, color); //bottom 
	DrawFilledRect(x + w + borderPx, y, borderPx, h / 3, color);//right 
	DrawFilledRect(x + w + borderPx, y + h - h / 3 + borderPx * 2, borderPx, h / 3, color);//right 
}

void drawLoop() {


	Uworld = read<DWORD_PTR>(DrverInit, FNProcID, base_address + OFFSET_UWORLD);
	//printf(_xor_("Uworld: %p.\n").c_str(), Uworld);

	DWORD_PTR Gameinstance = read<DWORD_PTR>(DrverInit, FNProcID, Uworld + 0x180); // changes sometimes 4u

	if (Gameinstance == (DWORD_PTR)nullptr)
		return;

	//printf(_xor_("Gameinstance: %p.\n").c_str(), Gameinstance);

	DWORD_PTR LocalPlayers = read<DWORD_PTR>(DrverInit, FNProcID, Gameinstance + 0x38);

	if (LocalPlayers == (DWORD_PTR)nullptr)
		return;

	//printf(_xor_("LocalPlayers: %p.\n").c_str(), LocalPlayers);

	Localplayer = read<DWORD_PTR>(DrverInit, FNProcID, LocalPlayers);

	if (Localplayer == (DWORD_PTR)nullptr)
		return;

	//printf(_xor_("LocalPlayer: %p.\n").c_str(), Localplayer);

	PlayerController = read<DWORD_PTR>(DrverInit, FNProcID, Localplayer + 0x30);

	if (PlayerController == (DWORD_PTR)nullptr)
		return;

	//printf(_xor_("playercontroller: %p.\n").c_str(), PlayerController);

	LocalPawn = read<uint64_t>(DrverInit, FNProcID, PlayerController + 0x2A0);  // changed often 4u sometimes called AcknowledgedPawn

	if (LocalPawn == (DWORD_PTR)nullptr)
		return;

	//printf(_xor_("Pawn: %p.\n").c_str(), LocalPawn);

	Rootcomp = read<uint64_t>(DrverInit, FNProcID, LocalPawn + 0x130);

	if (Rootcomp == (DWORD_PTR)nullptr)
		return;

	//printf(_xor_("Rootcomp: %p.\n").c_str(), Rootcomp);

	if (LocalPawn != 0) {
		localplayerID = read<int>(DrverInit, FNProcID, LocalPawn + 0x18);
	}

	Ulevel = read<DWORD_PTR>(DrverInit, FNProcID, Uworld + 0x30);
	//printf(_xor_("Ulevel: %p.\n").c_str(), Ulevel);

	if (Ulevel == (DWORD_PTR)nullptr)
		return;

	DWORD64 PlayerState = read<DWORD64>(DrverInit, FNProcID, LocalPawn + 0x240);  //changes often 4u

	if (PlayerState == (DWORD_PTR)nullptr)
		return;

	DWORD ActorCount = read<DWORD>(DrverInit, FNProcID, Ulevel + 0xA0);

	DWORD_PTR AActors = read<DWORD_PTR>(DrverInit, FNProcID, Ulevel + 0x98);
	//printf(_xor_("AActors: %p.\n").c_str(), AActors);

	if (AActors == (DWORD_PTR)nullptr)
		return;

	for (int i = 0; i < ActorCount; i++)
	{
		uint64_t CurrentActor = read<uint64_t>(DrverInit, FNProcID, AActors + i * 0x8);

		int curactorid = read<int>(DrverInit, FNProcID, CurrentActor + 0x18);

		if (curactorid == localplayerID || curactorid == 17384284 || curactorid == 9875145 || curactorid == 9873134 || curactorid == 9876800 || curactorid == 9874439) // this number changes for bot and NPC often, modified from original 4u
		// you will need to print out the actorID on screen and find the new numbers, currently different numbers for bots, NPC(2), and bot in solo and 2 player games are different.
		//if (curactorid == localplayerID) //original changed 4u to target bots and NPC
		{
			if (CurrentActor == (uint64_t)nullptr || CurrentActor == -1 || CurrentActor == NULL)
				continue;

			uint64_t CurrentActorRootComponent = read<uint64_t>(DrverInit, FNProcID, CurrentActor + 0x130);

			if (CurrentActorRootComponent == (uint64_t)nullptr || CurrentActorRootComponent == -1 || CurrentActorRootComponent == NULL)
				continue;

			uint64_t currentactormesh = read<uint64_t>(DrverInit, FNProcID, CurrentActor + 0x280); // change as needed 4u

			if (currentactormesh == (uint64_t)nullptr || currentactormesh == -1 || currentactormesh == NULL)
				continue;

			int MyTeamId = read<int>(DrverInit, FNProcID, PlayerState + 0xED0);  //changes often 4u

			DWORD64 otherPlayerState = read<uint64_t>(DrverInit, FNProcID, CurrentActor + 0x240); //changes often 4u

			if (otherPlayerState == (uint64_t)nullptr || otherPlayerState == -1 || otherPlayerState == NULL)
				continue;

			int ActorTeamId = read<int>(DrverInit, FNProcID, otherPlayerState + 0xED0); //changes often 4u

			Vector3 Headpos = GetBoneWithRotation(currentactormesh, 66);
			Localcam = CameraEXT;
			localactorpos = read<Vector3>(DrverInit, FNProcID, Rootcomp + 0x11C);

			float distance = localactorpos.Distance(Headpos) / 100.f;

			if (distance < 0.5)
				continue;
			Vector3 CirclePOS = GetBoneWithRotation(currentactormesh, 2);
			

			Vector3 rootOut = GetBoneWithRotation(currentactormesh, 0);

			Vector3 Out = ProjectWorldToScreen(rootOut, Vector3(Localcam.y, Localcam.x, Localcam.z));

			Vector3 HeadposW2s = ProjectWorldToScreen(Headpos, Vector3(Localcam.y, Localcam.x, Localcam.z));
			Vector3 bone0 = GetBoneWithRotation(currentactormesh, 0);
			Vector3 bottom = ProjectWorldToScreen(bone0, Vector3(Localcam.y, Localcam.x, Localcam.z));
			Vector3 Headbox = ProjectWorldToScreen(Vector3(Headpos.x, Headpos.y, Headpos.z + 15), Vector3(Localcam.y, Localcam.x, Localcam.z));	

			float boxsize = (float)(Out.y - HeadposW2s.y);
			float boxwidth = boxsize / 3.0f;

			float dwpleftx = (float)Out.x - boxwidth / 2.0f;
			float dwplefty = (float)Out.y;
			ImGui::GetOverlayDrawList()->AddCircle(ImVec2(ScreenCenterX, ScreenCenterY), Settings::AimbotFOV, ImColor(255,255,255, 230), Settings::Roughness);

			if (Settings::PlayerESP)
			{
				ImGui::GetOverlayDrawList()->AddRectFilled(ImVec2(dwpleftx, dwplefty), ImVec2(HeadposW2s.x + boxwidth, HeadposW2s.y + 5.0f), IM_COL32(255, 255, 255, 50));
				ImGui::GetOverlayDrawList()->AddRect(ImVec2(dwpleftx, dwplefty), ImVec2(HeadposW2s.x + boxwidth, HeadposW2s.y + 5.0f), IM_COL32(128, 224, 0, 200));
			}
		}
	}
	Sleep(2);
}
int r, g, b;
int r1, g2, b2;

float color_red = 1.;
float color_green = 0;
float color_blue = 0;
float color_random = 0.0;
float color_speed = -10.0;
bool rainbowmode = false;

void ColorChange()
{
	if (rainbowmode)
	{
		static float Color[3];
		static DWORD Tickcount = 0;
		static DWORD Tickcheck = 0;
		ImGui::ColorConvertRGBtoHSV(color_red, color_green, color_blue, Color[0], Color[1], Color[2]);
		if (GetTickCount() - Tickcount >= 1)
		{
			if (Tickcheck != Tickcount)
			{
				Color[0] += 0.001f * color_speed;
				Tickcheck = Tickcount;
			}
			Tickcount = GetTickCount();
		}
		if (Color[0] < 0.0f) Color[0] += 1.0f;
		ImGui::ColorConvertHSVtoRGB(Color[0], Color[1], Color[2], color_red, color_green, color_blue);
	}
}

void Render() {

	ImGui_ImplDX9_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();

	int X;
	int Y;
	float size1 = 3.0f;
	float size2 = 2.0f;

	if (Settings::AimbotCircle)
	{
		ImGui::GetOverlayDrawList()->AddCircle(ImVec2(ScreenCenterX, ScreenCenterY), Settings::AimbotFOV, ImColor(r, g, b, 230), Settings::Roughness);
		ImGui::GetOverlayDrawList()->AddCircle(ImVec2(ScreenCenterX, ScreenCenterY), Settings::AimbotFOV, ImColor(255, 255, 255, 230), Settings::Roughness);
	}
	if (rainbowmode)
	{
		ImGui::GetOverlayDrawList()->AddCircle(ImVec2(ScreenCenterX, ScreenCenterY), Settings::AimbotFOV, ImGui::GetColorU32({ color_red, color_green, color_blue, 230 }), Settings::Roughness);
	}

	if (Settings::Reticle)
	{
		ImGui::GetOverlayDrawList()->AddCircle(ImVec2(ScreenCenterX, ScreenCenterY), size1, ImColor(0, 0, 0, 255), 120);
		ImGui::GetOverlayDrawList()->AddCircleFilled(ImVec2(ScreenCenterX, ScreenCenterY), size2, ImColor(0, 255, 251, 200), 120);
	}
	if (Settings::Crosshair)
	{
		ImGui::GetOverlayDrawList()->AddLine(ImVec2(Width / 2 - 12, Height / 2), ImVec2(Width / 2 - 2, Height / 2), ImGui::GetColorU32({ 255, 255, 255, 255.f }), 2.0f);
		ImGui::GetOverlayDrawList()->AddLine(ImVec2(Width / 2 + 13, Height / 2), ImVec2(Width / 2 + 3, Height / 2), ImGui::GetColorU32({ 255, 255, 255, 255.f }), 2.0f);
		ImGui::GetOverlayDrawList()->AddLine(ImVec2(Width / 2, Height / 2 - 12), ImVec2(Width / 2, Height / 2 - 2), ImGui::GetColorU32({ 255, 255, 255, 255.f }), 2.0f);
		ImGui::GetOverlayDrawList()->AddLine(ImVec2(Width / 2, Height / 2 + 13), ImVec2(Width / 2, Height / 2 + 3), ImGui::GetColorU32({ 255, 255, 255, 255.f }), 2.0f);
	}
	if (Settings::Radar)
	{
		RadarLoop();
	}

	ColorChange();

	bool circleedit = false;
	ImGui::SetNextWindowSize({ 213, 200 });
	if (Settings::ShowMenu)
	{
		ImGui::Begin("fart.club", 0, ImGuiWindowFlags_::ImGuiWindowFlags_NoResize);

		static int fortnitetab;
		ImGuiStyle* Style = &ImGui::GetStyle();
		if (ImGui::Button("aimbot", ImVec2(60, 20))) fortnitetab = 1;
		ImGui::SameLine();
		if (ImGui::Button("visuals", ImVec2(60, 20))) fortnitetab = 2;
		ImGui::SameLine();
		if (ImGui::Button("colors", ImVec2(60, 20))) fortnitetab = 3;

		if (fortnitetab == 1)
		{
			ImGui::Checkbox("Mouse Aimbot", &Settings::MouseAimbot);
			ImGui::Checkbox("Aimbot FOV", &Settings::AimbotCircle);
			ImGui::Checkbox("Edit Circle", &circleedit);
			if (Settings::AimbotCircle)
			{
				ImGui::SliderFloat("Size", &Settings::AimbotFOV, 30, 900);
			}
			ImGui::Checkbox("Reticle", &Settings::Reticle);
			ImGui::Checkbox("Crosshair", &Settings::Crosshair);
		}
		if (fortnitetab == 2)
		{
			ImGui::Checkbox("Player Box", &Settings::PlayerESP);
			ImGui::Checkbox("Corner ESP", &Settings::CornerESP);
			ImGui::Checkbox("Distance ESP", &Settings::Distance);
			ImGui::Checkbox("Skeleton ESP", &Settings::Skeleton);
			ImGui::Checkbox("Radar ESP", &Settings::Radar);
			if (Settings::Radar)
			{
				ImGui::SliderFloat("Radar Distance", &Settings::RadarDistance, 1000, 50000);
			}
		}

		ImGui::GetOverlayDrawList()->AddRectFilled(ImGui::GetIO().MousePos, ImVec2(ImGui::GetIO().MousePos.x + 7.f, ImGui::GetIO().MousePos.y + 7.f), ImColor( r1, g2, b2, 255 ));

		if (fortnitetab == 3)
		{
			ImGui::Text("Block Cursor");
			ImGui::SliderInt("Reds ", &r1, 1, 255);
			ImGui::SliderInt("Greens ", &g2, 1, 255);
			ImGui::SliderInt("Blues ", &b2, 1, 255);
			ImGui::Text("Circle");
			ImGui::SliderFloat("Roughness", &Settings::Roughness, 1, 128);
			ImGui::SliderInt("Red", &r, 1, 255);
			ImGui::SliderInt("Green", &g, 1, 255);
			ImGui::SliderInt("Blue", &b, 1, 255);
			ImGui::Checkbox("Rainbow Mode", &rainbowmode);
			if (rainbowmode)
			{
				ImGui::SliderFloat("R", &color_red, 1, 255);
				ImGui::SliderFloat("G", &color_green, 1, 255);
				ImGui::SliderFloat("B", &color_blue, 1, 255);
			}
		}
		ImGui::End();
	}
	
	drawLoop();

	ImGui::EndFrame();
	D3DDevice->SetRenderState(D3DRS_ZENABLE, false);
	D3DDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, false);
	D3DDevice->SetRenderState(D3DRS_SCISSORTESTENABLE, false);
	D3DDevice->Clear(0, NULL, D3DCLEAR_TARGET, D3DCOLOR_ARGB(0, 0, 0, 0), 1.0f, 0);

	if (D3DDevice->BeginScene() >= 0)
	{
		ImGui::Render();
		ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
		D3DDevice->EndScene();
	}
	HRESULT Results = D3DDevice->Present(NULL, NULL, NULL, NULL);

	if (Results == D3DERR_DEVICELOST && D3DDevice->TestCooperativeLevel() == D3DERR_DEVICENOTRESET)
	{
		ImGui_ImplDX9_InvalidateDeviceObjects();
		D3DDevice->Reset(&d3d);
		ImGui_ImplDX9_CreateDeviceObjects();
	}
}


MSG Message_Loop = { NULL };

void Loop()
{
	static RECT old_rc;
	ZeroMemory(&Message_Loop, sizeof(MSG));

	while (Message_Loop.message != WM_QUIT)
	{
		if (PeekMessage(&Message_Loop, Window, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&Message_Loop);
			DispatchMessage(&Message_Loop);
		}

		HWND hwnd_active = GetForegroundWindow();

		if (hwnd_active == hWnd) {
			HWND hwndtest = GetWindow(hwnd_active, GW_HWNDPREV);
			SetWindowPos(Window, hwndtest, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
		}

		if (GetAsyncKeyState(0x23) & 1)
			exit(8);

		RECT rc;
		POINT xy;

		ZeroMemory(&rc, sizeof(RECT));
		ZeroMemory(&xy, sizeof(POINT));
		GetClientRect(hWnd, &rc);
		ClientToScreen(hWnd, &xy);
		rc.left = xy.x;
		rc.top = xy.y;

		ImGuiIO& io = ImGui::GetIO();
		io.ImeWindowHandle = hWnd;
		io.DeltaTime = 1.0f / 60.0f;

		POINT p;
		GetCursorPos(&p);
		io.MousePos.x = p.x - xy.x;
		io.MousePos.y = p.y - xy.y;

		if (GetAsyncKeyState(VK_LBUTTON)) {
			io.MouseDown[0] = true;
			io.MouseClicked[0] = true;
			io.MouseClickedPos[0].x = io.MousePos.x;
			io.MouseClickedPos[0].x = io.MousePos.y;
		}
		else
			io.MouseDown[0] = false;

		if (rc.left != old_rc.left || rc.right != old_rc.right || rc.top != old_rc.top || rc.bottom != old_rc.bottom)
		{
			old_rc = rc;

			Width = rc.right;
			Height = rc.bottom;

			d3d.BackBufferWidth = Width;
			d3d.BackBufferHeight = Height;
			SetWindowPos(Window, (HWND)0, xy.x, xy.y, Width, Height, SWP_NOREDRAW);
			D3DDevice->Reset(&d3d);
		}
		Render();
	}
	ImGui_ImplDX9_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();

	DestroyWindow(Window);
}

LRESULT CALLBACK WindowProc(HWND hWnd, UINT Message, WPARAM wParam, LPARAM lParam)
{
	if (ImGui_ImplWin32_WndProcHandler(hWnd, Message, wParam, lParam))
		return true;

	switch (Message)
	{
	case WM_DESTROY:
		ShutDown();
		PostQuitMessage(0);
		exit(4);
		break;
	case WM_SIZE:
		if (D3DDevice != NULL && wParam != SIZE_MINIMIZED)
		{
			ImGui_ImplDX9_InvalidateDeviceObjects();
			d3d.BackBufferWidth = LOWORD(lParam);
			d3d.BackBufferHeight = HIWORD(lParam);
			HRESULT hr = D3DDevice->Reset(&d3d);
			if (hr == D3DERR_INVALIDCALL)
				IM_ASSERT(0);
			ImGui_ImplDX9_CreateDeviceObjects();
		}
		break;
	default:
		return DefWindowProc(hWnd, Message, wParam, lParam);
		break;
	}
	return 0;
}


void ShutDown()
{
	VertBuff->Release();
	D3DDevice->Release();
	pObject->Release();

	DestroyWindow(Window);
	UnregisterClass(L"fgers", NULL);
}
