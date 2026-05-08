#include "browser/BrowserWindow.h"
#include "browser/BrowserTab.h"

#include <QAction>
#include <QCloseEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QSizePolicy>
#include <QStatusBar>
#include <QStyle>

namespace vkb {

BrowserWindow::BrowserWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle("Vulkan Browser");
    setMinimumSize(800, 600);
    resize(1280, 800);
    setupUi();
    setupToolbar();

    // Open a blank starting tab.
    openTab(QUrl("about:blank"));
}

BrowserWindow::~BrowserWindow() = default;

// ---------------------------------------------------------------------------
// Public
// ---------------------------------------------------------------------------
void BrowserWindow::openTab(const QUrl& url)
{
    auto* tab = new BrowserTab(this);
    connectTab(tab);

    int idx = m_tabs->addTab(tab, "New Tab");
    m_tabs->setCurrentIndex(idx);
    tab->navigate(url);
}

void BrowserWindow::closeTab(int index)
{
    QWidget* w = m_tabs->widget(index);
    m_tabs->removeTab(index);
    w->deleteLater();

    if (m_tabs->count() == 0)
        close();
}

// ---------------------------------------------------------------------------
// Protected
// ---------------------------------------------------------------------------
void BrowserWindow::closeEvent(QCloseEvent* event)
{
    // Close all tabs in order so Vulkan resources are cleaned up properly.
    while (m_tabs->count() > 0) {
        QWidget* w = m_tabs->widget(0);
        m_tabs->removeTab(0);
        delete w;
    }
    event->accept();
}

// ---------------------------------------------------------------------------
// Private slots
// ---------------------------------------------------------------------------
void BrowserWindow::onNewTab()
{
    openTab(QUrl("about:blank"));
}

void BrowserWindow::onCloseTab(int index)
{
    closeTab(index);
}

void BrowserWindow::onTabChanged(int index)
{
    if (index < 0) return;
    auto* tab = tabAt(index);
    if (!tab) return;
    m_urlBar->setText(tab->currentUrl().toString());
    setWindowTitle(tab->title().isEmpty() ? "Vulkan Browser" : tab->title() + " — Vulkan Browser");
}

void BrowserWindow::onNavigate()
{
    QString text = m_urlBar->text().trimmed();
    if (text.isEmpty()) return;

    // Prefix with https:// if no scheme is present.
    if (!text.contains("://"))
        text = "https://" + text;

    auto* tab = currentTab();
    if (tab) tab->navigate(QUrl(text));
}

void BrowserWindow::onBack()   { /* TODO: history */ }
void BrowserWindow::onForward(){ /* TODO: history */ }
void BrowserWindow::onReload()
{
    auto* tab = currentTab();
    if (tab) tab->navigate(tab->currentUrl());
}

void BrowserWindow::onTabTitleChanged(const QString& title)
{
    auto* tab = qobject_cast<BrowserTab*>(sender());
    if (!tab) return;
    int idx = m_tabs->indexOf(tab);
    if (idx >= 0) {
        m_tabs->setTabText(idx, title.isEmpty() ? "New Tab" : title);
        if (idx == m_tabs->currentIndex())
            setWindowTitle(title + " — Vulkan Browser");
    }
}

void BrowserWindow::onTabUrlChanged(const QUrl& url)
{
    auto* tab = qobject_cast<BrowserTab*>(sender());
    if (!tab) return;
    if (m_tabs->indexOf(tab) == m_tabs->currentIndex())
        m_urlBar->setText(url.toString());
}

void BrowserWindow::onLoadStarted()
{
    statusBar()->showMessage("Loading…");
}

void BrowserWindow::onLoadFinished(bool ok)
{
    statusBar()->showMessage(ok ? "Done" : "Load failed", 3000);
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------
void BrowserWindow::setupUi()
{
    m_tabs = new QTabWidget(this);
    m_tabs->setTabsClosable(true);
    m_tabs->setMovable(true);
    m_tabs->setDocumentMode(true);

    connect(m_tabs, &QTabWidget::tabCloseRequested, this, &BrowserWindow::onCloseTab);
    connect(m_tabs, &QTabWidget::currentChanged,    this, &BrowserWindow::onTabChanged);

    setCentralWidget(m_tabs);
    statusBar()->show();
}

void BrowserWindow::setupToolbar()
{
    m_toolbar = addToolBar("Navigation");
    m_toolbar->setMovable(false);

    auto* backAction    = m_toolbar->addAction(style()->standardIcon(QStyle::SP_ArrowBack),    "Back");
    auto* forwardAction = m_toolbar->addAction(style()->standardIcon(QStyle::SP_ArrowForward), "Forward");
    auto* reloadAction  = m_toolbar->addAction(style()->standardIcon(QStyle::SP_BrowserReload),"Reload");
    m_toolbar->addSeparator();

    m_urlBar = new QLineEdit(this);
    m_urlBar->setPlaceholderText("Enter URL or search…");
    m_urlBar->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_toolbar->addWidget(m_urlBar);
    m_toolbar->addSeparator();

    auto* newTabAction = m_toolbar->addAction(style()->standardIcon(QStyle::SP_FileIcon), "New Tab");

    connect(backAction,    &QAction::triggered, this, &BrowserWindow::onBack);
    connect(forwardAction, &QAction::triggered, this, &BrowserWindow::onForward);
    connect(reloadAction,  &QAction::triggered, this, &BrowserWindow::onReload);
    connect(newTabAction,  &QAction::triggered, this, &BrowserWindow::onNewTab);
    connect(m_urlBar,      &QLineEdit::returnPressed, this, &BrowserWindow::onNavigate);
}

void BrowserWindow::connectTab(BrowserTab* tab)
{
    connect(tab, &BrowserTab::titleChanged,  this, &BrowserWindow::onTabTitleChanged);
    connect(tab, &BrowserTab::urlChanged,    this, &BrowserWindow::onTabUrlChanged);
    connect(tab, &BrowserTab::loadStarted,   this, &BrowserWindow::onLoadStarted);
    connect(tab, &BrowserTab::loadFinished,  this, &BrowserWindow::onLoadFinished);
}

BrowserTab* BrowserWindow::currentTab() const
{
    return qobject_cast<BrowserTab*>(m_tabs->currentWidget());
}

BrowserTab* BrowserWindow::tabAt(int index) const
{
    return qobject_cast<BrowserTab*>(m_tabs->widget(index));
}

} // namespace vkb
