#include "StdAfx.h"
#include <Imgui/imconfig.h>
#include "ImguiImpl.h"
#include "CrySystem\IConsole.h"
#include <CrySystem\ISystem.h>

#include <Imgui/imgui.h>
#include "CryInput\IHardwareMouse.h"
#include "Render/ImguiRenderer.h"
//#include "CryAction/IActionMapManager.h"
#include "CryGame/IGameFramework.h"
#include "../CryAction/IActionMapManager.h"
#include "CrySystem/ConsoleRegistration.h"
#include "Widgets/PerfMonitor.h"

static CImguiImpl* g_pThis = nullptr;
static bool s_bCaptured = false;

static void ImguiCaptureMouse(IConsoleCmdArgs* pArgs)
{
	if (!g_pThis)
		return;

	if (!s_bCaptured)
	{
		gEnv->pHardwareMouse->IncrementCounter();
		gEnv->pGameFramework->GetIActionMapManager()->Enable(false);
		s_bCaptured = true;
	}
	else
	{
		gEnv->pHardwareMouse->DecrementCounter();
		gEnv->pGameFramework->GetIActionMapManager()->Enable(true);
		s_bCaptured = false;
	}
	
}


CImguiImpl::CImguiImpl()
{
	g_pThis = this;

	CryLogAlways("[CryImGui] Initializing implementation...");

	ConsoleRegistrationHelper::AddCommand("imgui_captureInput", ImguiCaptureMouse, 0, "Capture input for imgui");
	ConsoleRegistrationHelper::Register("imgui_showDemoWindow", &m_bShowDemoWindow, 0,0, "Show imgui demo window");
	ConsoleRegistrationHelper::Register("imgui_showPerfWidget", &m_showPerfWidget, 0, 0, "Show a small performance widget");
	
	gEnv->pSystem->GetISystemEventDispatcher()->RegisterListener(this, "CImguiImpl");
}

CImguiImpl::~CImguiImpl()
{
	gEnv->pRenderer->RemoveTexture(m_pFontTexture->GetTextureID());

	g_pThis = nullptr;

	gEnv->pConsole->RemoveCommand("imgui_captureMouse");
	gEnv->pHardwareMouse->RemoveListener(this);
	gEnv->pInput->RemoveEventListener(this);
	gEnv->pSystem->GetISystemEventDispatcher()->RemoveListener(this);
}


static void* Allocate(size_t size, void* data)
{
	return CryModuleCRTMalloc(size);
}

static void Free(void* ptr, void* data)
{
	return CryModuleCRTFree(ptr);
}

void CImguiImpl::InitImgui()
{
	ImGui::CreateContext();
	ImGui::SetAllocatorFunctions(Allocate, Free);
	
	ImGuiIO& io = ImGui::GetIO();
	//io.DisplaySize = ImVec2(300, 200);
	Vec2i dimensions(gEnv->pRenderer->GetWidth(),gEnv->pRenderer->GetHeight());
	io.DisplaySize = { (float)dimensions.x, (float)dimensions.y };
	io.RenderDrawListsFn = nullptr;

	m_pRenderer = std::make_unique<CImguiRenderer>(dimensions);

	auto keyMap = &ImGui::GetIO().KeyMap[0];

	keyMap[ImGuiKey_Tab] = EKeyId::eKI_Tab;
	keyMap[ImGuiKey_LeftArrow] = EKeyId::eKI_Left;
	keyMap[ImGuiKey_RightArrow] = EKeyId::eKI_Right;
	keyMap[ImGuiKey_UpArrow] = EKeyId::eKI_Up;
	keyMap[ImGuiKey_DownArrow] = EKeyId::eKI_Down;
	keyMap[ImGuiKey_PageUp] = EKeyId::eKI_PgUp;
	keyMap[ImGuiKey_PageDown] = EKeyId::eKI_PgDn;
	keyMap[ImGuiKey_Home] = EKeyId::eKI_Home;
	keyMap[ImGuiKey_End] = EKeyId::eKI_End;
	keyMap[ImGuiKey_Insert] = EKeyId::eKI_Insert;
	keyMap[ImGuiKey_Delete] = EKeyId::eKI_Delete;
	keyMap[ImGuiKey_Backspace] = EKeyId::eKI_Backspace;
	keyMap[ImGuiKey_Space] = EKeyId::eKI_Space;
	keyMap[ImGuiKey_Enter] = EKeyId::eKI_Enter;
	keyMap[ImGuiKey_Escape] = EKeyId::eKI_Escape;
	keyMap[ImGuiKey_A] = EKeyId::eKI_A;  // for text edit CTRL+A: select all
	keyMap[ImGuiKey_C] = EKeyId::eKI_C;  // for text edit CTRL+C: copy
	keyMap[ImGuiKey_V] = EKeyId::eKI_V;  // for text edit CTRL+V: paste
	keyMap[ImGuiKey_X] = EKeyId::eKI_X;  // for text edit CTRL+X: cut
	keyMap[ImGuiKey_Y] = EKeyId::eKI_Y;  // for text edit CTRL+Y: redo
	keyMap[ImGuiKey_Z] = EKeyId::eKI_Z;  // for text edit CTRL+Z: undo

	m_pPerfMon = std::make_unique<Cry::Imgui::CPerformanceMonitor>();
}

