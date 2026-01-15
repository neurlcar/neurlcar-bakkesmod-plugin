#include "pch.h"
#include "neuRLcar.h"


void neuRLcar::RenderWindow()
{
    bool modelReady = false;
    {
        CVarWrapper ready = cvarManager->getCvar("neurlcar_model_ready");
        modelReady = (!ready.IsNull() && ready.getIntValue() == 1);
    }

    if (modelReady)
    {
        if (ImGui::Button("Generate analysis"))
            generateAnalysis();
        ImGui::Separator();
    }

	RenderSettingsContents();

	ImGui::Separator();
	
	if (!replaydataloaded()) return;

    CVarWrapper currentframecvar = cvarManager->getCvar("currentframe");
    if (currentframecvar.IsNull()) return;
    CVarWrapper numframescvar = cvarManager->getCvar("numframes");
    if (numframescvar.IsNull()) return;

    int currentframe = currentframecvar.getIntValue();
    int numframes = numframescvar.getIntValue();

	auto eval = getloadedData()[0][currentframe];
    ImGui::Text("eval of current frame, 0 blue is winning, 1 orange is winning: %.4f", eval);
    renderEvalGraph("##eval_graph", getloadedData()[0], currentframe);
    ImGui::Separator();

    auto imm = getloadedData()[2][currentframe];
    ImGui::Text("probability <3seconds (90 frames) until a goal: %.4f", imm);
    renderEvalGraph("##imm_graph", getloadedData()[2], currentframe, IM_COL32(255, 255, 255, 255), IM_COL32(0, 0, 0, 255));
    ImGui::Separator();

}




void neuRLcar::renderEvalGraph(
    const char* id,
    const std::vector<double>& evaluation,
    int currentframe,
    ImU32 lowFillColor,
    ImU32 highFillColor
)
{
    const int evalDisplayBreadth = 301;
    const int halfWindow = 150;
    const ImVec2 graphSize = ImVec2(1920.0f / 3.0f, 1080.0f / 6.0f);
    float rectWidth = graphSize.x / evalDisplayBreadth;

    int minFrame = currentframe - halfWindow;

    float values[evalDisplayBreadth] = {};

    for (int i = 0; i < evalDisplayBreadth; i++) {
        int frame = minFrame + i;
        if (frame < 0 || frame >= (int)evaluation.size()) {
            values[i] = -1.0f;
        }
        else {
            values[i] = (float)evaluation[frame];
        }
    }

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();

    drawList->AddRectFilled(p, ImVec2(p.x + graphSize.x, p.y + graphSize.y), IM_COL32(255, 255, 255, 255));

    for (int i = 0; i < evalDisplayBreadth - 1; i++) {
        float x0 = p.x + i * rectWidth;
        float x1 = x0 + rectWidth;

        if (values[i] == -1.0f || values[i + 1] == -1.0f) {
            drawList->AddRectFilled(ImVec2(x0, p.y), ImVec2(x1, p.y + graphSize.y), IM_COL32(200, 200, 200, 255));
        }
        else {
            float y0 = p.y + (1.0f - values[i]) * graphSize.y;

            drawList->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, p.y + graphSize.y), lowFillColor);
            drawList->AddRectFilled(ImVec2(x0, p.y), ImVec2(x1, y0), highFillColor);
        }
    }

    ImU32 col = IM_COL32(80, 80, 80, 255);
    const float horizontalSpacing = 0.100f * graphSize.y;

    for (float y = p.y; y < p.y + graphSize.y; y += horizontalSpacing) {
        drawList->AddLine(ImVec2(p.x, y), ImVec2(p.x + graphSize.x, y), col, 0.005f);
    }

    drawList->AddLine(ImVec2(p.x + graphSize.x / 2.0f, p.y), ImVec2(p.x + graphSize.x / 2.0f, p.y + graphSize.y), col, 4.0f);

    ImGui::InvisibleButton(id, graphSize);
}
