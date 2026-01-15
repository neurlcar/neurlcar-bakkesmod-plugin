#include "pch.h"
#include "neuRLcar.h"
#include "bakkesmod/wrappers/canvaswrapper.h"
#include "bakkesmod/wrappers/ImageWrapper.h"
#include <filesystem>


#include <vector>
#include <memory>
#include <string>

std::vector<std::vector<double>>& getloadedData();
bool& replaydataloaded();
bool& isinreplay();

// Subtle grey used for outlines + separators + grid
static constexpr int UI_GREY_R = 150;
static constexpr int UI_GREY_G = 150;
static constexpr int UI_GREY_B = 150;
static constexpr int UI_GREY_A = 220;

static constexpr int GRID_A = 120;

// ====================
// Layout
// ====================
struct NeuRLcarLayout
{
    float screenW = 0.0f;
    float screenH = 0.0f;

    // Top advantage bars
    Vector2 barSize{};
    Vector2 leftBarPos{};
    Vector2 rightBarPos{};

    // Main eval display box
    Vector2 mainPos{};
    Vector2 mainSize{};
};

static constexpr float BAR_HEIGHT_FRAC = 0.07f;
static constexpr float BAR_WIDTH_FRAC = 0.40f;

static constexpr float MAIN_TOP_FRAC = 0.098f;
static constexpr float MAIN_HEIGHT_FRAC = 0.14f;
static constexpr float MAIN_WIDTH_FRAC = 0.20f;

static float Clamp01(float v)
{
    if (v < 0.0f) return 0.0f;
    if (v > 1.0f) return 1.0f;
    return v;
}

