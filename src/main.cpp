#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <math.h>

// ============================================================
// ForgeUI MicroScope
// ESP32-S3 + ST7789 240x240 + Analog Joystick
//
// V1 simulated instrumentation:
//   SCOPE -> SPECTRUM -> XY / LISSAJOUS
//
// Joystick:
//   X  GPIO6
//   Y  GPIO5
//   SW GPIO4
//
// Display:
//   CS  GPIO8
//   DC  GPIO9
//   RST GPIO10
//   MOSI GPIO11
//   SCLK GPIO12
// ============================================================

// ---------------- Display -----------------------------------

#define TFT_SCLK 12
#define TFT_MOSI 11
#define TFT_DC    9
#define TFT_RST   10
#define TFT_CS    8

#define SCREEN_W 240
#define SCREEN_H 240

Arduino_DataBus *bus = new Arduino_ESP32SPI(
    TFT_DC,
    TFT_CS,
    TFT_SCLK,
    TFT_MOSI,
    GFX_NOT_DEFINED,
    HSPI
);

Arduino_GFX *gfx = new Arduino_ST7789(
    bus,
    TFT_RST,
    0,
    true,
    SCREEN_W,
    SCREEN_H
);

Arduino_Canvas *canvas = nullptr;

// ---------------- Joystick ----------------------------------

constexpr int JOY_X  = 6;
constexpr int JOY_Y  = 5;
constexpr int JOY_SW = 4;

int joyCentreX = 2048;
int joyCentreY = 2048;

// ---------------- Colours -----------------------------------

constexpr uint16_t C_BLACK   = 0x0000;
constexpr uint16_t C_WHITE   = 0xFFFF;
constexpr uint16_t C_CYAN    = 0x07FF;
constexpr uint16_t C_BLUE    = 0x001F;
constexpr uint16_t C_GREEN   = 0x07E0;
constexpr uint16_t C_YELLOW  = 0xFFE0;
constexpr uint16_t C_RED     = 0xF800;
constexpr uint16_t C_MAGENTA = 0xF81F;
constexpr uint16_t C_GREY    = 0x8410;
constexpr uint16_t C_DKGREY  = 0x3186;

// Instrument-specific shades
constexpr uint16_t GRID_MAJOR = 0x4208;
constexpr uint16_t GRID_MINOR = 0x2104;
constexpr uint16_t TRACE_DIM  = 0x03E0;

// ---------------- Instrument modes --------------------------

enum InstrumentMode
{
    MODE_SCOPE = 0,
    MODE_SPECTRUM = 1,
    MODE_XY = 2
};

InstrumentMode mode = MODE_SCOPE;

// ---------------- Waveform type -----------------------------

enum WaveType
{
    WAVE_SINE = 0,
    WAVE_SQUARE = 1,
    WAVE_TRIANGLE = 2,
    WAVE_NOISE = 3
};

WaveType waveType = WAVE_SINE;

// ---------------- Runtime -----------------------------------

unsigned long lastFrame = 0;
unsigned long lastButtonTime = 0;

float animationPhase = 0.0f;

// Scope settings
float voltsPerDiv = 1.0f;
float timePerDivMs = 1.0f;
float triggerLevel = 0.0f;

// Simulated signal
float signalFrequency = 1000.0f;
float signalAmplitude = 1.25f;
float signalOffset = 0.0f;

// Spectrum
constexpr int SPECTRUM_BINS = 32;
float spectrum[SPECTRUM_BINS];
float spectrumPeak[SPECTRUM_BINS];

// XY
float xyPhaseShift = 1.5708f;
float xyRatio = 1.0f;

// ============================================================
// Helpers
// ============================================================

float clampFloat(
    float value,
    float minimum,
    float maximum)
{
    if (value < minimum)
        return minimum;

    if (value > maximum)
        return maximum;

    return value;
}

bool buttonPressed()
{
    return digitalRead(JOY_SW) == LOW;
}