void CImguiImpl::Update()
{
	CRY_PROFILE_FUNCTION(PROFILE_GAME);

	static bool bFirstFrame = true;

	if (bFirstFrame)
	{
		bFirstFrame = false;
	}
	else
		m_pRenderer->RenderImgui();

	ImGuiIO& io = ImGui::GetIO();
	io.DeltaTime = gEnv->pTimer->GetFrameTime() ? gEnv->pTimer->GetFrameTime() : 1;
	io.DisplaySize = ImVec2((float)gEnv->pRenderer->GetWidth(), (float)gEnv->pRenderer->GetHeight());
	if (s_bCaptured)
		gEnv->pHardwareMouse->GetHardwareMouseClientPosition(&io.MousePos.x, &io.MousePos.y);


	for (auto &entry : m_cachedInputEvents)
		OnCachedInputEvent(entry);

	m_cachedInputEvents.clear();
	for (auto &entry : m_cachedMouseEvents)
		OnCachedMouseEvent(entry.iX, entry.iY, entry.eHardwareMouseEvent, entry.wheelDelta);

	m_cachedMouseEvents.clear();
	
	ImGui::NewFrame();

	bool bShowDemoWindow = m_bShowDemoWindow;
	if (bShowDemoWindow)
	{
		ImGui::SetNextWindowPos(ImVec2(0, 0));
		ImGui::ShowDemoWindow(&bShowDemoWindow);
	}
	m_bShowDemoWindow = bShowDemoWindow;

	if (m_showPerfWidget)
		m_pPerfMon->Update();
}

void CImguiImpl::InitImguiFontTexture()
{
	if (!gEnv->pRenderer)
	{
		CryLogAlways("[CryImGui] Failed to initialize; renderer not present.");
		return;
	}

	ImGuiIO& io = ImGui::GetIO();
	unsigned char* pixels;
	int width, height;
	io.Fonts->AddFontDefault();
	//io.Fonts->AddFontFromFileTTF("Engine\\Fonts\\VeraMono.ttf");
	io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
	//io.Fonts->GetTexDataAsAlpha8(&pixels, &width, &height);

	auto pFontTexture = gEnv->pRenderer->CreateTexture("ImGuiFontAtlas", width, height, 1, pixels, eTF_R8G8B8A8, 0);
	m_pFontTexture.Assign_NoAddRef(pFontTexture);
	m_pRenderer->m_pFontTexture = pFontTexture;	
	io.Fonts->TexID = pFontTexture;
}

CImguiImpl* CImguiImpl::Get()
{
	return g_pThis;
}

void CImguiImpl::OnSystemEvent(ESystemEvent event, UINT_PTR wparam, UINT_PTR lparam)
{
	if (event == ESYSTEM_EVENT_EDITOR_GAME_MODE_CHANGED)
	{
		/*if (wparam)
		{
			gEnv->pHardwareMouse->IncrementCounter();
		}
		else
		{
			gEnv->pHardwareMouse->DecrementCounter();
		}*/
	}
	else if (event == ESYSTEM_EVENT_CRYSYSTEM_INIT_DONE)
	{
		InitImgui();

		InitImguiFontTexture();
		gEnv->pHardwareMouse->AddListener(this);
		gEnv->pInput->AddEventListener(this);
		//gEnv->p3DEngine->SetPostEffectParam("Post3DRenderer_Active", 1.0f, true);
	}
}

