#pragma once

#include <QMainWindow>
#include <QTabWidget>
#include <QLineEdit>
#include <QToolBar>
#include <QUrl>
#include <memory>

namespace vkb {

class BrowserTab;

// Top-level window: owns QTabWidget, URL bar, back/forward/reload buttons.
// Each tab is a BrowserTab (QWidget) embedded in the QTabWidget.
class BrowserWindow final : public QMainWindow {
    Q_OBJECT

public:
    explicit BrowserWindow(QWidget* parent = nullptr);
    ~BrowserWindow() override;

    void openTab(const QUrl& url = QUrl("about:blank"));
    void closeTab(int index);

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void onNewTab();
    void onCloseTab(int index);
    void onTabChanged(int index);
    void onNavigate();
    void onBack();
    void onForward();
    void onReload();
    void onTabTitleChanged(const QString& title);
    void onTabUrlChanged(const QUrl& url);
    void onLoadStarted();
    void onLoadFinished(bool ok);

private:
    void setupUi();
    void setupToolbar();
    void connectTab(BrowserTab* tab);

    [[nodiscard]] BrowserTab* currentTab() const;
    [[nodiscard]] BrowserTab* tabAt(int index) const;

    QTabWidget* m_tabs{nullptr};
    QLineEdit*  m_urlBar{nullptr};
    QToolBar*   m_toolbar{nullptr};
};

} // namespace vkb
