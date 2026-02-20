#include "pch.h"
#include "neuRLcar.h"
#include "csvparser.h"
#include "bakkesmod/core/http_structs.h"

#include <windows.h>
#include <shlobj.h>
#include <string>
#include <vector>
#include <filesystem>
#include <string>



BAKKESMOD_PLUGIN(neuRLcar, "neuRLcar!", plugin_version, PLUGINTYPE_FREEPLAY)

std::shared_ptr<CVarManagerWrapper> _globalCvarManager;

WCHAR* myDocuments = new WCHAR[MAX_PATH];
HRESULT result = SHGetFolderPathW(NULL, CSIDL_PERSONAL, NULL, SHGFP_TYPE_CURRENT, myDocuments);
std::filesystem::path replayFolder = (std::filesystem::absolute(std::wstring(myDocuments)) / "My Games" / "Rocket League" / "TAGame" / "Demos");
std::filesystem::path replayFolderEpic = (std::filesystem::absolute(std::wstring(myDocuments)) / "My Games" / "Rocket League" / "TAGame" / "DemosEpic");

//test
std::string testreplayname = "mockypreds.csv";

char apiKeyInput[256] = "";
std::string savedApiKey = "";

std::vector<std::vector<double>>& getloadedData() //vector won't work as a global variable without doing this
{
	static std::vector<std::vector<double>> loadedData;
	return loadedData;
}

bool& replaydataloaded()
{
	static bool replaydataloaded = false;
	return replaydataloaded;
}

bool& loadingtoggle() //use this boolean so I don't have to call updateloadeddataset every frame to detect if there's a corresponding replay analysis
{
	static bool loadingtoggle = true;
	return loadingtoggle;
}

bool& isinreplay() //use this boolean so I don't have to call updateloadeddataset every frame to detect if there's a corresponding replay analysis
{
	static bool isinreplay = false;
	return isinreplay;
}

bool& wasInReplay_()
{
	static bool wasInReplay = false;
	return wasInReplay;
}

bool runPythonApplet(const std::string& exePath,
	const std::string& replayPath,
	const std::string& analysisPath)
{
	// Build command line
	std::string cmdLineStr = "\"" + exePath + "\" \"" + replayPath + "\" \"" + analysisPath + "\"";

	// Create pipes for stderr capture
	SECURITY_ATTRIBUTES sa{};
	sa.nLength = sizeof(sa);
	sa.bInheritHandle = TRUE;
	sa.lpSecurityDescriptor = NULL;

	HANDLE hStdErrRead = NULL;
	HANDLE hStdErrWrite = NULL;

	if (!CreatePipe(&hStdErrRead, &hStdErrWrite, &sa, 0)) {
		LOG("RunPythonApplet: Failed CreatePipe() for stderr");
		return false;
	}

	// Ensure the read handle is not inherited
	SetHandleInformation(hStdErrRead, HANDLE_FLAG_INHERIT, 0);

	STARTUPINFOA si{};
	PROCESS_INFORMATION pi{};
	si.cb = sizeof(si);

	si.dwFlags = STARTF_USESTDHANDLES;
	si.hStdError = hStdErrWrite;
	si.hStdOutput = NULL;
	si.hStdInput = NULL;

	// Convert command line to modifiable buffer
	std::vector<char> cmdBuf(cmdLineStr.begin(), cmdLineStr.end());
	cmdBuf.push_back('\0');

	BOOL ok = CreateProcessA(
		NULL,
		cmdBuf.data(),
		NULL,
		NULL,
		TRUE, // IMPORTANT: allow handle inheritance
		CREATE_NO_WINDOW,
		NULL,
		NULL,
		&si,
		&pi
	);

	if (!ok)
	{
		DWORD err = GetLastError();
		LOG("RunPythonApplet: CreateProcess failed with error " + std::to_string(err));
		CloseHandle(hStdErrWrite);
		CloseHandle(hStdErrRead);
		return false;
	}
	CloseHandle(hStdErrWrite);

	// Wait for the python applet to finish
	WaitForSingleObject(pi.hProcess, INFINITE);

	// Read all stderr output
	std::string stderrText;
	char buffer[512];
	DWORD bytesRead;

	while (true)
	{
		BOOL success = ReadFile(hStdErrRead, buffer, sizeof(buffer) - 1, &bytesRead, NULL);
		if (!success || bytesRead == 0)
			break;
		buffer[bytesRead] = '\0';
		stderrText += buffer;
	}

	CloseHandle(hStdErrRead);

	// Get exit code
	DWORD exitCode = 0;
	GetExitCodeProcess(pi.hProcess, &exitCode);

	CloseHandle(pi.hThread);
	CloseHandle(pi.hProcess);

	// If there was stderr output, log it
	if (!stderrText.empty()) { std::ofstream(exePath.substr(0, exePath.find_last_of("\\/")) + "\\applet_stderr.log", std::ios::binary) << stderrText; }

	if (exitCode != 0)
	{
		LOG("RunPythonApplet: Python applet exited with code " + std::to_string(exitCode));
		LOG("wrote an applet_stderr.log file next to the exe");
		return false;
	}

	return true;
}





