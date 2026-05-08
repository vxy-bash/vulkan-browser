#include "renderer/PageRenderer.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPath.h"
#include "include/core/SkFont.h"
#include "include/core/SkTypeface.h"
#include "include/core/SkMatrix.h"
#include "include/effects/SkGradientShader.h"
#include "include/core/SkRRect.h"

#include <cmath>
#include <chrono>

namespace vkb {

PageRenderer::PageRenderer(SkiaVulkanContext* skia, QObject* parent)
    : QObject(parent), m_skia(skia)
{}

PageRenderer::~PageRenderer() = default;

void PageRenderer::setSurface(SkSurface* surface)
{
    m_surface = surface;
}

void PageRenderer::navigate(const QUrl& url)
{
    m_pendingUrl = url;
    m_loading    = true;
    emit loadStarted();
    emit urlChanged(url);

#ifdef USE_ULTRALIGHT
    if (m_view) {
        ultralight::String ulUrl(url.toString().toUtf8().constData());
        m_view->LoadURL(ulUrl);
    }
#endif
}

void PageRenderer::resize(uint32_t width, uint32_t height)
{
    m_width  = width;
    m_height = height;

#ifdef USE_ULTRALIGHT
    if (m_view) m_view->Resize(width, height);
#endif
}

void PageRenderer::renderFrame(VkSemaphore imageAvail, VkSemaphore renderDone)
{
    if (!m_skia || !m_skia->ready()) return;

    SkCanvas* canvas = m_skia->canvas();
    if (!canvas) return;

#ifdef USE_ULTRALIGHT
    if (m_view) {
        m_ultralightRenderer->Update();
        m_ultralightRenderer->Render();
        // Ultralight GPU driver uploads pixels into our Skia surface here.
    }
#else
    // Stub renderer: draw a styled placeholder page.
    auto now = std::chrono::steady_clock::now();
    float t  = std::chrono::duration<float>(now.time_since_epoch()).count();

    if (m_loading) {
        drawLoadingSpinner(canvas, m_width, m_height, t);
    } else {
        drawPlaceholderPage(canvas, m_width, m_height);
    }
#endif

    m_skia->flush(imageAvail, renderDone);

    m_spinnerAngle = std::fmod(m_spinnerAngle + 3.0f, 360.0f);

    if (m_loading) {
        // Simulate page load completion after 1.5 s of spin.
        static float loadStart = 0;
        if (loadStart == 0) loadStart = std::chrono::duration<float>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        float elapsed = std::chrono::duration<float>(
            std::chrono::steady_clock::now().time_since_epoch()).count() - loadStart;
        if (elapsed > 1.5f) {
            m_loading = false;
            loadStart = 0;
            emit titleChanged(m_pendingUrl.host().isEmpty()
                                  ? QStringLiteral("New Tab")
                                  : m_pendingUrl.host());
            emit loadFinished(true);
        }
    }
}

// ---------------------------------------------------------------------------
// Drawing helpers
// ---------------------------------------------------------------------------
void PageRenderer::drawPlaceholderPage(SkCanvas* canvas, uint32_t w, uint32_t h)
{
    // Gradient background: dark blue → near-black
    SkRect rect = SkRect::MakeWH(SkScalar(w), SkScalar(h));
    SkPoint pts[2] = {{0, 0}, {0, SkScalar(h)}};
    SkColor colors[2] = {0xFF1a1a2e, 0xFF0d0d1a};
    SkPaint bgPaint;
    bgPaint.setShader(SkGradientShader::MakeLinear(pts, colors, nullptr, 2,
                                                    SkTileMode::kClamp));
    canvas->drawRect(rect, bgPaint);

    // URL bar chrome
    SkPaint barPaint;
    barPaint.setColor(0xFF2a2a3e);
    barPaint.setAntiAlias(true);
    SkRRect bar = SkRRect::MakeRectXY(
        SkRect::MakeLTRB(SkScalar(w) * 0.1f, SkScalar(h) * 0.06f,
                         SkScalar(w) * 0.9f, SkScalar(h) * 0.12f),
        8.f, 8.f);
    canvas->drawRRect(bar, barPaint);

    // URL text
    SkPaint textPaint;
    textPaint.setColor(0xFFaaaacc);
    textPaint.setAntiAlias(true);
    SkFont font;
    font.setSize(SkScalar(h) * 0.025f);
    std::string urlStr = m_pendingUrl.toString().toStdString();
    if (urlStr.empty()) urlStr = "about:blank";
    canvas->drawSimpleText(urlStr.c_str(), urlStr.size(),
                           SkTextEncoding::kUTF8,
                           SkScalar(w) * 0.12f,
                           SkScalar(h) * 0.10f,
                           font, textPaint);

    // Centered message
    SkPaint msgPaint;
    msgPaint.setColor(0xFF5555aa);
    msgPaint.setAntiAlias(true);
    SkFont bigFont;
    bigFont.setSize(SkScalar(h) * 0.04f);
    const char* msg = "Vulkan Browser — Skia rendering active";
    float msgW = bigFont.measureText(msg, std::strlen(msg), SkTextEncoding::kUTF8);
    canvas->drawSimpleText(msg, std::strlen(msg), SkTextEncoding::kUTF8,
                           (SkScalar(w) - msgW) * 0.5f,
                           SkScalar(h) * 0.5f,
                           bigFont, msgPaint);
}

void PageRenderer::drawLoadingSpinner(SkCanvas* canvas, uint32_t w, uint32_t h, float /*t*/)
{
    // Background
    canvas->clear(0xFF0d0d1a);

    float cx = SkScalar(w) * 0.5f;
    float cy = SkScalar(h) * 0.5f;
    float r  = SkScalar(std::min(w, h)) * 0.06f;

    SkPaint arcPaint;
    arcPaint.setColor(0xFF5555ff);
    arcPaint.setStyle(SkPaint::kStroke_Style);
    arcPaint.setStrokeWidth(r * 0.15f);
    arcPaint.setAntiAlias(true);
    arcPaint.setStrokeCap(SkPaint::kRound_Cap);

    canvas->save();
    canvas->translate(cx, cy);
    canvas->rotate(m_spinnerAngle);
    SkRect oval = SkRect::MakeLTRB(-r, -r, r, r);
    canvas->drawArc(oval, 0, 270, false, arcPaint);
    canvas->restore();

    // "Loading…"
    SkPaint tp;
    tp.setColor(0xFF8888cc);
    tp.setAntiAlias(true);
    SkFont f;
    f.setSize(SkScalar(h) * 0.025f);
    const char* msg = "Loading\xe2\x80\xa6"; // "Loading…"
    float msgW = f.measureText(msg, std::strlen(msg), SkTextEncoding::kUTF8);
    canvas->drawSimpleText(msg, std::strlen(msg), SkTextEncoding::kUTF8,
                           (SkScalar(w) - msgW) * 0.5f,
                           cy + r * 2.5f,
                           f, tp);
}

} // namespace vkb
