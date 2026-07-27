#include "AboutScreenComponent.h"
#include <BinaryData.h>

namespace
{
    // Design tokens from docs/design/design_handoff_kplayer_about_icons/README.md
    const juce::Colour kPanelBg        (0xff141a26);
    const juce::Colour kChromeBg       (0xff0e1320);
    const juce::Colour kBorder         (0xff232d42);
    const juce::Colour kTrafficRed     (0xffe0563f);
    const juce::Colour kTrafficInactive(0xff3a4356);
    const juce::Colour kTitleColour    (0xfff5f6f7);
    const juce::Colour kVersionColour  (0xff7b8aa3);
    const juce::Colour kWelcomeColour  (0xffc7cfdb);
    const juce::Colour kCreditsColour  (0xff8a97ac);
    const juce::Colour kCreditsLink    (0xffdfe4ec);
    const juce::Colour kLicenseColour  (0xff6d7a90);
    const juce::Colour kDefaultLink    (0xff3fd9c4);
    const juce::Colour kCopyrightColour(0xff525d70);
    const juce::Colour kProgressTrack  (0xff232d42);
    const juce::Colour kProgressFill   (0xff22c7b4);

    // Design calls for Inter (body/UI) and JetBrains Mono (the repo-URL
    // link) - neither font ships with this project, so this falls back to
    // the platform's default sans-serif/monospaced faces rather than
    // bundling two more font files for one small block of text.
    juce::Font bodyFont(float height, bool bold = false)
    {
        return juce::Font(juce::FontOptions(height, bold ? juce::Font::bold : juce::Font::plain));
    }

    juce::Font monoFont(float height)
    {
        return juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), height, juce::Font::plain));
    }
}

AboutScreenComponent::AboutScreenComponent(bool splashMode)
    : isSplash(splashMode)
{
    azonixTypeface = juce::Typeface::createSystemTypefaceFor(BinaryData::Azonix_otf, (size_t) BinaryData::Azonix_otfSize);
    kplayerIcon    = juce::ImageCache::getFromMemory(BinaryData::kplayer256_png, BinaryData::kplayer256_pngSize);

    setSize(fixedWidth, fixedHeight);
    setInterceptsMouseClicks(true, true);
}

AboutScreenComponent::~AboutScreenComponent() = default;

void AboutScreenComponent::setProgress(float newProgress)
{
    progress = juce::jlimit(0.0f, 1.0f, newProgress);
    repaint();
}

float AboutScreenComponent::layoutParagraph(const std::vector<Word>& words, juce::Rectangle<float> bounds, float lineHeight)
{
    float x = bounds.getX();
    float y = bounds.getY();
    const float startX = bounds.getX();
    const float maxX   = bounds.getRight();

    for (auto& w : words)
    {
        float wordWidth = juce::GlyphArrangement::getStringWidth(w.font, w.text);

        bool forceBreak = w.lineBreakBefore && x > startX;
        bool wrapBreak  = (x + wordWidth > maxX) && (x > startX);

        if (forceBreak || wrapBreak)
        {
            x = startX;
            y += lineHeight;
        }

        PositionedWord pw;
        pw.text   = w.text;
        pw.font   = w.font;
        pw.colour = w.colour;
        pw.isLink = w.isLink;
        pw.url    = w.url;
        pw.bounds = juce::Rectangle<float>(x, y, wordWidth, lineHeight);
        positionedWords.push_back(pw);

        x += wordWidth;
    }

    return (y + lineHeight) - bounds.getY();
}

