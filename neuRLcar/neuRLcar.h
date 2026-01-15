#pragma once

#include "bakkesmod/plugin/bakkesmodplugin.h"
#include "bakkesmod/plugin/pluginwindow.h"
#include "bakkesmod/plugin/PluginSettingsWindow.h"
#include "GuiBase.h"
#include "bakkesmod/wrappers/canvaswrapper.h"


#include "version.h"

#include <windows.h>
#include <fstream>
#include <vector>

constexpr auto plugin_version = stringify(VERSION_MAJOR) "." stringify(VERSION_MINOR) "." stringify(VERSION_PATCH) "." stringify(VERSION_BUILD);
std::vector<std::vector<double>> &getloadedData();

extern std::shared_ptr<CVarManagerWrapper> _globalCvarManager;
extern WCHAR* myDocuments;
extern std::filesystem::path replayFolder;
extern std::string savedApiKey;
extern char apiKeyInput[256];

extern bool showCreateAccountWindow;
extern char usernameInput[128];
extern char passwordInput[128];
extern char retypePasswordInput[128];

// Function declarations for globals managed with static variables
std::vector<std::vector<double>>& getloadedData();
bool& replaydataloaded();
bool& loadingtoggle();
bool& isinreplay();
bool& wasInReplay_();

class neuRLcar: public BakkesMod::Plugin::BakkesModPlugin,

	public SettingsWindowBase, // Uncomment if you wanna render your own tab in the settings menu
	public PluginWindowBase // Uncomment if you want to render your own plugin window
{
	void onLoad() override;
	std::string GetCurrentModelName() const;
	void saveKeybinds();
	void onTick();
	void renderEvalGraph(const char* id, const std::vector<double>& evaluation, int currentframe, ImU32 lowFillColor = IM_COL32(255, 165, 0, 255),   ImU32 highFillColor = IM_COL32(0, 0, 255, 255));
	void updateLoadedDataset();
	void deleteLoadedDatasetFile();
	void generateAnalysis();

public:
	void RenderSettingsContents();
	void RenderSettings() override;
	void RenderWindow() override; 
	void RenderCanvas(CanvasWrapper canvas);

	void DrawCanvasText(CanvasWrapper& canvas, const std::string& text, float x, float y);

};
