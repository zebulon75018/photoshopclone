#pragma once
#include <QToolBar>
#include <QToolButton>
#include <QWidget>
#include <vector>

class ToolManager;

// Pastille premier plan / arrière-plan (clic : choisir, flèche : permuter, mini-carrés : couleurs par défaut).
class ColorSwatchWidget : public QWidget {
    Q_OBJECT
public:
    explicit ColorSwatchWidget(QWidget* parent = nullptr);
    QSize sizeHint() const override { return QSize(46, 50); }
protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
};

class ToolBox : public QToolBar {
    Q_OBJECT
public:
    ToolBox(ToolManager* tm, QWidget* parent = nullptr);
private:
    void refresh();
    ToolManager* m_tm;
    std::vector<QToolButton*> m_buttons;
};