void neuRLcar::onLoad()
{
	_globalCvarManager = cvarManager;
	gameWrapper->RegisterDrawable(
		std::bind(&neuRLcar::RenderCanvas, this, std::placeholders::_1)
	);

	cvarManager->registerNotifier("onTick", [this](std::vector<std::string> args) { //exectute command "onTick",  a test/example notifier
		onTick();
		}, "", PERMISSION_ALL);
	cvarManager->registerNotifier("generateAnalysis", [this](std::vector<std::string> args) {
		generateAnalysis();
		}, "", PERMISSION_REPLAY);
	cvarManager->registerNotifier("updateLoadedDataset", [this](std::vector<std::string> args) { 
		updateLoadedDataset();
		}, "", PERMISSION_REPLAY);

	cvarManager->registerCvar("currentframe", "0", "current replay frame");
	cvarManager->registerCvar("numframes", "0", "number of frames in this replay");
	cvarManager->registerCvar("neurlcar_analysis_busy", "0", "1 while neuRLcar analysis is running");

	cvarManager->registerCvar("neurlcar_model_ready", "0", "1 if the current model has required applet exe and _internal folder");

	cvarManager->registerCvar("neurlcar_ui_show_topbars", "0", "");
	cvarManager->registerCvar("neurlcar_ui_show_maineval", "1", "");
	cvarManager->registerCvar("neurlcar_ui_show_mainbg", "1", "");
	cvarManager->registerCvar("neurlcar_ui_show_midline", "1", "");
	cvarManager->registerCvar("neurlcar_ui_show_presentband", "1", "");
	cvarManager->registerCvar("neurlcar_ui_past_breadth", "150", "");
	cvarManager->registerCvar("neurlcar_ui_future_breadth", "150", "");
	cvarManager->registerCvar("neurlcar_ui_smoothing_window", "0", "");
	cvarManager->registerCvar("neurlcar_ui_debug_grid", "0", "Show debug grid (1% increments)");
	cvarManager->registerCvar("neurlcar_ui_enabled", "1", "Enable neuRLcar UI");
	cvarManager->registerCvar("neurlcar_ui_show_hotkey_reminders", "1", "Show hotkey reminder text");
	cvarManager->registerCvar("neurlcar_ui_maineval_alpha","165","Alpha transparency for main eval background (0-255)",true, true, 0.0f, true, 255.0f);
	cvarManager->registerCvar(
		"neurlcar_ui_open_window_on_replay", "1", "Open neuRLcar window automatically when entering a replay");
	cvarManager->registerCvar("neurlcar_current_model","neurlcar","Model folder name under bakkesmod/data/neurlcar/models/<model>/");



	cvarManager->registerCvar("neurlcar_initialized", "0", "Whether the plugin has run its initialization function");
	cvarManager->registerCvar("plugin_settings_keybind", "Z", "Hotkey for neuRLcar settings (toggle menu)");
	cvarManager->registerCvar("analysis_keybind", "X", "Hotkey for neuRLcar analysis generation");
	if (cvarManager->getCvar("neurlcar_initialized").getIntValue() == 0)
	{
		LOG("neurlcar init: enter");

		constexpr const char* kModelCvar = "neurlcar_current_model";
		auto modelCvar = cvarManager->getCvar(kModelCvar);
		std::string model = modelCvar.IsNull() ? "neurlcar" : modelCvar.getStringValue();


		auto bmPath = gameWrapper->GetBakkesModPath();
		auto init_dir = bmPath / "data" / "neurlcar" / "models" / model / "demoanalysis";


		std::error_code ec;
		std::filesystem::create_directories(init_dir, ec);

		saveKeybinds();
		cvarManager->getCvar("neurlcar_initialized").setValue(1);

	}


	//call onTick() on every tick
	gameWrapper->HookEvent("Function Engine.GameViewportClient.Tick",
		[this](std::string eventName) {
			onTick();
		});
}