float readAxis(
    int raw,
    int centre)
{
    constexpr int deadZone = 180;

    int delta =
        raw - centre;

    if (abs(delta) < deadZone)
        return 0.0f;

    float value = 0.0f;

    if (delta > 0)
    {
        int range =
            4095 -
            centre -
            deadZone;

        if (range > 0)
        {
            value =
                (float)(
                    delta -
                    deadZone
                ) /
                (float)range;
        }
    }
    else
    {
        int range =
            centre -
            deadZone;

        if (range > 0)
        {
            value =
                (float)(
                    delta +
                    deadZone
                ) /
                (float)range;
        }
    }

    return clampFloat(
        value,
        -1.0f,
        1.0f
    );
}

const char *modeName()
{
    switch (mode)
    {
        case MODE_SCOPE:
            return "SCOPE";

        case MODE_SPECTRUM:
            return "SPECTRUM";

        case MODE_XY:
            return "XY";

        default:
            return "?";
    }
}

const char *waveName()
{
    switch (waveType)
    {
        case WAVE_SINE:
            return "SINE";

        case WAVE_SQUARE:
            return "SQUARE";

        case WAVE_TRIANGLE:
            return "TRIANGLE";

        case WAVE_NOISE:
            return "NOISE";

        default:
            return "?";
    }
}

// ============================================================
// Text helpers
// ============================================================

void centredText(
    const char *text,
    int y,
    int size,
    uint16_t colour)
{
    canvas->setTextSize(size);
    canvas->setTextColor(colour);

    int16_t x1;
    int16_t y1;
    uint16_t w;
    uint16_t h;

    canvas->getTextBounds(
        text,
        0,
        0,
        &x1,
        &y1,
        &w,
        &h
    );

    canvas->setCursor(
        (SCREEN_W - w) / 2,
        y
    );

    canvas->print(text);
}

// ============================================================
// Joystick calibration
// ============================================================

void calibrateJoystick()
{
    canvas->fillScreen(C_BLACK);

    centredText(
        "FORGEUI",
        54,
        3,
        C_CYAN
    );

    centredText(
        "MICROSCOPE",
        88,
        2,
        C_WHITE
    );

    centredText(
        "INSTRUMENT CONTROL",
        122,
        1,
        C_GREY
    );

    centredText(
        "CALIBRATING...",
        150,
        1,
        C_GREEN
    );

    centredText(
        "RELEASE STICK",
        174,
        1,
        C_YELLOW
    );

    canvas->flush();

    long totalX = 0;
    long totalY = 0;

    constexpr int samples = 64;

    for (int i = 0; i < samples; i++)
    {
        totalX +=
            analogRead(JOY_X);

        totalY +=
            analogRead(JOY_Y);

        delay(5);
    }

    joyCentreX =
        totalX / samples;

    joyCentreY =
        totalY / samples;

    Serial.printf(
        "Joystick centre X=%d Y=%d\n",
        joyCentreX,
        joyCentreY
    );
}

// ============================================================
// Simulated signal generator
// ============================================================

float triangleWave(float phase)
{
    float wrapped =
        fmodf(
            phase,
            2.0f * PI
        );

    if (wrapped < 0)
        wrapped +=
            2.0f * PI;

    float normalized =
        wrapped /
        (2.0f * PI);

    if (normalized < 0.25f)
        return normalized * 4.0f;

    if (normalized < 0.75f)
        return 2.0f -
               normalized * 4.0f;

    return normalized * 4.0f -
           4.0f;
}

float sampleSignal(float phase)
{
    float value = 0.0f;

    switch (waveType)
    {
        case WAVE_SINE:
        {
            value =
                sinf(phase);

            break;
        }

        case WAVE_SQUARE:
        {
            value =
                sinf(phase) >= 0.0f
                    ? 1.0f
                    : -1.0f;

            break;
        }

        case WAVE_TRIANGLE:
        {
            value =
                triangleWave(phase);

            break;
        }

        case WAVE_NOISE:
        {
            float sineBase =
                sinf(phase) * 0.65f;

            float noise =
                random(-100, 101) /
                100.0f;

            value =
                sineBase +
                noise * 0.35f;

            break;
        }
    }

    return
        signalOffset +
        value *
        signalAmplitude;
}

// ============================================================
// Shared header
// ============================================================