void AboutScreenComponent::buildLayout()
{
    positionedWords.clear();

    constexpr float topBarHeight    = 30.0f;
    constexpr float footerHeight    = 28.0f;
    constexpr float contentPadTop   = 22.0f;
    constexpr float contentPadLeft  = 24.0f;
    constexpr float contentPadRight = 24.0f;
    constexpr float iconColumnWidth = 112.0f;
    constexpr float columnGap       = 22.0f;

    iconBounds = juce::Rectangle<float>(contentPadLeft + (iconColumnWidth - 96.0f) * 0.5f,
                                        topBarHeight + contentPadTop + 10.0f, 96.0f, 96.0f);

   #if JUCE_WINDOWS
    closeControlBounds = juce::Rectangle<float>((float) fixedWidth - 30.0f, 0.0f, 30.0f, topBarHeight);
   #else
    closeControlBounds = juce::Rectangle<float>(17.0f - 8.0f, 15.0f - 8.0f, 16.0f, 16.0f);
   #endif

    float textX     = contentPadLeft + iconColumnWidth + columnGap;
    float textWidth = (float) fixedWidth - contentPadRight - textX;
    float y          = topBarHeight + contentPadTop;

    titleRowBounds = juce::Rectangle<float>(textX, y, textWidth, 30.0f);
    y += 30.0f + 8.0f;

    auto welcomeFont = bodyFont(12.0f);
    auto linkFont    = welcomeFont;
    std::vector<Word> welcome = {
        { "Play ",     welcomeFont, kWelcomeColour, false, {}, false },
        { "and ",      welcomeFont, kWelcomeColour, false, {}, false },
        { "perform ",  welcomeFont, kWelcomeColour, false, {}, false },
        { "with ",     welcomeFont, kWelcomeColour, false, {}, false },
        { "your ",     welcomeFont, kWelcomeColour, false, {}, false },
        { "Kadabra",   linkFont,    kDefaultLink,   true,  juce::URL("https://www.kadabra.net"), false },
        { ", ",        welcomeFont, kWelcomeColour, false, {}, false },
        { "at ",       welcomeFont, kWelcomeColour, false, {}, false },
        { "home ",     welcomeFont, kWelcomeColour, false, {}, false },
        { "or ",       welcomeFont, kWelcomeColour, false, {}, false },
        { "on ",       welcomeFont, kWelcomeColour, false, {}, false },
        { "stage! ",   welcomeFont, kWelcomeColour, false, {}, false },
        { "Compatible ", welcomeFont, kWelcomeColour, false, {}, false },
        { "with ",     welcomeFont, kWelcomeColour, false, {}, false },
        { "your ",     welcomeFont, kWelcomeColour, false, {}, false },
        { "favorite ", welcomeFont, kWelcomeColour, false, {}, false },
        { "MIDI ",     welcomeFont, kWelcomeColour, false, {}, false },
        { "controllers ", welcomeFont, kWelcomeColour, false, {}, false },
        { "and ",      welcomeFont, kWelcomeColour, false, {}, false },
        { "audio ",    welcomeFont, kWelcomeColour, false, {}, false },
        { "interfaces.", welcomeFont, kWelcomeColour, false, {}, false },
    };
    y += layoutParagraph(welcome, { textX, y, textWidth, 0 }, 12.0f * 1.5f);
    y += 8.0f;

    auto creditsFont = bodyFont(11.0f);
    auto imiUrl    = juce::URL("https://www.innovativemusicalinstruments.com/Kplayer");
    auto tribalUrl = juce::URL("https://www.tribal-tools.com");
    std::vector<Word> credits = {
        { "Developed ",   creditsFont, kCreditsColour, false, {}, false },
        { "by ",          creditsFont, kCreditsColour, false, {}, false },
        { "IMI ",         creditsFont, kCreditsLink,   true,  imiUrl, false },
        { "Innovative ",  creditsFont, kCreditsLink,   true,  imiUrl, false },
        { "Musical ",     creditsFont, kCreditsLink,   true,  imiUrl, false },
        { "Instruments",  creditsFont, kCreditsLink,   true,  imiUrl, false },
        { ", ",           creditsFont, kCreditsColour, false, {}, false },
        { "in ",          creditsFont, kCreditsColour, false, {}, false },
        { "partnership ", creditsFont, kCreditsColour, false, {}, false },
        { "with ",        creditsFont, kCreditsColour, false, {}, false },
        { "Tribal ",      creditsFont, kCreditsLink,   true,  tribalUrl, false },
        { "Tools",        creditsFont, kCreditsLink,   true,  tribalUrl, false },
        { ", ",           creditsFont, kCreditsColour, false, {}, false },
        { "creators ",    creditsFont, kCreditsColour, false, {}, false },
        { "of ",          creditsFont, kCreditsColour, false, {}, false },
        { "the ",         creditsFont, kCreditsColour, false, {}, false },
        { "Kadabra.",     creditsFont, kCreditsColour, false, {}, false },
    };
    y += layoutParagraph(credits, { textX, y, textWidth, 0 }, 11.0f * 1.5f);
    y += 8.0f;

    auto licenseFont = bodyFont(10.5f);
    auto repoFont    = monoFont(10.0f);
    auto repoUrl     = juce::URL("https://github.com/innovative-musical-instruments/IMI-KPlayer");
    auto juceUrl     = juce::URL("https://juce.com/");
    std::vector<Word> license = {
        { "Free ",       licenseFont, kLicenseColour, false, {}, false },
        { "and ",        licenseFont, kLicenseColour, false, {}, false },
        { "open ",       licenseFont, kLicenseColour, false, {}, false },
        { "source, ",    licenseFont, kLicenseColour, false, {}, false },
        { "licensed ",   licenseFont, kLicenseColour, false, {}, false },
        { "under ",      licenseFont, kLicenseColour, false, {}, false },
        { "the ",        licenseFont, kLicenseColour, false, {}, false },
        { "GNU ",        licenseFont, kLicenseColour, false, {}, false },
        { "AGPLv3. ",    licenseFont, kLicenseColour, false, {}, false },
        { "Source ",     licenseFont, kLicenseColour, false, {}, false },
        { "& ",          licenseFont, kLicenseColour, false, {}, false },
        { "docs:",       licenseFont, kLicenseColour, false, {}, false },
        { "github.com/innovative-musical-instruments/IMI-KPlayer",
                          repoFont,    kDefaultLink,   true,  repoUrl, true },
        { "Built ",      licenseFont, kLicenseColour, false, {}, true },
        { "with ",       licenseFont, kLicenseColour, false, {}, false },
        { "JUCE",        licenseFont, kDefaultLink,   true,  juceUrl, false },
        { ".",           licenseFont, kLicenseColour, false, {}, false },
    };
    y += layoutParagraph(license, { textX, y, textWidth, 0 }, 10.5f * 1.5f);
}

