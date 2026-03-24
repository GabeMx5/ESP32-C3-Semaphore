#pragma once
#include <Adafruit_NeoPixel.h>
#include <Arduino.h>
#include <functional>

#define LED_DATA_PIN 10
#define LED_COUNT 3
#define BLINK_INTERVAL_MS    500
#define RANDOM_YN_BLINK_MS   300
#define RANDOM_YN_RESULT_MS 5000

// Cycle order: top=LED2, middle=LED1, bottom=LED0
static const int CYCLE_LEDS[3] = {2, 1, 0};

class LEDController
{
public:
    Adafruit_NeoPixel strip = Adafruit_NeoPixel(LED_COUNT, LED_DATA_PIN, NEO_GRB + NEO_KHZ800);

private:
    // Per-LED state
    uint32_t colors[LED_COUNT]   = {0};
    bool     onState[LED_COUNT]  = {false};
    bool     blinking[LED_COUNT] = {false};

    // Blink
    bool          blinkState    = false;
    unsigned long lastBlinkTime = 0;
    bool          restoreNeeded = false;

    // Random Yes/No
    enum class RandomYNState { IDLE, BLINK, RESULT };
    RandomYNState randomYNState      = RandomYNState::IDLE;
    bool          randomYNBlinkPhase = false;
    bool          randomYNResult     = false;
    unsigned long randomYNPhaseStart = 0;  // inizio fase BLINK
    unsigned long randomYNLastBlink  = 0;  // ultimo toggle
    unsigned long randomYNResultStart= 0;  // inizio fase RESULT

    void randomYNTick()
    {
        if (randomYNState == RandomYNState::BLINK)
        {
            unsigned long elapsed = millis() - randomYNPhaseStart;
            if (elapsed >= 5000)
            {
                randomYNState       = RandomYNState::RESULT;
                randomYNResultStart = millis();
                randomYNResult      = random(2);
                for (int i = 0; i < LED_COUNT; i++) strip.setPixelColor(i, 0);
                if (randomYNResult)
                    strip.setPixelColor(2, strip.Color(255, 0, 0));  // top = rosso
                else
                    strip.setPixelColor(0, strip.Color(0, 255, 0));  // bottom = verde
                strip.show();
                return;
            }
            // intervallo: da 500ms a 60ms in 5 secondi
            unsigned long interval = map((long)elapsed, 0, 5000, 500, 60);
            if (millis() - randomYNLastBlink >= interval)
            {
                randomYNLastBlink  = millis();
                randomYNBlinkPhase = !randomYNBlinkPhase;
                strip.setPixelColor(1, randomYNBlinkPhase ? strip.Color(255, 200, 0) : 0);
                strip.show();
            }
        }
        else if (randomYNState == RandomYNState::RESULT)
        {
            if (millis() - randomYNResultStart < RANDOM_YN_RESULT_MS) return;
            randomYNState = RandomYNState::IDLE;
            restoreAll();
        }
    }

    // Morse
    struct MorseEvent { bool on; uint16_t ms; };
    static const int MAX_MORSE_EVENTS = 512;
    MorseEvent    morseSeq[MAX_MORSE_EVENTS];
    int           morseTotalEvents = 0;
    int           morseEventIdx    = 0;
    unsigned long morseNext        = 0;
    bool          morseRunning     = false;

    static const char* morseChar(int idx)
    {
        static const char* tbl[26] = {
            ".-","-...","-.-.","-..",".","..-.","--.","....",
            "..","---","-.-.",".-..","--","-.","---",".--.",
            "--.-",".-.","...","-","..-","...-",".--","-..-",
            "-.--","--.."
        };
        if (idx < 0 || idx >= 26) return nullptr;
        return tbl[idx];
    }

    void morseTick()
    {
        if (!morseRunning || millis() < morseNext) return;
        if (morseEventIdx >= morseTotalEvents)
        {
            morseRunning = false;
            restoreAll();
            return;
        }
        MorseEvent ev = morseSeq[morseEventIdx++];
        if (ev.on)
            for (int i = 0; i < LED_COUNT; i++) strip.setPixelColor(i, strip.Color(255, 255, 255));
        else
            for (int i = 0; i < LED_COUNT; i++) strip.setPixelColor(i, 0);
        strip.show();
        morseNext = millis() + ev.ms;
    }

    // Guess
    static constexpr int GUESS_STEPS = 24;