void drawHeader()
{
    canvas->fillRect(
        0,
        0,
        SCREEN_W,
        24,
        C_BLACK
    );

    canvas->drawFastHLine(
        0,
        23,
        SCREEN_W,
        C_CYAN
    );

    canvas->setTextSize(1);

    canvas->setTextColor(C_CYAN);
    canvas->setCursor(4, 5);
    canvas->print("FORGEUI");

    canvas->setTextColor(C_WHITE);
    canvas->setCursor(58, 5);
    canvas->print("MICROSCOPE");

    canvas->setTextColor(C_GREEN);
    canvas->setCursor(154, 5);
    canvas->print(modeName());
}

// ============================================================
// Shared footer
// ============================================================

void drawFooter()
{
    canvas->fillRect(
        0,
        218,
        SCREEN_W,
        22,
        C_BLACK
    );

    canvas->drawFastHLine(
        0,
        218,
        SCREEN_W,
        C_DKGREY
    );

    canvas->setTextSize(1);

    if (mode == MODE_SCOPE)
    {
        canvas->setTextColor(C_CYAN);
        canvas->setCursor(4, 224);
        canvas->printf(
            "%.1fV/D",
            voltsPerDiv
        );

        canvas->setTextColor(C_YELLOW);
        canvas->setCursor(76, 224);
        canvas->printf(
            "%.1fms/D",
            timePerDivMs
        );

        canvas->setTextColor(C_GREEN);
        canvas->setCursor(166, 224);
        canvas->print(waveName());
    }
    else if (mode == MODE_SPECTRUM)
    {
        canvas->setTextColor(C_CYAN);
        canvas->setCursor(4, 224);
        canvas->print("0");

        canvas->setTextColor(C_GREY);
        canvas->setCursor(98, 224);
        canvas->print("5kHz");

        canvas->setTextColor(C_CYAN);
        canvas->setCursor(202, 224);
        canvas->print("10k");
    }
    else
    {
        canvas->setTextColor(C_CYAN);
        canvas->setCursor(4, 224);
        canvas->printf(
            "RATIO %.1f",
            xyRatio
        );

        canvas->setTextColor(C_MAGENTA);
        canvas->setCursor(128, 224);
        canvas->printf(
            "PH %.0f",
            xyPhaseShift *
                57.2958f
        );
    }
}

// ============================================================
// Instrument graticule
// ============================================================

void drawGraticule(
    int left,
    int top,
    int right,
    int bottom)
{
    canvas->fillRect(
        left,
        top,
        right - left + 1,
        bottom - top + 1,
        C_BLACK
    );

    canvas->drawRect(
        left,
        top,
        right - left + 1,
        bottom - top + 1,
        C_GREY
    );

    int width =
        right - left;

    int height =
        bottom - top;

    // 10 horizontal divisions.
    for (int i = 1; i < 10; i++)
    {
        int x =
            left +
            width * i / 10;

        uint16_t colour =
            i == 5
                ? GRID_MAJOR
                : GRID_MINOR;

        canvas->drawFastVLine(
            x,
            top + 1,
            height - 1,
            colour
        );
    }

    // 8 vertical divisions.
    for (int i = 1; i < 8; i++)
    {
        int y =
            top +
            height * i / 8;

        uint16_t colour =
            i == 4
                ? GRID_MAJOR
                : GRID_MINOR;

        canvas->drawFastHLine(
            left + 1,
            y,
            width - 1,
            colour
        );
    }

    // Centre cross ticks.
    int centreX =
        (left + right) / 2;

    int centreY =
        (top + bottom) / 2;

    for (int y = top + 6;
         y < bottom;
         y += 8)
    {
        canvas->drawPixel(
            centreX,
            y,
            C_GREY
        );
    }

    for (int x = left + 6;
         x < right;
         x += 8)
    {
        canvas->drawPixel(
            x,
            centreY,
            C_GREY
        );
    }
}

// ============================================================
// Joystick-controlled instrument settings
// ============================================================