void CImguiImpl::OnHardwareMouseEvent(int iX, int iY, EHARDWAREMOUSEEVENT eHardwareMouseEvent, int wheelDelta /*= 0*/)
{
	if (!s_bCaptured)
		return;

	m_cachedMouseEvents.emplace_back(iX, iY, eHardwareMouseEvent, wheelDelta);
}

bool CImguiImpl::OnInputEvent(const SInputEvent& event)
{
	//auto &io = ImGui::GetIO();

	if (event.keyId == eKI_F9 && event.state == eIS_Pressed)
		ImguiCaptureMouse(nullptr);

	if (!s_bCaptured || event.keyId == eKI_SYS_Commit)
		return false;
	
	m_cachedInputEvents.push_back(event);
		
	return true;
}

void CImguiImpl::OnCachedInputEvent(const SInputEvent &event)
{
	auto &io = ImGui::GetIO();

	if (!s_bCaptured || event.keyId == eKI_SYS_Commit)
		return;

	bool isDown = false;

	if (event.state & eIS_Down || event.state & eIS_Pressed)
		isDown = true;
	else if (event.state & eIS_Released)
		isDown = false;

	io.KeysDown[event.keyId] = isDown;

	switch (event.keyId)
	{
	case eKI_RAlt:
	case eKI_LAlt:
		io.KeyAlt = isDown;
		break;
	case eKI_RCtrl:
	case eKI_LCtrl:
		io.KeyCtrl = isDown;
		break;
	case eKI_RShift:
	case eKI_LShift:
		io.KeyShift = isDown;
		break;
	default:
		break;
	}
	if (event.deviceType == eIDT_Mouse || event.state == eIS_Released)
		return;

	auto keyName = event.keyName;
	if (event.state == eIS_Pressed)
	{
		char key = keyName[0];
		
		if(event.keyId == eKI_Space)
			key = ' ';
		else if(event.keyId == eKI_Tab)
			key = 9;
		else if (strlen(keyName) != 1)
			return;

		io.AddInputCharacter(key);		
	}
}

void CImguiImpl::OnCachedMouseEvent(int iX, int iY, EHARDWAREMOUSEEVENT eHardwareMouseEvent, int wheelDelta /*= 0*/)
{
	if (!s_bCaptured)
		return;

	ImGuiIO& io = ImGui::GetIO();

	switch (eHardwareMouseEvent)
	{
	case HARDWAREMOUSEEVENT_MOVE:
		io.MousePos = ImVec2((float)iX, (float)iY);
		break;
	case HARDWAREMOUSEEVENT_LBUTTONDOWN:
		io.MouseDown[0] = true;
		break;
	case HARDWAREMOUSEEVENT_LBUTTONUP:
		io.MouseDown[0] = false;
		break;
	case HARDWAREMOUSEEVENT_LBUTTONDOUBLECLICK:
		io.MouseDoubleClicked[0] = true;
		break;
	case HARDWAREMOUSEEVENT_RBUTTONDOWN:
		io.MouseDown[1] = true;
		break;
	case HARDWAREMOUSEEVENT_RBUTTONUP:
		io.MouseDown[1] = false;
		break;
	case HARDWAREMOUSEEVENT_RBUTTONDOUBLECLICK:
		io.MouseDoubleClicked[1] = true;
		break;
	case HARDWAREMOUSEEVENT_MBUTTONDOWN:
		io.MouseDown[2] = true;
		break;
	case HARDWAREMOUSEEVENT_MBUTTONUP:
		io.MouseDown[2] = false;
		break;
	case HARDWAREMOUSEEVENT_MBUTTONDOUBLECLICK:
		io.MouseDoubleClicked[2] = true;
		break;
	case HARDWAREMOUSEEVENT_WHEEL:
		io.MouseWheel += wheelDelta / WHEEL_DELTA;
		break;
	default:
		break;
	}
}

void CImguiImpl::DrawPerformance()
{
	gEnv->pRenderer->GetGPUFrameTime();
	SDebugFPSInfo info;
	gEnv->p3DEngine->FillDebugFPSInfo(info);
}