    bool          guessRunning      = false;
    bool          guessShowWinner   = false;
    int           guessPickedLed    = -1;
    int           guessWinnerLed    = -1;
    int           guessStep         = 0;
    int           guessOrderIdx     = 0;
    unsigned long guessNextStep     = 0;
    unsigned long guessRestoreAt    = 0;

    void guessTick()
    {
        static const uint8_t GC[][3] = {
            {255,0,0},{0,255,0},{0,0,255},{255,255,0},
            {255,0,255},{0,255,255},{255,128,0},{128,0,255}
        };

        if (guessShowWinner)
        {
            if (millis() >= guessRestoreAt)
            {
                guessRunning    = false;
                guessShowWinner = false;
                strip.clear();
                strip.show();
                restoreAll();
            }
            return;
        }

        if (millis() < guessNextStep) return;

        if (guessStep >= GUESS_STEPS)
        {
            int      ci = random(8);
            uint32_t c  = strip.Color(GC[ci][0], GC[ci][1], GC[ci][2]);
            for (int i = 0; i < LED_COUNT; i++) strip.setPixelColor(i, 0);
            strip.setPixelColor(guessWinnerLed, c);
            strip.show();
            guessShowWinner = true;
            guessRestoreAt  = millis() + 5000;
            if (onGuessResult) onGuessResult(guessWinnerLed == guessPickedLed, guessWinnerLed);
            return;
        }

        float t        = (float)guessStep / (GUESS_STEPS - 1);
        int   interval = (int)(500.0f + t * (50.0f - 500.0f));
        int   led      = CYCLE_LEDS[guessOrderIdx % 3];
        int   ci       = random(8);
        for (int i = 0; i < LED_COUNT; i++) strip.setPixelColor(i, 0);
        strip.setPixelColor(led, strip.Color(GC[ci][0], GC[ci][1], GC[ci][2]));
        strip.show();
        guessOrderIdx++;
        guessStep++;
        guessNextStep = millis() + interval;
    }

    // Rainbow
    bool  rainbowEnabled   = false;
    float rainbowCycleTime = 5.0f;

    void applyRainbow()
    {
        unsigned long cycleMs = (unsigned long)(rainbowCycleTime * 1000.0f);
        if (cycleMs == 0) cycleMs = 1;
        uint16_t hue = (uint16_t)(millis() % cycleMs * 65536UL / cycleMs);
        for (int i = 0; i < LED_COUNT; i++)
            strip.setPixelColor(i, strip.gamma32(strip.ColorHSV(hue + (uint16_t)(i * 65536 / 8), 255, 255)));
        strip.show();
    }

    // Party
    bool          partyEnabled                  = false;
    int           partyMadness                  = 5;
    bool          partyLedOn[LED_COUNT]         = {false};
    unsigned long partyNextEvent[LED_COUNT]     = {0};

    uint32_t randomColor()
    {
        return strip.gamma32(strip.ColorHSV(random(65536), 255, 255));
    }

    void partyInterval(unsigned long &minMs, unsigned long &maxMs)
    {
        maxMs = (unsigned long)map(partyMadness, 1, 10, 2000, 20);
        minMs = max(5UL, maxMs / 4);
    }

    void applyPartyStart()
    {
        unsigned long minMs, maxMs;
        partyInterval(minMs, maxMs);
        for (int i = 0; i < LED_COUNT; i++)
        {
            partyLedOn[i]     = false;
            partyNextEvent[i] = millis() + random(minMs, maxMs + 1);
            strip.setPixelColor(i, 0);
        }
        strip.show();
    }

    // Cycle
    bool          cycleEnabled   = false;
    float         topLedTime     = 5.0f;
    float         middleLedTime  = 2.0f;
    float         bottomLedTime  = 5.0f;
    int           cycleStep      = 0;
    unsigned long cycleStepStart = 0;

    float cycleStepDuration()
    {
        switch (cycleStep) {
            case 0: return topLedTime;
            case 1: return middleLedTime;
            case 2: return bottomLedTime;
        }
        return topLedTime;
    }

    void applyCycleStep()
    {
        for (int i = 0; i < LED_COUNT; i++)
            strip.setPixelColor(i, 0);
        int idx = CYCLE_LEDS[cycleStep];
        blinkState    = true;
        lastBlinkTime = millis();
        strip.setPixelColor(idx, colors[idx]);
        strip.show();
        cycleStepStart = millis();
    }

public:
    std::function<void(bool win, int led)> onGuessResult;