void neuRLcar::saveKeybinds()
{
	CVarWrapper settingsKey = cvarManager->getCvar("plugin_settings_keybind");
	CVarWrapper analysisKey = cvarManager->getCvar("analysis_keybind");

	if (settingsKey.IsNull() || analysisKey.IsNull())
		return;

	std::string keySettings = settingsKey.getStringValue();
	std::string keyAnalysis = analysisKey.getStringValue();

	// Bind the commands using current CVar values
	cvarManager->executeCommand("bind " + keySettings + " \"togglemenu " + GetMenuName() + "\"");
	cvarManager->executeCommand("bind " + keyAnalysis + " \"generateAnalysis\"");

	LOG("ReplayFrames: Bound [" + keySettings + "] to open menu and [" + keyAnalysis + "] to generate analysis.");
}




void neuRLcar::onTick()
{
	const bool inReplayNow = gameWrapper->IsInReplay();
	const bool justEnteredReplay = (inReplayNow && !wasInReplay_());
	wasInReplay_() = inReplayNow;

	if (inReplayNow)
	{
		isinreplay() = true;

		if (!replaydataloaded() && loadingtoggle())
		{
			updateLoadedDataset();
			loadingtoggle() = false;
		}

		// Auto-open window ONCE when entering replay (if enabled)
		if (justEnteredReplay)
		{
			bool openOnReplay = cvarManager->getCvar("neurlcar_ui_open_window_on_replay").getBoolValue();
			if (openOnReplay && !isWindowOpen_)
			{
				_globalCvarManager->executeCommand("openmenu " + GetMenuName());
			}
		}
	}
	else
	{
		isinreplay() = false;
		replaydataloaded() = false;
		loadingtoggle() = true;

		if (isWindowOpen_)
			_globalCvarManager->executeCommand("closemenu " + GetMenuName());

		return;
	}

	ReplayServerWrapper serverReplay = gameWrapper->GetGameEventAsReplay();
	if (serverReplay.IsNull()) return;

	ReplayWrapper replay = serverReplay.GetReplay();
	if (replay.IsNull()) return;

	int currentframe = serverReplay.GetCurrentReplayFrame();
	int numframes = replay.GetNumFrames();

	auto currentframecvar = cvarManager->getCvar("currentframe");
	if (currentframecvar.IsNull()) return;
	auto numframescvar = cvarManager->getCvar("numframes");
	if (numframescvar.IsNull()) return;

	currentframecvar.setValue(currentframe);
	numframescvar.setValue(numframes);
}



void neuRLcar::updateLoadedDataset()
{
	getloadedData().clear();

	if (!gameWrapper->IsInReplay()) return;
	ReplayServerWrapper serverReplay = gameWrapper->GetGameEventAsReplay();
	if (serverReplay.IsNull()) return;
	ReplayWrapper replay = serverReplay.GetReplay();
	if (replay.IsNull()) return;
	//if (replaydataloaded()) return;
	auto current_model = cvarManager->getCvar("neurlcar_current_model").getStringValue();
	auto replayid = replay.GetId().ToString();
	auto bakkespath = gameWrapper->GetBakkesModPath();
	auto analysispath = (bakkespath / "data" / "neurlcar" / "models" / current_model / "demoanalysis" / (replayid + ".csv"));
	//check if analysis exists
	std::ifstream infile(analysispath.c_str());
	bool analysisExists = infile.good();
	infile.close();
	if (!analysisExists) 
	{ 
		LOG("no analysis file for this replay exists");
		return;
	}

	LOG("Analysis found");

	auto datatoload = csvparser(analysispath);

	LOG("This is the demoanalysis path {}", (analysispath).string());
	LOG("datatoloadsize is {}", (std::to_string(datatoload.size())));

	
	getloadedData() = datatoload;

	LOG("loadeddata size is {}", (std::to_string(getloadedData().size())));

	replaydataloaded() = true;

	return;
}

