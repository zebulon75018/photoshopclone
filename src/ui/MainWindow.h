#pragma once
#include <QMainWindow>
#include <QMenu>
#include <functional>
#include "effects/Effect.h"

class CanvasView; class Document; class OptionsBar; class ToolBox; class LayersPanel; class HistoryPanel;
class ColorPanel; class SwatchesPanel; class HistogramPanel; class QTabWidget; class QLabel; class QDockWidget;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow();
    ~MainWindow() override;
    void openPath(const QString& path);

protected:
    void closeEvent(QCloseEvent*) override;
    void dragEnterEvent(QDragEnterEvent*) override;
    void dropEvent(QDropEvent*) override;

private:
    // construction
    void buildMenus();
    void buildDocks();
    void buildStatusBar();
    void buildToolShortcuts();
    QAction* add(QMenu* m, const QString& text, const QString& shortcut, const std::function<void()>& fn, bool flushTool = true);
    std::function<void()> onDoc(const std::function<void(Document*)>& f);

    // documents
    Document* doc() const;
    CanvasView* view() const;
    CanvasView* addDocument(Document* d);
    void onTabChanged(int index);
    void closeTab(int index);
    bool maybeSave(CanvasView* v);
    void updateTabTitles();
    void newDocument();
    void openDialog();
    bool saveDoc(Document* d, bool saveAs);
    void exportAs();
    void addRecent(const QString& path);

    // actions
    void runEffect(const QString& id, bool reuseLast = false);
    void repeatLastFilter();
    void doPaste(bool inPlace);
    void startTransform();
    void togglePanels();
    void showShortcuts();
    void modifySelectionPrompt(int kind, const QString& title, const QString& label, int def, int max);

    QTabWidget* m_tabs;
    OptionsBar* m_options; ToolBox* m_toolbox;
    LayersPanel* m_layers; HistoryPanel* m_history; ColorPanel* m_color; SwatchesPanel* m_swatches; HistogramPanel* m_histogram;
    QList<QDockWidget*> m_docks;
    QLabel *m_zoomLabel, *m_posLabel, *m_sizeLabel;
    QMenu* m_recentMenu = nullptr;
    EffectPtr m_lastEffect; Params m_lastParams;
    bool m_panelsHidden = false, m_closing = false;
};