void AboutScreenComponent::resized()
{
    buildLayout();
}

void AboutScreenComponent::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    g.setColour(kPanelBg);
    g.fillRoundedRectangle(bounds, 10.0f);

    if (isSplash)
    {
        // Splash: full-width progress bar instead of a title bar, no close
        // control - the splash dismisses itself once the main window is
        // ready, the user can't dismiss it early.
        g.setColour(kProgressTrack);
        g.fillRect(juce::Rectangle<float>(bounds.getX(), bounds.getY(), bounds.getWidth(), 4.0f));
        g.setColour(kProgressFill);
        g.fillRect(juce::Rectangle<float>(bounds.getX(), bounds.getY(), bounds.getWidth() * progress, 4.0f));
    }
    else
    {
        // Top bar with macOS-style traffic-light dots (About boxes aren't
        // resizable, so min/max are drawn inactive per the design spec).
        juce::Path topBarPath;
        topBarPath.addRoundedRectangle(bounds.getX(), bounds.getY(), bounds.getWidth(), 30.0f,
                                       10.0f, 10.0f, true, true, false, false);
        g.setColour(kChromeBg);
        g.fillPath(topBarPath);
        g.setColour(kBorder);
        g.drawLine(bounds.getX(), 30.0f, bounds.getRight(), 30.0f, 1.0f);

       #if JUCE_WINDOWS
        // Windows: plain title bar with a single "x" close button, per the
        // design spec, instead of macOS-style traffic lights.
        g.setColour(kVersionColour);
        g.setFont(bodyFont(14.0f));
        g.drawText("×", closeControlBounds, juce::Justification::centred);
       #else
        float dotY = 15.0f;
        float dotR = 5.0f;
        g.setColour(kTrafficRed);
        g.fillEllipse(17.0f - dotR, dotY - dotR, dotR * 2.0f, dotR * 2.0f);
        g.setColour(kTrafficInactive);
        g.fillEllipse(35.0f - dotR, dotY - dotR, dotR * 2.0f, dotR * 2.0f);
        g.fillEllipse(53.0f - dotR, dotY - dotR, dotR * 2.0f, dotR * 2.0f);
       #endif
    }

    // Footer
    g.setColour(kChromeBg);
    g.fillRect(juce::Rectangle<float>(bounds.getX(), bounds.getBottom() - 28.0f, bounds.getWidth(), 28.0f));
    g.setColour(kBorder);
    g.drawLine(bounds.getX(), bounds.getBottom() - 28.0f, bounds.getRight(), bounds.getBottom() - 28.0f, 1.0f);

    if (isSplash)
    {
        g.setColour(kProgressFill);
        g.setFont(monoFont(12.0f));
        g.drawText("LAUNCHING KPLAYER...",
                  juce::Rectangle<float>(bounds.getX() + 24.0f, bounds.getBottom() - 24.0f, 200.0f, 16.0f),
                  juce::Justification::centredLeft);
    }

    g.setColour(kCopyrightColour);
    g.setFont(bodyFont(10.5f));
    g.drawText("© 2026 IMI Innovative Musical Instruments Ltd. All rights reserved.",
              juce::Rectangle<float>(bounds.getX(), bounds.getBottom() - 24.0f, bounds.getWidth() - 24.0f, 16.0f),
              juce::Justification::centredRight);

    // Icon
    if (kplayerIcon.isValid())
        g.drawImage(kplayerIcon, iconBounds, juce::RectanglePlacement::centred);

    // Title row: "KPLAYER" wordmark + version, baseline-aligned.
    auto titleFont = azonixTypeface != nullptr
        ? juce::Font(azonixTypeface).withHeight(24.0f)
        : bodyFont(24.0f, true);
    g.setColour(kTitleColour);
    g.setFont(titleFont);
    float kplayerWidth = juce::GlyphArrangement::getStringWidth(titleFont, "KPLAYER");
    g.drawText("KPLAYER", titleRowBounds.withWidth(kplayerWidth), juce::Justification::centredLeft);

    auto versionFont = bodyFont(12.0f);
    g.setColour(kVersionColour);
    g.setFont(versionFont);
    g.drawText("Version " JUCE_APPLICATION_VERSION_STRING,
              titleRowBounds.withX(titleRowBounds.getX() + kplayerWidth + 10.0f),
              juce::Justification::centredLeft);

    // Paragraphs
    for (auto& pw : positionedWords)
    {
        g.setColour(pw.colour);
        g.setFont(pw.font);
        g.drawText(pw.text, pw.bounds, juce::Justification::centredLeft);

        if (pw.isLink)
        {
            float underlineY = pw.bounds.getBottom() - 3.0f;
            float textWidth  = juce::GlyphArrangement::getStringWidth(pw.font, pw.text.trimEnd());
            g.drawLine(pw.bounds.getX(), underlineY, pw.bounds.getX() + textWidth, underlineY, 1.0f);
        }
    }
}

void AboutScreenComponent::mouseMove(const juce::MouseEvent& e)
{
    if (! isSplash && closeControlBounds.contains(e.position))
    {
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
        return;
    }

    for (auto& pw : positionedWords)
    {
        if (pw.isLink && pw.bounds.contains(e.position))
        {
            setMouseCursor(juce::MouseCursor::PointingHandCursor);
            return;
        }
    }
    setMouseCursor(juce::MouseCursor::NormalCursor);
}

void AboutScreenComponent::mouseUp(const juce::MouseEvent& e)
{
    if (! isSplash && closeControlBounds.contains(e.position))
    {
        if (onCloseRequested)
            onCloseRequested();
        return;
    }

    for (auto& pw : positionedWords)
    {
        if (pw.isLink && pw.bounds.contains(e.position))
        {
            pw.url.launchInDefaultBrowser();
            return;
        }
    }
}
