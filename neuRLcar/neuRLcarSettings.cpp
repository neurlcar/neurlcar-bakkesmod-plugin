#include <filesystem>
#include <vector>
#include <string>
#include <algorithm>
#include "pch.h"
#include "neuRLcar.h"

#include <windows.h>
#include <shellapi.h>


void neuRLcar::RenderSettingsContents()
{
    ImGui::TextUnformatted("neuRLcar plugin settings");
    ImGui::Separator();


    auto C = [&](const char* name) { return cvarManager->getCvar(name); };

    auto SetBoolAndSave = [&](const char* cvarName, bool value)
        {
            C(cvarName).setValue(value);
            cvarManager->executeCommand("writeconfig");
        };

    auto SetIntAndSave = [&](const char* cvarName, int value)
        {
            C(cvarName).setValue(value);
            cvarManager->executeCommand("writeconfig");
        };

    auto GetModelsDir = [&]() -> std::filesystem::path
        {
            return gameWrapper->GetBakkesModPath() / "data" / "neurlcar" / "models";
        };

    auto GetModelDir = [&](const std::string& model) -> std::filesystem::path
        {
            return GetModelsDir() / model;
        };

    auto GetModelExePath = [&](const std::string& model) -> std::filesystem::path
        {
            return GetModelDir(model) / (model + "_applet.exe");
        };

    auto GetModelInternalDir = [&](const std::string& model) -> std::filesystem::path
        {
            return GetModelDir(model) / "_internal";
        };

    auto CheckModelInstall = [&](const std::string& model, std::string& outReason) -> bool
        {
            std::error_code ec;

            const auto modelDir = GetModelDir(model);
            if (!std::filesystem::exists(modelDir, ec) || !std::filesystem::is_directory(modelDir, ec))
            {
                outReason = "Model folder missing: " + modelDir.string();
                return false;
            }

            const auto exePath = GetModelExePath(model);
            if (!std::filesystem::exists(exePath, ec) || !std::filesystem::is_regular_file(exePath, ec))
            {
                outReason = "Missing applet exe: " + exePath.string();
                return false;
            }

            const auto internalDir = GetModelInternalDir(model);
            if (!std::filesystem::exists(internalDir, ec) || !std::filesystem::is_directory(internalDir, ec))
            {
                outReason = "Missing _internal folder: " + internalDir.string();
                return false;
            }

            outReason.clear();
            return true;
        };

    auto UpdateModelReadyCvar = [&](const std::string& model)
        {
            std::string reason;
            bool ok = CheckModelInstall(model, reason);

            CVarWrapper ready = C("neurlcar_model_ready");
            if (!ready.IsNull())
                ready.setValue(ok ? 1 : 0);

            cvarManager->executeCommand("writeconfig");

            if (ok)
                LOG("Model verify OK for '{}'", model);
            else
                LOG("Model verify FAILED for '{}': {}", model, reason);
        };

    // ---------------------
    // Model selection list 
    // ---------------------
    CVarWrapper modelCvar = C("neurlcar_current_model");
    std::string current_model = modelCvar.IsNull() ? "neurlcar" : modelCvar.getStringValue();

    const std::filesystem::path modelsDir = GetModelsDir();

    static std::vector<std::string> modelNames;
    static std::vector<const char*> modelItems;
    static int selectedIndex = -1;
    static bool modelsInit = false;

    auto RefreshModels = [&]()
        {
            modelNames.clear();
            modelItems.clear();
            selectedIndex = -1;

            std::error_code ec;
            if (!std::filesystem::exists(modelsDir, ec))
                return;

            for (auto it = std::filesystem::directory_iterator(modelsDir, ec);
                !ec && it != std::filesystem::directory_iterator();
                ++it)
            {
                const auto& entry = *it;
                if (!entry.is_directory(ec))
                    continue;

                std::string name = entry.path().filename().string();
                if (!name.empty())
                    modelNames.push_back(name);
            }

            std::sort(modelNames.begin(), modelNames.end());

            modelItems.reserve(modelNames.size());
            for (auto& s : modelNames)
                modelItems.push_back(s.c_str());

            for (int i = 0; i < (int)modelNames.size(); ++i)
            {
                if (modelNames[i] == current_model)
                {
                    selectedIndex = i;
                    break;
                }
            }

            if (selectedIndex < 0 && !modelNames.empty())
                selectedIndex = 0;
        };

    if (!modelsInit)
    {
        RefreshModels();
        modelsInit = true;

        // On first open, set readiness cvar based on current model
        UpdateModelReadyCvar(current_model);
    }

    bool modelReady = false;
    {
        CVarWrapper ready = C("neurlcar_model_ready");
        modelReady = (!ready.IsNull() && ready.getIntValue() == 1);
    }

    // Always re-check the currently-selected model if the cvar says "ready",
    // because users can delete files while RL is running
    if (modelReady)
    {
        std::string reason;
        if (!CheckModelInstall(current_model, reason))
        {
            // Flip back to setup mode
            C("neurlcar_model_ready").setValue(0);
            cvarManager->executeCommand("writeconfig");
            modelReady = false;
        }
    }

    if (!modelReady)
    {
        // =================
        // Model Setup Page
        // =================
        ImGui::TextUnformatted("neuRLcar - Model Setup");
        ImGui::Separator();

        ImGui::TextWrapped("The python machine learning part of this plugin is too big for hosting on the bakkesmod plugin hub -- you have to download it directly.");
        ImGui::TextWrapped("Once the python part of this plugin is installed, the full settings page will appear here.");
        if (ImGui::Button("Click here to open download instructions (GitHub Releases)"))
        {
            ShellExecuteA(
                nullptr,
                "open",
                "https://github.com/neurlcar/neurlcar-python/releases/tag/v1.0.0",
                nullptr,
                nullptr,
                SW_SHOWNORMAL
            );
        }

        ImGui::Separator();
        ImGui::TextUnformatted("directory structure status:");

        {
            const auto modelDir = GetModelDir(current_model);
            const auto exePath = GetModelExePath(current_model);
            const auto internalDir = GetModelInternalDir(current_model);

            std::error_code ec;
            bool hasExe = std::filesystem::exists(exePath, ec) && std::filesystem::is_regular_file(exePath, ec);
            bool hasInternal = std::filesystem::exists(internalDir, ec) && std::filesystem::is_directory(internalDir, ec);

            ImGui::Text("Applet exe: %s", hasExe ? "FOUND" : "MISSING");
            ImGui::Text("_internal:  %s", hasInternal ? "FOUND" : "MISSING");
        }

        ImGui::Separator();

        if (ImGui::Button("Verify model install"))
        {
            UpdateModelReadyCvar(current_model);

        }

        return;
    }

    // =====================
    // Normal Settings Page 
    // =====================

    bool b;
    int v;

    ImGui::Text("UI Visibility");
    ImGui::Separator();

    bool openOnReplay = C("neurlcar_ui_open_window_on_replay").getBoolValue();
    if (ImGui::Checkbox("Open neuRLcar window automatically in replays", &openOnReplay))
    {
        C("neurlcar_ui_open_window_on_replay").setValue(openOnReplay);
        cvarManager->executeCommand("writeconfig");
    }

    b = C("neurlcar_ui_show_topbars").getBoolValue();
    if (ImGui::Checkbox("Show top bars", &b))
        SetBoolAndSave("neurlcar_ui_show_topbars", b);

    b = C("neurlcar_ui_show_maineval").getBoolValue();
    if (ImGui::Checkbox("Show main eval", &b))
        SetBoolAndSave("neurlcar_ui_show_maineval", b);

    b = C("neurlcar_ui_show_hotkey_reminders").getBoolValue();
    if (ImGui::Checkbox("Show hotkey reminders", &b))
        SetBoolAndSave("neurlcar_ui_show_hotkey_reminders", b);

    b = C("neurlcar_ui_enabled").getBoolValue();
    if (ImGui::Checkbox("neuRLcar replay overlay on/off", &b))
        SetBoolAndSave("neurlcar_ui_enabled", b);

    ImGui::Separator();
    ImGui::Text("Eval Display Settings (replays are 30 frames per second)");
    ImGui::Separator();

    int alpha = C("neurlcar_ui_maineval_alpha").getIntValue();
    float alpha_f = (float)alpha;

    bool changed = ImGui::SliderFloat("Main display transparency", &alpha_f, 0.0f, 255.0f, "%.0f");

    int a = (int)alpha_f;
    if (a < 0) a = 0;
    if (a > 255) a = 255;

    if (changed)
    {
        cvarManager->getCvar("neurlcar_ui_maineval_alpha").setValue(a);
    }

    if (ImGui::IsItemDeactivatedAfterEdit())
    {
        SetIntAndSave("neurlcar_ui_maineval_alpha", a);
    }

    v = C("neurlcar_ui_smoothing_window").getIntValue();
    if (ImGui::InputInt("Smoothing window (frames)", &v))
        SetIntAndSave("neurlcar_ui_smoothing_window", v);

    //bool grid = C("neurlcar_ui_debug_grid").getBoolValue();
    //if (ImGui::Checkbox("Debug grid (1% increments)", &grid))
    //    SetBoolAndSave("neurlcar_ui_debug_grid", grid);

    ImGui::Separator();
    ImGui::Text("Hotkeys");
    ImGui::Separator();

    static char settingsKeyBuf[32];
    static char analysisKeyBuf[32];

    static bool hotkeyBufInit = false;
    if (!hotkeyBufInit)
    {
        std::string s = C("plugin_settings_keybind").getStringValue();
        std::string a2 = C("analysis_keybind").getStringValue();

        strncpy_s(settingsKeyBuf, s.c_str(), sizeof(settingsKeyBuf) - 1);
        strncpy_s(analysisKeyBuf, a2.c_str(), sizeof(analysisKeyBuf) - 1);
        hotkeyBufInit = true;
    }

    ImGui::InputText("Settings key", settingsKeyBuf, IM_ARRAYSIZE(settingsKeyBuf));
    ImGui::InputText("Analyze key", analysisKeyBuf, IM_ARRAYSIZE(analysisKeyBuf));

    if (ImGui::Button("Apply keybinds"))
    {
        C("plugin_settings_keybind").setValue(std::string(settingsKeyBuf));
        C("analysis_keybind").setValue(std::string(analysisKeyBuf));

        saveKeybinds();
    }

    ImGui::SameLine();

    if (ImGui::Button("Restore defaults"))
    {
        C("plugin_settings_keybind").setValue("Z");
        C("analysis_keybind").setValue("X");

        strncpy_s(settingsKeyBuf, "Z", sizeof(settingsKeyBuf) - 1);
        strncpy_s(analysisKeyBuf, "X", sizeof(analysisKeyBuf) - 1);

        saveKeybinds();
    }

    ImGui::Separator();

    if (ImGui::Button("Delete analysis for current replay"))
    {
        deleteLoadedDatasetFile();
    }

    ImGui::Separator();

    ImGui::Text("Change model:");
    if (modelNames.empty())
    {
        ImGui::TextWrapped("No model folders found. Expected:");
        ImGui::TextWrapped(".../data/neurlcar/models/<model>/demoanalysis/");
    }
    else
    {
        int prevIndex = selectedIndex;

        if (ImGui::Combo("Current model", &selectedIndex, modelItems.data(), (int)modelItems.size()))
        {
            if (selectedIndex >= 0 &&
                selectedIndex < (int)modelNames.size() &&
                selectedIndex != prevIndex)
            {
                if (!modelCvar.IsNull())
                {
                    modelCvar.setValue(modelNames[selectedIndex]);
                    cvarManager->executeCommand("writeconfig");

                    // When switching models, go back to setup mode until verified
                    C("neurlcar_model_ready").setValue(0);
                    cvarManager->executeCommand("writeconfig");

                    updateLoadedDataset();
                }
            }
        }

        if (ImGui::Button("Refresh model list"))
        {
            current_model = modelCvar.IsNull() ? "neurlcar" : modelCvar.getStringValue();
            RefreshModels();
        }
        ImGui::Separator();
    }
}

void neuRLcar::RenderSettings()
{
    RenderSettingsContents();
}