static int ClampInt(int v, int lo, int hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static NeuRLcarLayout ComputeLayout(const Vector2& screenSize)
{
    NeuRLcarLayout lay;
    lay.screenW = screenSize.X;
    lay.screenH = screenSize.Y;

    // Top bars
    float barW = lay.screenW * BAR_WIDTH_FRAC;
    float barH = lay.screenH * BAR_HEIGHT_FRAC;
    lay.barSize = Vector2(barW, barH);
    lay.leftBarPos = Vector2(0.0f, 0.0f);
    lay.rightBarPos = Vector2(lay.screenW - barW, 0.0f);

    // Main eval box (aligned to scoreboard)
    float mainW = lay.screenW * MAIN_WIDTH_FRAC;
    float mainH = lay.screenH * MAIN_HEIGHT_FRAC;
    float mainX = (lay.screenW - mainW) * 0.5f;
    float mainY = lay.screenH * MAIN_TOP_FRAC;

    lay.mainPos = Vector2(mainX, mainY);
    lay.mainSize = Vector2(mainW, mainH);

    return lay;
}

// Debug grid: 1% increments across screen
static void RenderDebugGrid(CanvasWrapper& canvas)
{
    Vector2 s = canvas.GetSize();
    float W = s.X;
    float H = s.Y;

    const int minorA = 50;   // 1% lines
    const int majorA = 110;  // 10% lines
    const int r = 160, g = 160, b = 160;

    for (int i = 0; i <= 100; ++i)
    {
        int a = (i % 10 == 0) ? majorA : minorA;
        float x = (W * (float)i) / 100.0f;

        canvas.SetColor(r, g, b, a);
        canvas.SetPosition(Vector2(x, 0.0f));
        canvas.FillBox(Vector2(1.0f, H));
    }

    for (int i = 0; i <= 100; ++i)
    {
        int a = (i % 10 == 0) ? majorA : minorA;
        float y = (H * (float)i) / 100.0f;

        canvas.SetColor(r, g, b, a);
        canvas.SetPosition(Vector2(0.0f, y));
        canvas.FillBox(Vector2(W, 1.0f));
    }
}

// Pixel slicing for rows (prevents seams / gaps at different resolutions)
static void GetRowPixelSpan(const NeuRLcarLayout& lay, int row, int totalRows, int& y0, int& y1)
{
    float top = lay.mainPos.Y;
    float h = lay.mainSize.Y;

    float fy0 = top + (h * (float)row) / (float)totalRows;
    float fy1 = top + (h * (float)(row + 1)) / (float)totalRows;

    y0 = (int)fy0;
    y1 = (int)fy1;
    if (y1 <= y0) y1 = y0 + 1;
}
// Smoothing: avg over [frame-half, frame+half], half = smoothingWindow/2
static float SmoothedEvalAt(const std::vector<double>& evalSeries, int frame, int smoothingWindow)
{
    int n = (int)evalSeries.size();
    if (n <= 0) return 0.5f;

    if (frame < 0) frame = 0;
    if (frame >= n) frame = n - 1;

    if (smoothingWindow <= 0) return Clamp01((float)evalSeries[frame]);

    int half = smoothingWindow / 2;
    int lo = frame - half;
    int hi = frame + half;

    if (lo < 0) lo = 0;
    if (hi >= n) hi = n - 1;

    double sum = 0.0;
    int count = 0;
    for (int i = lo; i <= hi; ++i)
    {
        sum += evalSeries[i];
        ++count;
    }

    if (count <= 0) return Clamp01((float)evalSeries[frame]);
    return Clamp01((float)(sum / (double)count));
}

static void DrawHorizontalEvalGraph(CanvasWrapper& canvas,
    const NeuRLcarLayout& lay,
    const std::vector<double>& evalSeries,
    int currentframe,
    int smoothingWindow,
    bool showBackground,
    int bgAlpha)
{
    const int evalDisplayBreadth = 301;
    const int halfWindow = 150;

    int x0 = (int)lay.mainPos.X;
    int x1 = (int)(lay.mainPos.X + lay.mainSize.X);
    if (x1 <= x0) x1 = x0 + 1;
    int w = x1 - x0;

    int y0 = (int)lay.mainPos.Y;
    int y1 = (int)(lay.mainPos.Y + lay.mainSize.Y);
    if (y1 <= y0) y1 = y0 + 1;
    int h = y1 - y0;

    if (showBackground)
    {
        canvas.SetColor(255, 255, 255, bgAlpha);
        canvas.SetPosition(lay.mainPos);
        canvas.FillBox(lay.mainSize);
    }

    int minFrame = currentframe - halfWindow;

    for (int i = 0; i < evalDisplayBreadth; ++i)
    {
        int xi0 = x0 + (w * i) / evalDisplayBreadth;
        int xi1 = x0 + (w * (i + 1)) / evalDisplayBreadth;
        if (xi1 <= xi0) xi1 = xi0 + 1;

        int frame = minFrame + i;

        float v = -1.0f;
        if (frame >= 0 && frame < (int)evalSeries.size())
            v = SmoothedEvalAt(evalSeries, frame, smoothingWindow);

        if (v < 0.0f)
        {
            canvas.SetColor(200, 200, 200, bgAlpha);
            canvas.SetPosition(Vector2((float)xi0, (float)y0));
            canvas.FillBox(Vector2((float)(xi1 - xi0), (float)h));
            continue;
        }

        int ySplit = y0 + (int)((1.0f - v) * (float)h);
        ySplit = ClampInt(ySplit, y0, y1);

        int topH = ySplit - y0;
        int botH = y1 - ySplit;

        if (topH > 0)
        {
            canvas.SetColor(0, 0, 255, bgAlpha);
            canvas.SetPosition(Vector2((float)xi0, (float)y0));
            canvas.FillBox(Vector2((float)(xi1 - xi0), (float)topH));
        }

        if (botH > 0)
        {
            canvas.SetColor(255, 165, 0, bgAlpha);
            canvas.SetPosition(Vector2((float)xi0, (float)ySplit));
            canvas.FillBox(Vector2((float)(xi1 - xi0), (float)botH));
        }
    }

    canvas.SetColor(UI_GREY_R, UI_GREY_G, UI_GREY_B, GRID_A);
    for (int j = 1; j < 10; ++j)
    {
        int yi = y0 + (h * j) / 10;

        int thick = (j == 5) ? 3 : 1;
        int yStart = yi - (thick / 2);

        if (yStart < y0) yStart = y0;
        if (yStart + thick > y1) yStart = y1 - thick;

        canvas.SetPosition(Vector2((float)x0, (float)yStart));
        canvas.FillBox(Vector2((float)w, (float)thick));
    }

    int xCenter = x0 + (w / 2);
    int centerThick = 4;
    int xStart = xCenter - (centerThick / 2);
    if (xStart < x0) xStart = x0;
    if (xStart + centerThick > x1) xStart = x1 - centerThick;

    canvas.SetColor(UI_GREY_R, UI_GREY_G, UI_GREY_B, UI_GREY_A);
    canvas.SetPosition(Vector2((float)xStart, (float)y0));
    canvas.FillBox(Vector2((float)centerThick, (float)h));

    canvas.SetColor(UI_GREY_R, UI_GREY_G, UI_GREY_B, UI_GREY_A);
    canvas.SetPosition(lay.mainPos);
    canvas.DrawBox(lay.mainSize);
}


// ====================
// CVar-driven config
// ====================
struct NeuRLcarConfig
{
    bool showTopBars = true;
    bool showMainEval = true;
    bool showMainBackground = false;

    int mainEvalAlpha = 220;

    bool showTitleText = true;
    bool showHotkeyReminders = true;

    int pastRows = 150;          // frames
    int futureRows = 150;        // frames
    int smoothingWindow = 0;    // frames (0 = raw)
    int barBgAlpha = 180;
};

static NeuRLcarConfig LoadConfig(CVarManagerWrapper* cvarManager)
{
    NeuRLcarConfig cfg;

    cfg.showTopBars = cvarManager->getCvar("neurlcar_ui_show_topbars").getBoolValue();
    cfg.showMainEval = cvarManager->getCvar("neurlcar_ui_show_maineval").getBoolValue();
    cfg.showMainBackground = cvarManager->getCvar("neurlcar_ui_show_mainbg").getBoolValue();

    cfg.showTitleText = cvarManager->getCvar("neurlcar_ui_show_title").getBoolValue();
    cfg.showHotkeyReminders = cvarManager->getCvar("neurlcar_ui_show_hotkey_reminders").getBoolValue();

    cfg.pastRows = cvarManager->getCvar("neurlcar_ui_past_breadth").getIntValue();
    cfg.futureRows = cvarManager->getCvar("neurlcar_ui_future_breadth").getIntValue();
    cfg.smoothingWindow = cvarManager->getCvar("neurlcar_ui_smoothing_window").getIntValue();

    cfg.pastRows = ClampInt(cfg.pastRows, 0, 5000);
    cfg.futureRows = ClampInt(cfg.futureRows, 0, 5000);
    cfg.smoothingWindow = ClampInt(cfg.smoothingWindow, 0, 5000);

    cfg.mainEvalAlpha = cvarManager->getCvar("neurlcar_ui_maineval_alpha").getIntValue();
    cfg.mainEvalAlpha = ClampInt(cfg.mainEvalAlpha, 0, 255);

    return cfg;
}


// ====================
// Rendering context + elements
// ====================
struct RenderContext
{
    CanvasWrapper* canvas = nullptr;
    NeuRLcarLayout layout{};
    const std::vector<double>* evalSeries = nullptr; // may be null if no analysis
    int currentframe = 0;

    NeuRLcarConfig cfg{};
    float presentEval01 = 0.5f; // smoothed at current frame (only meaningful if evalSeries != nullptr)
};

class ICanvasElement
{
public:
    virtual ~ICanvasElement() = default;
    virtual void Render(const RenderContext& ctx) = 0;
};

// ====================
// Top bars element
// ====================
class TopBarsElement : public ICanvasElement
{
public:
    void Render(const RenderContext& ctx) override
    {
        if (!ctx.cfg.showTopBars || !ctx.evalSeries) return;

        CanvasWrapper& canvas = *ctx.canvas;
        const NeuRLcarLayout& lay = ctx.layout;

        float barW = lay.barSize.X;
        float barH = lay.barSize.Y;

        // Backgrounds
        canvas.SetColor(0, 0, 0, ctx.cfg.barBgAlpha);
        canvas.SetPosition(lay.leftBarPos);
        canvas.FillBox(lay.barSize);

        canvas.SetColor(0, 0, 0, ctx.cfg.barBgAlpha);
        canvas.SetPosition(lay.rightBarPos);
        canvas.FillBox(lay.barSize);

        // If we have an eval, fill proportional to advantage magnitude
        if (ctx.evalSeries)
        {
            float e = ctx.presentEval01;

            // advantage magnitude: 0 at 0.5, 1 at 0 or 1
            float adv = e - 0.5f;
            if (adv < 0.0f) adv = -adv;
            adv = Clamp01(adv * 2.0f);

            bool orangeWins = (e > 0.5f);
            bool blueWins = (e < 0.5f);

            if (blueWins && adv > 0.0f)
            {
                float fillW = barW * adv;
                canvas.SetColor(0, 128, 255, 230);
                float x = lay.leftBarPos.X + (barW - fillW);
                canvas.SetPosition(Vector2(x, lay.leftBarPos.Y));
                canvas.FillBox(Vector2(fillW, barH));
            }

            if (orangeWins && adv > 0.0f)
            {
                float fillW = barW * adv;
                canvas.SetColor(255, 165, 0, 230);
                canvas.SetPosition(lay.rightBarPos);
                canvas.FillBox(Vector2(fillW, barH));
            }
        }

		// 10% eval lines
        canvas.SetColor(UI_GREY_R, UI_GREY_G, UI_GREY_B, GRID_A);
        for (int i = 1; i < 5; ++i)
        {
            int thick = 1;
            int xi = (int)(lay.leftBarPos.X + (barW * i) / 5.0f);
            canvas.SetPosition(Vector2((float)xi, lay.leftBarPos.Y));
            canvas.FillBox(Vector2((float)thick, barH));
        }

        canvas.SetColor(UI_GREY_R, UI_GREY_G, UI_GREY_B, GRID_A);
        for (int i = 1; i < 5; ++i)
        {
            int thick = 1;
            int xi = (int)(lay.rightBarPos.X + (barW * i) / 5.0f);
            canvas.SetPosition(Vector2((float)xi, lay.rightBarPos.Y));
            canvas.FillBox(Vector2((float)thick, barH));
        }



        // Grey outlines
        canvas.SetColor(UI_GREY_R, UI_GREY_G, UI_GREY_B, UI_GREY_A);
        canvas.SetPosition(lay.leftBarPos);
        canvas.DrawBox(lay.barSize);
        canvas.SetPosition(lay.rightBarPos);
        canvas.DrawBox(lay.barSize);
    }
};

// ====================
// Main eval display element
// ====================
class MainEvalDisplayElement : public ICanvasElement
{
public:
    void Render(const RenderContext& ctx) override
    {
        if (!ctx.cfg.showMainEval) return;
        if (!ctx.evalSeries) return;

        CanvasWrapper& canvas = *ctx.canvas;
        const NeuRLcarLayout& lay = ctx.layout;
        const std::vector<double>& evalSeries = *ctx.evalSeries;

        DrawHorizontalEvalGraph(canvas,
            lay,
            evalSeries,
            ctx.currentframe,
            ctx.cfg.smoothingWindow,
            ctx.cfg.showMainBackground,
            ctx.cfg.mainEvalAlpha);

    }
};

class TextOverlayElement : public ICanvasElement
{
public:
    void Render(const RenderContext& ctx) override
    {
        CanvasWrapper& canvas = *ctx.canvas;
        const NeuRLcarLayout& lay = ctx.layout;

        float W = ctx.layout.screenW;
        float H = ctx.layout.screenH;
        int busy = ctx.canvas == nullptr ? 0 : 0;
        (void)W; (void)H; (void)lay; (void)busy;
    }
};


static void DrawCenteredText(CanvasWrapper& canvas,
    const std::string& text,
    float centerX,
    float y,
    int r = 255, int g = 255, int b = 255, int a = 255,
    float xScale = 1.0f, float yScale = 1.0f)
{
    Vector2F sz = canvas.GetStringSize(text, xScale, yScale); // width/height in pixels :contentReference[oaicite:1]{index=1}
    canvas.SetColor(r, g, b, a);
    canvas.SetPosition(Vector2F(centerX - (sz.X * 0.5f), y));
    canvas.DrawString(text, xScale, yScale);
}

static std::shared_ptr<ImageWrapper> GetScoreboardWrapperImage(GameWrapper* gw)
{
    static std::shared_ptr<ImageWrapper> img;
    static bool attempted = false;

    if (!img && !attempted)
    {
        attempted = true;

        std::filesystem::path p =
            gw->GetBakkesModPath() / "data" / "neurlcar" / "scoreboard_wrapper.png";
        img = std::make_shared<ImageWrapper>(p.string());

        if (img)
        {
            img->LoadForCanvas();
            if (!img->IsLoadedForCanvas())
                LOG("scoreboard_wrapper.png: LoadForCanvas failed ({})", p.string());
        }
        else
        {
            LOG("scoreboard_wrapper.png: failed to create ImageWrapper ({})", p.string());
        }
    }

    return img;
}

static void DrawScoreboardWrapperPng(CanvasWrapper& canvas, GameWrapper* gw)
{
    auto img = GetScoreboardWrapperImage(gw);
    if (!img || !img->IsLoadedForCanvas())
        return;

    Vector2 screen = canvas.GetSize();
    Vector2 imgSz = img->GetSize();
    if (imgSz.X <= 0.0f || imgSz.Y <= 0.0f)
        return;


    const float targetWFrac = 0.205f;      
    const float maxHFrac = 0.40f;

    const float yUpFrac = 0.005f;


    float targetW = screen.X * targetWFrac;
    float maxH = screen.Y * maxHFrac;

    float scale = targetW / imgSz.X;
    if (imgSz.Y * scale > maxH)
        scale = maxH / imgSz.Y;

    float drawW = imgSz.X * scale;

    float x = (screen.X - drawW) * 0.5f;
    float y = -(screen.Y * yUpFrac);

    canvas.SetPosition(Vector2(x, y));
    canvas.DrawTexture(img.get(), scale);
}



// ====================
// Canvas entrypoint
// ====================
void neuRLcar::RenderCanvas(CanvasWrapper canvas)
{
    if (!cvarManager->getCvar("neurlcar_ui_enabled").getBoolValue())
        return;
    if (cvarManager->getCvar("neurlcar_ui_debug_grid").getBoolValue())
        RenderDebugGrid(canvas);

    if (!isinreplay())
        return;

    NeuRLcarConfig cfg = LoadConfig(cvarManager.get());
    NeuRLcarLayout lay = ComputeLayout(canvas.GetSize());
    DrawScoreboardWrapperPng(canvas, gameWrapper.get());

    // Live busy flag (RAW cvar, every frame)
    int analysisBusy = cvarManager->getCvar("neurlcar_analysis_busy").getIntValue();

    // Always get current frame
    int currentframe = cvarManager->getCvar("currentframe").getIntValue();

    // Determine if we have analysis loaded
    bool hasAnalysis = replaydataloaded();
    const std::vector<double>* evalSeriesPtr = nullptr;
    float presentEval01 = 0.5f;

    if (hasAnalysis)
    {
        auto& loadedData = getloadedData();
        if (!loadedData.empty() && !loadedData[0].empty())
        {
            evalSeriesPtr = &loadedData[0];

            // clamp currentframe
            int n = (int)evalSeriesPtr->size();
            if (currentframe < 0) currentframe = 0;
            if (currentframe >= n) currentframe = n - 1;

            presentEval01 = SmoothedEvalAt(*evalSeriesPtr, currentframe, cfg.smoothingWindow);
        }
        else
        {
            hasAnalysis = false;
        }
    }

    RenderContext ctx;
    ctx.canvas = &canvas;
    ctx.layout = lay;
    ctx.evalSeries = evalSeriesPtr;
    ctx.currentframe = currentframe;
    ctx.cfg = cfg;
    ctx.presentEval01 = presentEval01;

    // Elements (class instances)
    static std::vector<std::unique_ptr<ICanvasElement>> elements;
    static bool initialized = false;
    if (!initialized)
    {
        elements.clear();
        elements.push_back(std::make_unique<TopBarsElement>());
        elements.push_back(std::make_unique<MainEvalDisplayElement>());
        initialized = true;
    }

    for (auto& el : elements)
        if (el) el->Render(ctx);

    if (cfg.showTitleText)
    {
        float xCenter = lay.screenW * 0.5f;
        float y = lay.screenH * 0.00f;
        DrawCenteredText(canvas, "neuRLcar :D", xCenter, y, 255, 255, 255, 255);
    }




    // Hotkey reminders
    if (cfg.showHotkeyReminders)
    {
        float xCenter = lay.screenW * 0.5f;

        // Read keys from CVars (fallback to X/Z if missing)
        std::string keyAnalysis = "X";
        std::string keySettings = "Z";

        {
            auto cA = cvarManager->getCvar("analysis_keybind");
            if (!cA.IsNull())
            {
                std::string v = cA.getStringValue();
                if (!v.empty()) keyAnalysis = v;
            }

            auto cS = cvarManager->getCvar("plugin_settings_keybind");
            if (!cS.IsNull())
            {
                std::string v = cS.getStringValue();
                if (!v.empty()) keySettings = v;
            }
        }

        if (!hasAnalysis)
        {
            float yTop = lay.screenH * 0.10f;

            std::string line1 =
                (analysisBusy == 1)
                ? "analyzing..."
                : ("press " + keyAnalysis + " to analyze replay");

            DrawCenteredText(canvas, line1, xCenter, yTop, 255, 255, 255, 255);

            float y2 = lay.screenH * 0.09f;
            DrawCenteredText(canvas, "press " + keySettings + " to toggle settings window", xCenter, y2, 255, 255, 255, 255);
        }
        else
        {
            float y = lay.screenH * (cfg.showMainEval ? 0.24f : 0.09f);
            DrawCenteredText(canvas, "press " + keySettings + " to toggle settings window", xCenter, y, 255, 255, 255, 255);
        }
    }


}