void updateControls()
{
    float x =
        readAxis(
            analogRead(JOY_X),
            joyCentreX
        );

    float y =
        readAxis(
            analogRead(JOY_Y),
            joyCentreY
        );

    if (mode == MODE_SCOPE)
    {
        // Horizontal stick adjusts timebase.
        if (x > 0.65f)
            timePerDivMs = 2.0f;
        else if (x > 0.20f)
            timePerDivMs = 1.0f;
        else if (x < -0.65f)
            timePerDivMs = 0.25f;
        else if (x < -0.20f)
            timePerDivMs = 0.5f;

        // Vertical stick adjusts volts/div.
        if (y > 0.65f)
            voltsPerDiv = 2.0f;
        else if (y > 0.20f)
            voltsPerDiv = 1.0f;
        else if (y < -0.65f)
            voltsPerDiv = 0.25f;
        else if (y < -0.20f)
            voltsPerDiv = 0.5f;
    }
    else if (mode == MODE_SPECTRUM)
    {
        // Spectrum mode uses joystick to alter simulated source.
        signalFrequency +=
            x * 35.0f;

        signalFrequency =
            clampFloat(
                signalFrequency,
                250.0f,
                8000.0f
            );

        signalAmplitude +=
            -y * 0.015f;

        signalAmplitude =
            clampFloat(
                signalAmplitude,
                0.35f,
                2.0f
            );
    }
    else
    {
        // XY mode:
        // X adjusts phase, Y adjusts frequency ratio.
        xyPhaseShift +=
            x * 0.025f;

        if (xyPhaseShift >
            2.0f * PI)
        {
            xyPhaseShift -=
                2.0f * PI;
        }

        if (xyPhaseShift < 0)
        {
            xyPhaseShift +=
                2.0f * PI;
        }

        xyRatio +=
            -y * 0.006f;

        xyRatio =
            clampFloat(
                xyRatio,
                0.5f,
                2.0f
            );
    }
}

// ============================================================
// Mode button
// ============================================================

void updateModeButton()
{
    static bool previousButton = false;

    bool currentButton =
        buttonPressed();

    bool edge =
        currentButton &&
        !previousButton;

    previousButton =
        currentButton;

    if (!edge)
        return;

    if (millis() -
            lastButtonTime <
        250)
    {
        return;
    }

    lastButtonTime =
        millis();

    int next =
        ((int)mode + 1) % 3;

    mode =
        (InstrumentMode)next;

    Serial.printf(
        "MicroScope mode: %s\n",
        modeName()
    );
}
// ============================================================
// Scope waveform selection
// ============================================================

void cycleWaveform()
{
    int next =
        ((int)waveType + 1) % 4;

    waveType =
        (WaveType)next;
}

// ============================================================
// Scope measurements
// ============================================================

void drawScopeMeasurements()
{
    // For V1 these measurements correspond to the simulated source.
    float vpp =
        signalAmplitude * 2.0f;

    float rms = 0.0f;

    if (waveType == WAVE_SINE)
    {
        rms =
            signalAmplitude *
            0.7071f;
    }
    else if (waveType == WAVE_SQUARE)
    {
        rms =
            signalAmplitude;
    }
    else if (waveType == WAVE_TRIANGLE)
    {
        rms =
            signalAmplitude *
            0.5774f;
    }
    else
    {
        // Approximate RMS for the noisy simulated source.
        rms =
            signalAmplitude *
            0.62f;
    }

    canvas->fillRect(
        0,
        24,
        SCREEN_W,
        20,
        C_BLACK
    );

    canvas->setTextSize(1);

    canvas->setTextColor(C_GREEN);
    canvas->setCursor(4, 29);
    canvas->printf(
        "F %.0fHz",
        signalFrequency
    );

    canvas->setTextColor(C_CYAN);
    canvas->setCursor(78, 29);
    canvas->printf(
        "VPP %.2f",
        vpp
    );

    canvas->setTextColor(C_YELLOW);
    canvas->setCursor(164, 29);
    canvas->printf(
        "RMS %.2f",
        rms
    );
}

// ============================================================
// Scope trigger marker
// ============================================================

void drawTriggerMarker(
    int plotTop,
    int plotBottom)
{
    int centreY =
        (plotTop + plotBottom) / 2;

    float pixelsPerVolt =
        20.0f /
        voltsPerDiv;

    int triggerY =
        centreY -
        (int)(
            triggerLevel *
            pixelsPerVolt
        );

    triggerY =
        constrain(
            triggerY,
            plotTop + 2,
            plotBottom - 2
        );

    // Trigger level marker on right edge.
    canvas->fillTriangle(
        239,
        triggerY,
        232,
        triggerY - 4,
        232,
        triggerY + 4,
        C_YELLOW
    );

    canvas->setTextSize(1);
    canvas->setTextColor(C_YELLOW);
    canvas->setCursor(
        214,
        triggerY - 10
    );
    canvas->print("T");
}

