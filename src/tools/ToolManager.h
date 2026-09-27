#pragma once
#include <QList>
#include <QObject>
#include "Tool.h"

// Une case de la boîte à outils : plusieurs outils partagent une touche (Maj+touche pour alterner), comme Photoshop.
struct ToolGroup {
    QString key;                 // ex. "M"
    QList<Tool*> tools;
    int current = 0;
    Tool* active() const { return tools.value(current); }
};

class TransformTool;
class HandTool;
class ZoomTool;

class ToolManager : public QObject {
    Q_OBJECT
public:
    explicit ToolManager(QObject* parent = nullptr);
    const QList<ToolGroup>& groups() const { return m_groups; }
    Tool* current() const { return m_current; }
    Tool* byId(const QString& id) const;
    TransformTool* transform() const { return m_transform; }
    Tool* hand() const;
    Tool* zoomTool() const;

    void select(Tool* t);
    void selectGroup(int g);          // touche pressée : active l'outil courant du groupe
    void cycleGroup(int g);           // Maj+touche : outil suivant du groupe
    void restorePrevious();           // après Transformation manuelle

signals:
    void toolChanged(Tool*);
    void groupsChanged();

private:
    QList<ToolGroup> m_groups;
    Tool* m_current = nullptr;
    Tool* m_previous = nullptr;
    TransformTool* m_transform = nullptr;
};