    void begin()
    {
        strip.begin();
        strip.show();
    }

    // ── LED state ─────────────────────────────────────────────────────────────

    void setLED(int index, int red, int green, int blue, bool on, bool blink)
    {
        if (index < 0 || index >= LED_COUNT) return;
        colors[index]   = strip.Color(red, green, blue);
        onState[index]  = on;
        blinking[index] = blink;
        if (!cycleEnabled && !partyEnabled)
        {
            if (!on)
            {
                strip.setPixelColor(index, 0);
                strip.show();
            }
            else if (!blink)
            {
                strip.setPixelColor(index, colors[index]);
                strip.show();
            }
            // on=true && blink=true: update() gestisce il lampeggio
        }
    }

    uint32_t getLEDColor(int index)
    {
        if (index < 0 || index >= LED_COUNT) return 0;
        return colors[index];
    }

    bool getLEDOn(int index)
    {
        if (index < 0 || index >= LED_COUNT) return false;
        return onState[index];
    }

    bool getLEDBlink(int index)
    {
        if (index < 0 || index >= LED_COUNT) return false;
        return blinking[index];
    }

    void restoreAll()
    {
        for (int i = 0; i < LED_COUNT; i++)
            strip.setPixelColor(i, onState[i] ? colors[i] : 0);
        strip.show();
    }

    // ── Cycle ─────────────────────────────────────────────────────────────────

    void setCycle(bool enabled, float topTime, float midTime, float botTime)
    {
        topLedTime    = topTime;
        middleLedTime = midTime;
        bottomLedTime = botTime;
        cycleEnabled  = enabled;
        if (cycleEnabled)
        {
            partyEnabled   = false;
            rainbowEnabled = false;
            cycleStep      = 0;
            applyCycleStep();
        }
        else
        {
            restoreNeeded = true;
        }
    }

    bool  getCycleEnabled()    { return cycleEnabled;   }
    float getTopLedTime()      { return topLedTime;     }
    float getMiddleLedTime()   { return middleLedTime;  }
    float getBottomLedTime()   { return bottomLedTime;  }

    // ── Party ─────────────────────────────────────────────────────────────────

    void setParty(bool enabled, int madness = -1)
    {
        if (madness >= 1 && madness <= 10) partyMadness = madness;
        partyEnabled = enabled;
        if (partyEnabled)
        {
            cycleEnabled   = false;
            rainbowEnabled = false;
            applyPartyStart();
        }
        else
        {
            restoreNeeded = true;
        }
    }

    bool getPartyEnabled()  { return partyEnabled;  }
    int  getPartyMadness()  { return partyMadness;  }

    // ── Rainbow ───────────────────────────────────────────────────────────────

    void setRainbow(bool enabled, float cycleTime)
    {
        rainbowCycleTime = cycleTime;
        rainbowEnabled   = enabled;
        if (rainbowEnabled)
        {
            cycleEnabled = false;
            partyEnabled = false;
        }
        else
        {
            restoreNeeded = true;
        }
    }

    bool  getRainbowEnabled()   { return rainbowEnabled;   }
    float getRainbowCycleTime() { return rainbowCycleTime; }

    // ── Random Yes/No ─────────────────────────────────────────────────────────

    void startRandomYesNo()
    {
        cycleEnabled   = false;
        partyEnabled   = false;
        rainbowEnabled = false;
        randomYNState      = RandomYNState::BLINK;
        randomYNBlinkPhase = true;
        randomYNPhaseStart = millis();
        randomYNLastBlink  = millis();
        for (int i = 0; i < LED_COUNT; i++) strip.setPixelColor(i, 0);
        strip.setPixelColor(1, strip.Color(255, 200, 0));
        strip.show();
    }

    bool isRandomYNRunning() { return randomYNState != RandomYNState::IDLE; }

    // ── Guess ─────────────────────────────────────────────────────────────────

    void startGuess(int picked)
    {
        cycleEnabled   = false;
        partyEnabled   = false;
        rainbowEnabled = false;
        randomYNState  = RandomYNState::IDLE;
        guessRunning   = true;
        guessShowWinner = false;
        guessPickedLed  = picked;
        guessWinnerLed  = CYCLE_LEDS[random(3)];
        guessStep       = 0;
        guessOrderIdx   = 0;
        guessNextStep   = millis();
        for (int i = 0; i < LED_COUNT; i++) strip.setPixelColor(i, 0);
        strip.show();
    }