// ============================================================
// Scope trace
// ============================================================

void drawScopeTrace()
{
    constexpr int plotLeft = 0;
    constexpr int plotTop = 44;
    constexpr int plotRight = 239;
    constexpr int plotBottom = 217;

    drawGraticule(
        plotLeft,
        plotTop,
        plotRight,
        plotBottom
    );

    int centreY =
        (plotTop + plotBottom) / 2;

    float pixelsPerVolt =
        20.0f /
        voltsPerDiv;

    // More time/div means more cycles visible.
    float cyclesAcrossScreen =
        1.1f *
        timePerDivMs;

    cyclesAcrossScreen =
        clampFloat(
            cyclesAcrossScreen,
            0.35f,
            4.5f
        );

    int previousX = plotLeft;
    int previousY = centreY;

    for (int x = plotLeft;
         x <= plotRight;
         x++)
    {
        float normalizedX =
            (float)(x - plotLeft) /
            (float)(plotRight - plotLeft);

        float phase =
            animationPhase +
            normalizedX *
            cyclesAcrossScreen *
            2.0f *
            PI;

        float voltage =
            sampleSignal(phase);

        int y =
            centreY -
            (int)(
                voltage *
                pixelsPerVolt
            );

        y =
            constrain(
                y,
                plotTop + 2,
                plotBottom - 2
            );

        if (x > plotLeft)
        {
            // Dim under-trace gives a tiny persistence/glow effect.
            canvas->drawLine(
                previousX,
                previousY + 1,
                x,
                y + 1,
                TRACE_DIM
            );

            canvas->drawLine(
                previousX,
                previousY,
                x,
                y,
                C_GREEN
            );
        }

        previousX = x;
        previousY = y;
    }

    drawTriggerMarker(
        plotTop,
        plotBottom
    );

    // Trigger position marker at upper edge.
    canvas->fillTriangle(
        120,
        plotTop + 1,
        116,
        plotTop + 7,
        124,
        plotTop + 7,
        C_YELLOW
    );
}

// ============================================================
// Scope control indicator
// ============================================================

void drawScopeControlHints()
{
    // Tiny vertical scale arrows.
    canvas->setTextSize(1);
    canvas->setTextColor(C_GREY);

    canvas->setCursor(3, 205);
    canvas->print("Y:V/D");

    canvas->setCursor(190, 205);
    canvas->print("X:T/D");
}

// ============================================================
// Complete scope mode
// ============================================================

void drawScopeMode()
{
    canvas->fillScreen(C_BLACK);

    drawHeader();
    drawScopeMeasurements();
    drawScopeTrace();
    drawScopeControlHints();
    drawFooter();

    canvas->flush();
}

// ============================================================
// Spectrum generator
// ============================================================

void updateSpectrumData()
{
    // Dominant simulated frequency mapped into 32 bins over 0-10 kHz.
    float dominantBin =
        signalFrequency /
        10000.0f *
        (SPECTRUM_BINS - 1);

    for (int i = 0;
         i < SPECTRUM_BINS;
         i++)
    {
        float distance =
            fabsf(
                i -
                dominantBin
            );

        float fundamental =
            expf(
                -distance *
                distance *
                0.72f
            );

        // Harmonics make square/triangle modes visually distinct.
        float harmonic2 = 0.0f;
        float harmonic3 = 0.0f;

        float bin2 =
            dominantBin * 2.0f;

        float bin3 =
            dominantBin * 3.0f;

        if (bin2 <
            SPECTRUM_BINS)
        {
            float d =
                fabsf(
                    i -
                    bin2
                );

            harmonic2 =
                expf(
                    -d * d *
                    0.85f
                );
        }

        if (bin3 <
            SPECTRUM_BINS)
        {
            float d =
                fabsf(
                    i -
                    bin3
                );

            harmonic3 =
                expf(
                    -d * d *
                    0.85f
                );
        }

        float value =
            fundamental *
            signalAmplitude;

        if (waveType ==
            WAVE_SQUARE)
        {
            value +=
                harmonic3 *
                signalAmplitude *
                0.34f;
        }
        else if (waveType ==
                 WAVE_TRIANGLE)
        {
            value +=
                harmonic3 *
                signalAmplitude *
                0.12f;
        }
        else if (waveType ==
                 WAVE_NOISE)
        {
            value +=
                random(5, 35) /
                100.0f;
        }
        else
        {
            value +=
                harmonic2 *
                signalAmplitude *
                0.06f;
        }

        // Slight animated noise floor.
        value +=
            random(0, 10) /
            100.0f;

        spectrum[i] =
            clampFloat(
                value,
                0.0f,
                2.2f
            );

        // Peak hold decays slowly.
        if (spectrum[i] >
            spectrumPeak[i])
        {
            spectrumPeak[i] =
                spectrum[i];
        }
        else
        {
            spectrumPeak[i] -=
                0.012f;

            if (spectrumPeak[i] < 0)
                spectrumPeak[i] = 0;
        }
    }
}