void neuRLcar::deleteLoadedDatasetFile()
{
	getloadedData().clear();
	replaydataloaded() = false;

	if (!gameWrapper || !gameWrapper->IsInReplay())
		return;

	ReplayServerWrapper serverReplay = gameWrapper->GetGameEventAsReplay();
	if (serverReplay.IsNull())
		return;

	ReplayWrapper replay = serverReplay.GetReplay();
	if (replay.IsNull())
		return;

	auto replayname = replay.GetId().ToString();
	auto bakkespath = gameWrapper->GetBakkesModPath();
	auto current_model = cvarManager->getCvar("neurlcar_current_model").getStringValue();
	auto analysispath = (bakkespath / "data" / "neurlcar" / "models" / current_model / "demoanalysis" / (replayname + ".csv"));


	std::error_code ec;
	bool removed = std::filesystem::remove(analysispath, ec);

	if (ec)
	{
		LOG("Failed to delete analysis file {} (error: {})", analysispath.string(), ec.message());
		return;
	}

	if (removed)
		LOG("Deleted analysis file {}", analysispath.string());
	else
		LOG("No analysis file to delete at {}", analysispath.string());
}





void neuRLcar::generateAnalysis()
{
	if (cvarManager->getCvar("neurlcar_analysis_busy").getBoolValue())
		return;

	cvarManager->getCvar("neurlcar_analysis_busy").setValue(1);

	ReplayServerWrapper serverReplay = gameWrapper->GetGameEventAsReplay();
	if (serverReplay.IsNull()) return;

	ReplayWrapper replay = serverReplay.GetReplay();
	if (replay.IsNull()) return;

	auto replayname = replay.GetId().ToString();
	std::filesystem::path candidateSteam = replayFolder / (replayname + ".replay");
	std::filesystem::path candidateEpic = replayFolderEpic / (replayname + ".replay");

	std::filesystem::path replayPathFs =
		std::filesystem::exists(candidateEpic) ? candidateEpic :
		std::filesystem::exists(candidateSteam) ? candidateSteam :
		candidateEpic; // default

	auto replaypath = replayPathFs.string();
	LOG("replay path chosen as: " + replaypath);
	auto bakkespath = gameWrapper->GetBakkesModPath();
	auto current_model = cvarManager->getCvar("neurlcar_current_model").getStringValue();
	auto analysispath = (bakkespath / "data" / "neurlcar" / "models" / current_model / "demoanalysis" / (replayname + ".csv")).string();
	auto exePath = (bakkespath / "data" / "neurlcar" / "models" / current_model / (current_model + "_applet.exe")).string();

	LOG("ReplayFrames: async analysis requested for " + replayname);

	std::thread([=]() {

		LOG("ReplayFrames: (thread) starting Python...");
		bool ok = runPythonApplet(exePath, replaypath, analysispath);

		if (!ok) {
			gameWrapper->Execute([this](GameWrapper*) {
				cvarManager->getCvar("neurlcar_analysis_busy").setValue(0);
				});
			return;
		}

		{
			std::ifstream infile(analysispath);
			if (!infile.good()) {
				gameWrapper->Execute([this](GameWrapper*) {
					cvarManager->getCvar("neurlcar_analysis_busy").setValue(0);
					});
				return;
			}
		}

		LOG("ReplayFrames: (thread) CSV generated successfully");

		gameWrapper->Execute([this](GameWrapper*) {
			updateLoadedDataset();
			cvarManager->getCvar("neurlcar_analysis_busy").setValue(0);
			});

		}).detach();
}