    bool isGuessRunning() { return guessRunning; }

    // ── Morse ─────────────────────────────────────────────────────────────────

    void startMorse(const char* text)
    {
        cycleEnabled   = false;
        partyEnabled   = false;
        rainbowEnabled = false;
        randomYNState  = RandomYNState::IDLE;
        guessRunning   = false;

        morseTotalEvents = 0;
        morseEventIdx    = 0;

        static const uint16_t DOT_MS  = 200;
        static const uint16_t DASH_MS = 600;
        static const uint16_t ELEM_MS = 200;
        static const uint16_t CHAR_MS = 600;
        static const uint16_t WORD_MS = 1400;

        for (int i = 0; text[i] != '\0' && morseTotalEvents < MAX_MORSE_EVENTS - 10; i++)
        {
            char c = toupper((unsigned char)text[i]);
            if (c == ' ')
            {
                if (morseTotalEvents > 0 && !morseSeq[morseTotalEvents - 1].on)
                    morseSeq[morseTotalEvents - 1].ms = WORD_MS;
                else
                    morseSeq[morseTotalEvents++] = {false, WORD_MS};
                continue;
            }
            if (c < 'A' || c > 'Z') continue;
            const char* sym = morseChar(c - 'A');
            if (!sym) continue;
            int len = strlen(sym);
            for (int j = 0; j < len; j++)
            {
                morseSeq[morseTotalEvents++] = {true, sym[j] == '-' ? DASH_MS : DOT_MS};
                if (j < len - 1)
                    morseSeq[morseTotalEvents++] = {false, ELEM_MS};
            }
            morseSeq[morseTotalEvents++] = {false, CHAR_MS};
        }

        morseRunning = (morseTotalEvents > 0);
        morseNext    = millis();
        for (int i = 0; i < LED_COUNT; i++) strip.setPixelColor(i, 0);
        strip.show();
    }

    bool isMorseRunning() { return morseRunning; }

    // ── Update (call every loop) ───────────────────────────────────────────────

    void update()
    {
        if (restoreNeeded)
        {
            restoreNeeded = false;
            strip.clear();
            strip.show();
            restoreAll();
            return;
        }
        if (guessRunning)
        {
            guessTick();
            return;
        }
        if (morseRunning)
        {
            morseTick();
            return;
        }
        if (randomYNState != RandomYNState::IDLE)
        {
            randomYNTick();
            return;
        }
        if (cycleEnabled)
        {
            if (millis() - cycleStepStart >= (unsigned long)(cycleStepDuration() * 1000.0f))
            {
                cycleStep = (cycleStep + 1) % 3;
                applyCycleStep();
            }
            else if (blinking[CYCLE_LEDS[cycleStep]] && millis() - lastBlinkTime >= BLINK_INTERVAL_MS)
            {
                lastBlinkTime = millis();
                blinkState    = !blinkState;
                strip.setPixelColor(CYCLE_LEDS[cycleStep], blinkState ? colors[CYCLE_LEDS[cycleStep]] : 0);
                strip.show();
            }
        }
        else if (rainbowEnabled)
        {
            applyRainbow();
        }
        else if (partyEnabled)
        {
            bool changed = false;
            for (int i = 0; i < LED_COUNT; i++)
            {
                if (millis() >= partyNextEvent[i])
                {
                    partyLedOn[i] = !partyLedOn[i];
                    if (partyLedOn[i])
                        strip.setPixelColor(i, randomColor());
                    else
                        strip.setPixelColor(i, 0);
                    unsigned long pMin, pMax;
                    partyInterval(pMin, pMax);
                    partyNextEvent[i] = millis() + random(pMin, pMax + 1);
                    changed = true;
                }
            }
            if (changed) strip.show();
        }
        else
        {
            if (millis() - lastBlinkTime < BLINK_INTERVAL_MS) return;
            lastBlinkTime = millis();
            blinkState    = !blinkState;
            bool changed  = false;
            for (int i = 0; i < LED_COUNT; i++)
            {
                if (blinking[i] && onState[i])
                {
                    strip.setPixelColor(i, blinkState ? colors[i] : 0);
                    changed = true;
                }
            }
            if (changed) strip.show();
        }
    }
};