// ============================================================
// Spectrum measurements
// ============================================================

void drawSpectrumMeasurements()
{
    canvas->fillRect(
        0,
        24,
        SCREEN_W,
        20,
        C_BLACK
    );

    canvas->setTextSize(1);

    canvas->setTextColor(C_GREEN);
    canvas->setCursor(4, 29);
    canvas->printf(
        "PEAK %.0fHz",
        signalFrequency
    );

    canvas->setTextColor(C_CYAN);
    canvas->setCursor(106, 29);
    canvas->printf(
        "AMP %.2fV",
        signalAmplitude
    );

    canvas->setTextColor(C_YELLOW);
    canvas->setCursor(190, 29);
    canvas->print("HOLD");
}

// ============================================================
// Spectrum renderer
// ============================================================

void drawSpectrumMode()
{
    constexpr int plotLeft = 4;
    constexpr int plotTop = 44;
    constexpr int plotRight = 235;
    constexpr int plotBottom = 217;

    canvas->fillScreen(C_BLACK);

    drawHeader();
    drawSpectrumMeasurements();

    drawGraticule(
        plotLeft,
        plotTop,
        plotRight,
        plotBottom
    );

    updateSpectrumData();

    int plotHeight =
        plotBottom -
        plotTop -
        4;

    int availableWidth =
        plotRight -
        plotLeft -
        4;

    int barWidth =
        max(
            2,
            availableWidth /
            SPECTRUM_BINS
        );

    int dominantIndex = 0;
    float dominantValue = 0.0f;

    for (int i = 0;
         i < SPECTRUM_BINS;
         i++)
    {
        if (spectrum[i] >
            dominantValue)
        {
            dominantValue =
                spectrum[i];

            dominantIndex = i;
        }

        float normalized =
            spectrum[i] /
            2.2f;

        normalized =
            clampFloat(
                normalized,
                0.0f,
                1.0f
            );

        int barHeight =
            (int)(
                normalized *
                plotHeight
            );

        int x =
            plotLeft +
            2 +
            i *
            availableWidth /
            SPECTRUM_BINS;

        int y =
            plotBottom -
            2 -
            barHeight;

        uint16_t colour;

        if (normalized > 0.72f)
            colour = C_YELLOW;
        else if (normalized > 0.38f)
            colour = C_CYAN;
        else
            colour = C_GREEN;

        canvas->fillRect(
            x,
            y,
            barWidth,
            barHeight,
            colour
        );

        // Peak-hold marker.
        float peakNormalized =
            spectrumPeak[i] /
            2.2f;

        peakNormalized =
            clampFloat(
                peakNormalized,
                0.0f,
                1.0f
            );

        int peakY =
            plotBottom -
            2 -
            (int)(
                peakNormalized *
                plotHeight
            );

        canvas->drawFastHLine(
            x,
            peakY,
            barWidth,
            C_WHITE
        );
    }

    // Dominant-frequency cursor.
    int dominantX =
        plotLeft +
        2 +
        dominantIndex *
        availableWidth /
        SPECTRUM_BINS;

    canvas->drawFastVLine(
        dominantX,
        plotTop + 2,
        8,
        C_MAGENTA
    );

    canvas->fillTriangle(
        dominantX,
        plotTop + 11,
        dominantX - 4,
        plotTop + 5,
        dominantX + 4,
        plotTop + 5,
        C_MAGENTA
    );

    drawFooter();

    canvas->flush();
}

// ============================================================
// XY / Lissajous renderer
// ============================================================

void drawXYMode()
{
    constexpr int plotLeft = 10;
    constexpr int plotTop = 34;
    constexpr int plotRight = 229;
    constexpr int plotBottom = 217;

    canvas->fillScreen(C_BLACK);

    drawHeader();

    drawGraticule(
        plotLeft,
        plotTop,
        plotRight,
        plotBottom
    );

    int centreX =
        (plotLeft +
         plotRight) / 2;

    int centreY =
        (plotTop +
         plotBottom) / 2;

    float radiusX =
        (plotRight -
         plotLeft) *
        0.42f;

    float radiusY =
        (plotBottom -
         plotTop) *
        0.42f;

    int previousX = centreX;
    int previousY = centreY;

    constexpr int points = 260;

    for (int i = 0;
         i < points;
         i++)
    {
        float t =
            animationPhase +
            i *
            (2.0f * PI /
             points) *
            2.0f;

        float sx =
            sinf(t);

        float sy =
            sinf(
                t *
                xyRatio +
                xyPhaseShift
            );

        int x =
            centreX +
            (int)(
                sx *
                radiusX
            );

        int y =
            centreY +
            (int)(
                sy *
                radiusY
            );

        if (i > 0)
        {
            // Dim companion trace.
            canvas->drawLine(
                previousX + 1,
                previousY,
                x + 1,
                y,
                TRACE_DIM
            );

            canvas->drawLine(
                previousX,
                previousY,
                x,
                y,
                C_CYAN
            );
        }

        previousX = x;
        previousY = y;
    }

    // Centre reference.
    canvas->drawCircle(
        centreX,
        centreY,
        3,
        C_YELLOW
    );

    canvas->drawPixel(
        centreX,
        centreY,
        C_WHITE
    );

    // XY mode labels.
    canvas->setTextSize(1);

    canvas->setTextColor(C_GREEN);
    canvas->setCursor(15, 40);
    canvas->print("CH1 X");

    canvas->setTextColor(C_MAGENTA);
    canvas->setCursor(190, 40);
    canvas->print("Y CH2");

    drawFooter();

    canvas->flush();
}
// ============================================================
// Startup / self-test
// ============================================================

void drawStartupScreen()
{
    canvas->fillScreen(C_BLACK);

    canvas->drawRect(
        8,
        8,
        224,
        224,
        C_CYAN
    );

    canvas->drawRect(
        12,
        12,
        216,
        216,
        C_BLUE
    );

    centredText(
        "FORGEUI",
        50,
        3,
        C_CYAN
    );

    centredText(
        "MICROSCOPE",
        86,
        2,
        C_WHITE
    );

    centredText(
        "DIGITAL INSTRUMENT",
        120,
        1,
        C_GREY
    );

    centredText(
        "SCOPE // FFT // XY",
        144,
        1,
        C_WHITE
    );

    centredText(
        "SYSTEM SELF TEST",
        174,
        1,
        C_YELLOW
    );

    centredText(
        "INSTRUMENT READY",
        198,
        1,
        C_GREEN
    );

    canvas->flush();
}

// ============================================================
// Waveform selector
//
// In SCOPE mode a long-ish joystick-button hold changes
// waveform. A normal press cycles instrument mode.
// ============================================================

void handleButton()
{
    static bool previousButton = false;
    static unsigned long pressStart = 0;

    bool currentButton =
        buttonPressed();

    // New press.
    if (currentButton &&
        !previousButton)
    {
        pressStart = millis();
    }

    // Release.
    if (!currentButton &&
        previousButton)
    {
        unsigned long held =
            millis() -
            pressStart;

        if (millis() -
                lastButtonTime >=
            250)
        {
            lastButtonTime =
                millis();

            // Hold >= 700 ms in scope mode:
            // change generated waveform.
            if (mode == MODE_SCOPE &&
                held >= 700)
            {
                cycleWaveform();

                Serial.printf(
                    "Waveform: %s\n",
                    waveName()
                );
            }
            else
            {
                int next =
                    ((int)mode + 1) %
                    3;

                mode =
                    (InstrumentMode)next;

                Serial.printf(
                    "MicroScope mode: %s\n",
                    modeName()
                );
            }
        }
    }

    previousButton =
        currentButton;
}

// ============================================================
// Scope animated trigger / signal behaviour
// ============================================================

void updateSimulatedInstrument()
{
    // Advance animation.
    //
    // Scope intentionally moves more slowly than the actual
    // simulated signal frequency so the waveform remains
    // visually readable rather than becoming a blur.
    animationPhase += 0.075f;

    if (animationPhase >
        2.0f * PI)
    {
        animationPhase -=
            2.0f * PI;
    }

    // Slowly animate trigger level for visual instrumentation.
    if (mode == MODE_SCOPE)
    {
        triggerLevel =
            sinf(
                millis() *
                0.0007f
            ) *
            0.30f;
    }
}

// ============================================================
// Small mode overlay
// ============================================================

void drawModeHint()
{
    canvas->fillRect(
        74,
        205,
        92,
        11,
        C_BLACK
    );

    canvas->setTextSize(1);
    canvas->setTextColor(C_GREY);

    canvas->setCursor(
        79,
        207
    );

    if (mode == MODE_SCOPE)
    {
        canvas->print(
            "HOLD: WAVE"
        );
    }
    else
    {
        canvas->print(
            "PRESS: MODE"
        );
    }
}

// ============================================================
// Render dispatcher
// ============================================================

void renderInstrument()
{
    switch (mode)
    {
        case MODE_SCOPE:
        {
            drawScopeMode();
            break;
        }

        case MODE_SPECTRUM:
        {
            drawSpectrumMode();
            break;
        }

        case MODE_XY:
        {
            drawXYMode();
            break;
        }
    }
}

// ============================================================
// Setup
// ============================================================

void setup()
{
    Serial.begin(115200);
    delay(300);

    Serial.println();
    Serial.println(
        "=============================="
    );
    Serial.println(
        "FORGEUI MICROSCOPE"
    );
    Serial.println(
        "ESP32-S3 + ST7789 240x240"
    );
    Serial.println(
        "SCOPE // SPECTRUM // XY"
    );
    Serial.println(
        "JOY X=6 Y=5 SW=4"
    );
    Serial.println(
        "=============================="
    );

    pinMode(
        JOY_SW,
        INPUT_PULLUP
    );

    analogReadResolution(12);

    // Physically proven ST7789 square-display baseline.
    if (!gfx->begin())
    {
        Serial.println(
            "DISPLAY INIT FAILED"
        );

        while (true)
            delay(1000);
    }

    // Full-resolution off-screen canvas.
    canvas =
        new Arduino_Canvas(
            SCREEN_W,
            SCREEN_H,
            gfx
        );

    if (canvas == nullptr ||
        !canvas->begin())
    {
        Serial.println(
            "CANVAS INIT FAILED"
        );

        while (true)
            delay(1000);
    }

    randomSeed(
        analogRead(JOY_X) ^
        analogRead(JOY_Y) ^
        micros()
    );

    // Initialise spectrum state.
    for (int i = 0;
         i < SPECTRUM_BINS;
         i++)
    {
        spectrum[i] = 0.0f;
        spectrumPeak[i] = 0.0f;
    }

    drawStartupScreen();

    delay(1500);

    calibrateJoystick();

    delay(400);

    mode = MODE_SCOPE;
    waveType = WAVE_SINE;

    voltsPerDiv = 1.0f;
    timePerDivMs = 1.0f;
    triggerLevel = 0.0f;

    signalFrequency = 1000.0f;
    signalAmplitude = 1.25f;
    signalOffset = 0.0f;

    xyPhaseShift = PI / 2.0f;
    xyRatio = 1.0f;

    Serial.println(
        "MICROSCOPE READY"
    );

    // Prevent held switch during calibration
    // becoming an immediate mode change.
    while (buttonPressed())
        delay(10);
}

// ============================================================
// Main loop
// ============================================================

void loop()
{
    // Approximately 30 FPS.
    if (millis() -
            lastFrame <
        33)
    {
        return;
    }

    lastFrame =
        millis();

    handleButton();

    updateControls();

    updateSimulatedInstrument();

    renderInstrument();
}
